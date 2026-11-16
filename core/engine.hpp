#pragma once
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <cmath>
#include <climits>
#include "types.hpp"
#include "stock.hpp"
#include "strategy.hpp"
#include "order_book.hpp"
#include "sim_book.hpp"
#include "trade_store.hpp"

class Engine
{
public:
    Engine(std::shared_ptr<ITradeStore> store,
           double commission = 0.00025, double stamp = 0.001, double tick = 0.01)
        : store_(store), commission_(commission), stamp_(stamp), tick_(tick) {}

    // 添加一只股票：信息 + 数据 + 该股票自己的策略实例
    void AddStock(const StockInfo &info, std::vector<Bar> bars, std::shared_ptr<Strategy> strat)
    {
        stocks_.push_back(StockSlot{info, std::move(bars), strat, 0, Signal::Hold});
    }

    void run(std::function<void(size_t, double)> on_step = nullptr)
    {
        double per_stock = cash_ / std::max((size_t)1, stocks_.size());
        std::map<std::string, double> base_qty;
        for (auto &s : stocks_)
        {
            if (!s.bars.empty())
                base_qty[s.info.symbol] = per_stock / s.bars[0].open;
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
            double base = 0;
            for (auto &[sym, qty] : base_qty)
            {
                auto it = last_close_.find(sym);
                if (it != last_close_.end())
                    base += qty * it->second;
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
            OrderBook book;
            book.applyQuote(SimulateBook(s.bars.back(), tick_));
            if (positions_[s.info.symbol].qty > 0)
            {
                // 直接按最后收盘价卖出（简化，跳过 T+1/跌停，因为是强制清仓）
                double qty = positions_[s.info.symbol].qty;
                double price = s.bars.back().close;
                double notional = price * qty;
                double fee = notional * (commission_ + stamp_);
                cash_ += notional - fee;
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
                e += pos.qty * it->second;
        }
        return e;
    }

    double finalCash() const { return cash_; }
    const std::vector<double> &tradePnl() const { return trade_pnl_; }
    const std::map<std::string, Position> &positions() const { return positions_; }
    const std::vector<double> &equityCurve() const { return equity_curve_; }
    const std::vector<double> &baseline() const { return baseline_; }

private:
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

        if (sig == Signal::Buy)
        {
            double ask = book.bestAsk();
            if (info.limitUp() > 0 && ask > info.limitUp())
                return false; // 涨停，保留重试
            double qty = cash_ / (ask * 1.001);
            qty = std::floor(qty / info.lot_size) * info.lot_size;
            if (qty < info.lot_size)
                return true; // 现金不足一手，清空（买不了）
            for (auto &f : book.matchMarketBuy(qty))
                applyFill(sym, f, bar.ts, info);
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
            for (auto &f : book.matchMarketSell(pos.qty))
                applyFill(sym, f, bar.ts, info);
            return true;
        }
        return true;
    }

    void applyFill(const std::string &sym, const Fill &f, uint64_t ts, const StockInfo &info)
    {
        bool is_buy = f.qty > 0;
        double qty = std::abs(f.qty);
        double notional = f.price * qty;
        double fee = notional * commission_;
        if (!is_buy)
            fee += notional * stamp_;

        double pnl = 0;
        auto &pos = positions_[sym];
        if (is_buy)
        {
            cash_ -= notional + fee;
            pos.qty += qty;
            pos.avg_cost += notional + fee;
            pos.buy_day = dayOf(ts);
        }
        else
        {
            double avg = pos.qty > 0 ? pos.avg_cost / pos.qty : 0;
            pnl = (notional - fee) - avg * qty;
            trade_pnl_.push_back(pnl);
            cash_ += notional - fee;
            pos.qty -= qty;
            pos.avg_cost -= avg * qty;
            if (pos.qty <= 0)
            {
                pos.qty = 0;
                pos.avg_cost = 0;
            }
        }
        store_->Add(Trade{sym, ts, f.price, f.qty, fee, pnl});
    }

    double cash_ = 1'000'000;
    std::map<std::string, Position> positions_;
    std::map<std::string, double> last_close_;
    std::vector<StockSlot> stocks_;
    std::shared_ptr<ITradeStore> store_;
    std::vector<double> trade_pnl_;
    std::vector<double> equity_curve_;
    std::vector<double> baseline_;
    double commission_, stamp_, tick_;
};