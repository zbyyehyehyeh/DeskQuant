#pragma once
#include <vector>
#include <cmath>
#include <numeric>
#include "strategy.hpp"

// 布林带：跌破下轨买入，回到中轨卖出
class BollingerStrategy : public Strategy {
    std::vector<double> closes_;
    int period_;
    double k_;
    mutable bool holding_ = false;
public:
    BollingerStrategy(int period = 20, double k = 2.0) : period_(period), k_(k) {}
    void onBar(const Bar& b) override {
        closes_.push_back(b.close);
        if ((int)closes_.size() > period_) closes_.erase(closes_.begin());
    }
    Signal signal() const override {
        if ((int)closes_.size() < period_) return Signal::Hold;
        double mean = std::accumulate(closes_.begin(), closes_.end(), 0.0) / period_;
        double var = 0;
        for (double c : closes_) var += (c - mean) * (c - mean);
        double sd = std::sqrt(var / period_);
        double lower = mean - k_ * sd, upper = mean + k_ * sd;
        double price = closes_.back();

        if (!holding_ && price < lower) { holding_ = true;  return Signal::Buy;  }
        if (holding_ && price > mean)  { holding_ = false; return Signal::Sell; }
        return Signal::Hold;
    }
};
REGISTER_STRATEGY("bollinger", BollingerStrategy);