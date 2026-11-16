#pragma once
#include <vector>
#include <memory>
#include <cmath>
#include "types.hpp"
#include "account.hpp"
#include "strategy.hpp"
#include "order_book.hpp"
#include "sim_book.hpp"
#include "data_source.hpp"

// 统一引擎：从任意数据源读 Bar，撮合 + 记账 + 记录绩效
class Engine {
public:
    Engine(std::shared_ptr<Strategy> strat, double commission = 0.00025,
           double stamp = 0.001, double tick = 0.01)
        : strategy_(strat), commission_(commission), stamp_(stamp), tick_(tick) {}

    void run(IDataSource& src) {
        Signal pending = Signal::Hold;
        bool first = true;
        Bar bar;
        while (src.Next(bar)) {
            OrderBook book;
            book.applyQuote(SimulateBook(bar, tick_));   // 实时流可换成 applyQuote(真实五档)

            if (pending != Signal::Hold && !first) {      // 下一根开盘价成交
                applyOrder(pending, book);
                pending = Signal::Hold;
            }

            strategy_->onBar(bar);
            pending = strategy_->signal();
            equity_.push_back(acc_.equity(bar));
            first = false;
        }
        OrderBook last;
        last.applyQuote(SimulateBook(bar, tick_));
        if (acc_.position > 0) applyOrder(Signal::Sell, last);
    }

    const std::vector<double>& equity() const { return equity_; }
    const std::vector<double>& tradePnl() const { return trade_pnl_; }
    double finalCash() const { return acc_.cash; }
    size_t tradeCount() const { return trade_count_; }

private:
    void applyOrder(Signal sig, OrderBook& book) {
        std::vector<Fill> fills;
        if (sig == Signal::Buy) {
            double qty = acc_.cash / (book.bestAsk() * 1.001);
            qty = std::floor(qty / 100) * 100;          // 100 股整数倍
            if (qty < 100) return;
            fills = book.matchMarketBuy(qty);
        } else if (sig == Signal::Sell) {
            fills = book.matchMarketSell(acc_.position);
        }
        for (const auto& f : fills) applyFill(f);
    }

    void applyFill(const Fill& f) {
        bool is_buy = f.qty > 0;
        double qty = std::abs(f.qty);
        double notional = f.price * qty;
        double fee = notional * commission_;
        if (!is_buy) fee += notional * stamp_;

        if (is_buy) {
            acc_.cash -= notional + fee;
            acc_.position += qty;
            entry_qty_ += qty;
            entry_cost_ += notional + fee;
        } else {
            double avg = entry_qty_ > 0 ? entry_cost_ / entry_qty_ : 0;
            trade_pnl_.push_back((notional - fee) - avg * qty);
            acc_.cash += notional - fee;
            acc_.position -= qty;
            entry_qty_ -= qty;
            entry_cost_ -= avg * qty;
        }
        ++trade_count_;
    }

    std::shared_ptr<Strategy> strategy_;
    Account acc_;
    std::vector<double> equity_, trade_pnl_;
    double commission_, stamp_, tick_;
    double entry_qty_ = 0, entry_cost_ = 0;
    size_t trade_count_ = 0;
};