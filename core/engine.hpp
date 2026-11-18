#pragma once
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <cmath>
#include <climits>
#include "types.hpp"
#include "stock.hpp"
#include "currency.hpp"
#include "strategy.hpp"
#include "order_book.hpp"
#include "sim_book.hpp"
#include "trade_store.hpp"

// =============================================================================
// 多市场回测引擎：现金/盈亏/手续费统一以「人民币」为基准，
// 美股（USD）、加股（CAD）按 globalFxRates() 实时汇率换算。
// 佣金按「每笔订单」收取（加拿大 Questrade 有最低/最高，不能逐档重复收）。
// =============================================================================
class Engine
{
public:
    Engine(std::shared_ptr<ITradeStore> store, double tick = 0.01)
        : store_(store), tick_(tick) {}

    // 添加一只股票：信息 + 数据 + 该股票自己的策略实例
    void AddStock(const StockInfo &info, std::vector<Bar> bars, std::shared_ptr<Strategy> strat)
    {
        stocks_.push_back(StockSlot{info, std::move(bars), strat, 0, Signal::Hold});
        currencies_[info.symbol] = currencyOf(info.market);
    }

    void run(std::function<void(size_t, double)> on_step = nullptr)
    {
        double per_stock = cash_ / std::max((size_t)1, stocks_.size());   // 人民币
        std::map<std::string, double> base_qty;   // 买入持有基准的股数（外币）
        for (auto &s : stocks_)
        {
            if (!s.bars.empty())
                base_qty[s.info.symbol] = cnyToNative(per_stock, currencyOf(s.info.market)) / s.bars[0].open;
        }
        size_t step = 0;
        while (true)
        {
            // 找"下一个时间最早"的 bar（多股票按时间归并）
            int next = -1;
            uint64_t min_ts = UINT64_MAX;
            for (size_t i = 0; i < stocks_.size(); ++i)
            {
                auto &s = stocks_[i];
                if (s.idx < s.bars.size() && s.bars[s.idx].ts < min_ts)
                {
                    min_ts = s.bars[s.idx].ts;
                    next = (int)i;
                }
            }
            if (next < 0)
                break;
            processStock(next);
            double base = 0;   // 人民币
            for (auto &[sym, qty] : base_qty)
            {
                auto it = last_close_.find(sym);
                if (it != last_close_.end())
                    base += nativeToCny(qty * it->second, currencyFor(sym));
            }
            baseline_.push_back(base);
            equity_curve_.push_back(equity());
            if (on_step)
                on_step(step++, equity());
        }
        for (auto &s : stocks_)
        {
            if (s.bars.empty())
                continue;
            if (positions_[s.info.symbol].qty > 0)
            {
                // 直接按最后收盘价卖出（强制清仓，简化，跳过 T+1/跌停）
                double qty = positions_[s.info.symbol].qty;
                double price = s.bars.back().close;
                double notional = price * qty;   // 外币
                double fee = calcFee(notional, qty, false, s.info.symbol);   // 外币
                Currency cur = currencyOf(s.info.market);
                cash_ += nativeToCny(notional - fee, cur);
                positions_[s.info.symbol].qty = 0;
                positions_[s.info.symbol].avg_cost = 0;
            }
        }
    }

    double equity() const
    {
        double e = cash_;
        for (auto &[sym, pos] : positions_)
        {
            auto it = last_close_.find(sym);
            if (it != last_close_.end())
                e += nativeToCny(pos.qty * it->second, currencyFor(sym));
        }
        return e;
    }

    double finalCash() const { return cash_; }
    const std::vector<double> &tradePnl() const { return trade_pnl_; }
    const std::map<std::string, Position> &positions() const { return positions_; }
    const std::vector<double> &equityCurve() const { return equity_curve_; }
    const std::vector<double> &baseline() const { return baseline_; }

private:
    // 股票代码 → 币种（AddStock 时登记；缺失按人民币兜底）
    Currency currencyFor(const std::string& sym) const {
        auto it = currencies_.find(sym);
        return it != currencies_.end() ? it->second : Currency::CNY;
    }

    struct StockSlot
    {
        StockInfo info;
        std::vector<Bar> bars;
        std::shared_ptr<Strategy> strat;
        size_t idx = 0;
        Signal pending = Signal::Hold; // 每只股票独立的挂单
    };

