#pragma once
#include <vector>
#include <memory>
#include <cmath>
#include "types.hpp"
#include "account.hpp"
#include "strategy.hpp"
#include "order_book.hpp"
#include "sim_book.hpp"

// 事件循环：逐根 K线，用"模拟五档"撮合。
// 信号用 bar[i] 收盘价产生，成交用 bar[i+1] 的模拟盘口（开盘价）→ 规避未来函数。
class Backtester
{
public:
    Backtester(std::vector<Bar> bars, std::shared_ptr<Strategy> strat,
               double commission = 0.00025, double stamp = 0.001, double tick = 0.01)
        : bars_(std::move(bars)), strategy_(strat),
          commission_(commission), stamp_(stamp), tick_(tick) {}

    void run()
    {
        Signal pending = Signal::Hold;
        for (size_t i = 0; i < bars_.size(); ++i)
        {
            const Bar &bar = bars_[i];

            // ① 用这一根开盘价模拟五档
            OrderBook book;
            book.applyQuote(SimulateBook(bar, tick_));

            // ② 上一根挂单，用这一根盘口撮合（市价单）
            if (pending != Signal::Hold && i > 0)
            {
                applyOrder(pending, book);
                pending = Signal::Hold;
            }

            // ③ 策略看这根 K线
            strategy_->onBar(bar);
            // ④ 策略产生信号（基于这根收盘价），挂到下一根
            pending = strategy_->signal();
            // ⑤ 记录净值（按收盘价估值）
            equity_.push_back(acc_.equity(bar));
        }
        // ⑥ 结束清仓
        OrderBook last_book;
        last_book.applyQuote(SimulateBook(bars_.back(), tick_));
        if (acc_.position > 0)
            applyOrder(Signal::Sell, last_book);
    }

    const std::vector<double>& tradePnl() const { return trade_pnl_; }
    const std::vector<double> &equity() const { return equity_; }
    double finalCash() const { return acc_.cash; }
    size_t tradeCount() const { return trade_count_; }

private:
    // 市价单：吃对手方盘口，多档成交（天然产生滑点），费用单独算
    void applyOrder(Signal sig, OrderBook &book)
    {
        std::vector<Fill> fills;
        if (sig == Signal::Buy)
        {
            double qty = acc_.cash / (book.bestAsk() * 1.001);
            qty = std::floor(qty / 100) * 100; // 向下取整到整手
            if (qty < 100)
                return; // 全仓买入（预留手续费）
            fills = book.matchMarketBuy(qty);
        }
        else if (sig == Signal::Sell)
        {
            fills = book.matchMarketSell(acc_.position);
        }
        for (const auto &f : fills)
            applyFill(f);
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
            entry_cost_ += notional + fee;       // ← 记买入成本
        } else {
            double avg_cost = entry_qty_ > 0 ? entry_cost_ / entry_qty_ : 0;
            double pnl = (notional - fee) - avg_cost * qty;   // ← 卖出盈亏
            trade_pnl_.push_back(pnl);
            acc_.cash += notional - fee;
            acc_.position -= qty;
            entry_qty_ -= qty;
            entry_cost_ -= avg_cost * qty;       // ← 摊薄剩余成本
        }
        ++trade_count_;
    }

    std::vector<Bar> bars_;
    std::shared_ptr<Strategy> strategy_;
    Account acc_;
    std::vector<double> equity_;
    double commission_, stamp_, tick_;
    size_t trade_count_ = 0;
    double entry_qty_ = 0, entry_cost_ = 0;   // 持仓成本（均价法）
    std::vector<double> trade_pnl_;           // 每回合盈亏
};