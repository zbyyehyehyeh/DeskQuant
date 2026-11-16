#pragma once
#include <vector>
#include <algorithm>
#include "strategy.hpp"

// 唐奇安突破：突破 N 日最高买入，跌破 N 日最低卖出
class BreakoutStrategy : public Strategy {
    std::vector<double> highs_, lows_;
    int period_;
    mutable bool holding_ = false;
public:
    BreakoutStrategy(int period = 20) : period_(period) {}
    void onBar(const Bar& b) override {
        closes_back_ = b.close;
        highs_.push_back(b.high);
        lows_.push_back(b.low);
        if ((int)highs_.size() > period_) { highs_.erase(highs_.begin()); lows_.erase(lows_.begin()); }
    }
    Signal signal() const override {
        if ((int)highs_.size() < period_) return Signal::Hold;
        double hh = *std::max_element(highs_.begin(), highs_.end() - 1);  // 前 N-1 根最高（不含当前）
        double ll = *std::min_element(lows_.begin(), lows_.end() - 1);
        double price = closes_back_;
        if (!holding_ && price > hh) { holding_ = true;  return Signal::Buy;  }
        if (holding_ && price < ll) { holding_ = false; return Signal::Sell; }
        return Signal::Hold;
    }
private:
    double closes_back_ = 0;
    // 上面 price 用最新收盘价，需要在 onBar 里更新 closes_back_
};
REGISTER_STRATEGY("breakout", BreakoutStrategy);