    void processStock(int i)
    {
        auto &s = stocks_[i];
        const Bar &bar = s.bars[s.idx];
        s.idx++;
        // 跨日：昨收 = 上一交易日最后收盘（涨跌停精确化）
        uint64_t day = dayOf(bar.ts);
        auto &ld = last_day_[s.info.symbol];
        if (ld != 0 && day != ld)
            s.info.prev_close = last_close_[s.info.symbol];
        ld = day;
        last_close_[s.info.symbol] = bar.close;

        OrderBook book;
        book.applyQuote(SimulateBook(bar, tick_));

        // 尝试成交上一根挂单；被挡住就保留，下一根重试
        if (s.pending != Signal::Hold)
        {
            if (applyOrder(s, s.pending, book, bar))
                s.pending = Signal::Hold; // 成交了才清空
        }

        s.strat->onBar(bar);
        Signal sig = s.strat->signal();
        if (sig != Signal::Hold)
            s.pending = sig; // 新信号覆盖旧挂单
    }

    bool applyOrder(StockSlot &s, Signal sig, OrderBook &book, const Bar &bar)
    {
        auto &info = s.info;
        const std::string &sym = info.symbol;
        Currency cur = currencyOf(info.market);

        if (sig == Signal::Buy)
        {
            double ask = book.bestAsk();
            if (info.limitUp() > 0 && ask > info.limitUp())
                return false; // 涨停，保留重试
            double native_cash = cnyToNative(cash_, cur);
            double qty = native_cash / (ask * 1.001);
            qty = std::floor(qty / info.lot_size) * info.lot_size;
            if (qty < info.lot_size)
                return true; // 现金不足一手，清空（买不了）
            applyOrderFills(sym, book.matchMarketBuy(qty), bar.ts, info, cur);
            return true;
        }
        else if (sig == Signal::Sell)
        {
            auto &pos = positions_[sym];
            if (pos.qty <= 0)
                return true; // 没持仓，清空信号
            if (info.t_plus_1 && pos.buy_day >= dayOf(bar.ts))
                return false; // T+1，保留重试
            double bid = book.bestBid();
            if (info.limitDown() > 0 && bid < info.limitDown())
                return false; // 跌停，保留重试
            applyOrderFills(sym, book.matchMarketSell(pos.qty), bar.ts, info, cur);
            return true;
        }
        return true;
    }

    // 一个订单（可能吃多档 → 多个 Fill）聚合成一笔：数量、成交额、佣金按整单算，
    // 换算成人民币后更新现金/持仓。
    void applyOrderFills(const std::string &sym, const std::vector<Fill> &fills, uint64_t ts,
                         const StockInfo &info, Currency cur)
    {
        if (fills.empty())
            return;
        bool is_buy = fills.front().qty > 0;
        auto [qty, notional] = aggregateFills(fills);   // qty 正，notional 为外币
        double avg_price = qty > 0 ? notional / qty : 0;

        double native_fee = calcFee(notional, qty, is_buy, sym);   // 外币手续费（按整单）
        double cny_notional = nativeToCny(notional, cur);
        double cny_fee = nativeToCny(native_fee, cur);

        double pnl = 0;   // 人民币
        auto &pos = positions_[sym];
        if (is_buy)
        {
            cash_ -= cny_notional + cny_fee;
            pos.qty += qty;
            pos.avg_cost += notional + native_fee;   // 记录外币成本（平均成本法）
            pos.buy_day = dayOf(ts);
        }
        else
        {
            double avg = pos.qty > 0 ? pos.avg_cost / pos.qty : 0;
            double native_pnl = (notional - native_fee) - avg * qty;
            pnl = nativeToCny(native_pnl, cur);
            trade_pnl_.push_back(pnl);
            cash_ += cny_notional - cny_fee;
            pos.qty -= qty;
            pos.avg_cost -= avg * qty;
            if (pos.qty <= 0)
            {
                pos.qty = 0;
                pos.avg_cost = 0;
            }
        }
        store_->Add(Trade{sym, ts, avg_price, is_buy ? qty : -qty, cny_fee, pnl, currencyCode(cur)});
    }

    double cash_ = 1'000'000;   // 人民币基准
    std::map<std::string, Position> positions_;
    std::map<std::string, double> last_close_;
    std::map<std::string, uint64_t> last_day_;   // 每只股票上次处理的"日"（昨收逐日更新用）
    std::map<std::string, Currency> currencies_;
    std::vector<StockSlot> stocks_;
    std::shared_ptr<ITradeStore> store_;
    std::vector<double> trade_pnl_;
    std::vector<double> equity_curve_;
    std::vector<double> baseline_;
    double tick_;
};
