#pragma once
#include <vector>
#include <algorithm>
#include <numeric>
#include "strategy.hpp"

// RSI 均值回归：RSI < 30 超卖买入，RSI > 70 超买卖出
class RSIStrategy : public Strategy {
    std::vector<double> gains_, losses_;
    int period_, ready_ = 0;
    double rsi_ = 50, prev_close_ = 0;
    double ob_, os_;
    mutable bool holding_ = false;   // signal 是 const，状态用 mutable

public:
    RSIStrategy(int period = 14, double overbought = 70, double oversold = 30)
        : period_(period), ob_(overbought), os_(oversold) {}

    void onBar(const Bar& b) override {          // ① 更新状态
        if (prev_close_ > 0) {
            double diff = b.close - prev_close_;
            gains_.push_back(std::max(diff, 0.0));
            losses_.push_back(std::max(-diff, 0.0));
            if ((int)gains_.size() > period_) { gains_.erase(gains_.begin()); losses_.erase(losses_.begin()); }
            if ((int)gains_.size() >= period_) {
                double ag = std::accumulate(gains_.begin(), gains_.end(), 0.0) / period_;
                double al = std::accumulate(losses_.begin(), losses_.end(), 0.0) / period_;
                rsi_ = (al == 0) ? 100.0 : 100 - 100 / (1 + ag / al);
                ++ready_;
            }
        }
        prev_close_ = b.close;
    }

    Signal signal() const override {             // ② 返回信号
        if (ready_ < 1) return Signal::Hold;
        if (!holding_ && rsi_ < os_) { holding_ = true;  return Signal::Buy;  }
        if (holding_ && rsi_ > ob_) { holding_ = false; return Signal::Sell; }
        return Signal::Hold;
    }
};
REGISTER_STRATEGY("rsi", RSIStrategy);