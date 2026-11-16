#pragma once
#include <vector>
#include "strategy.hpp"

class MACrossover : public Strategy {
    std::vector<double> c_;
    int sp_, lp_, ready_ = 0;
    double cs_ = 0, cl_ = 0, ps_ = 0, pl_ = 0;
    double ma(int p) const {
        double s = 0;
        for (int i = (int)c_.size() - p; i < (int)c_.size(); ++i) s += c_[i];
        return s / p;
    }
public:
    MACrossover(int sp = 20, int lp = 50) : sp_(sp), lp_(lp) {}
    void onBar(const Bar& b) override {
        c_.push_back(b.close);
        if ((int)c_.size() > lp_) c_.erase(c_.begin());
        if ((int)c_.size() < lp_) return;
        ps_ = cs_; pl_ = cl_;
        cs_ = ma(sp_); cl_ = ma(lp_);
        ++ready_;
    }
    Signal signal() const override {
        if (ready_ < 2) return Signal::Hold;
        if (ps_ <= pl_ && cs_ > cl_) return Signal::Buy;
        if (ps_ >= pl_ && cs_ < cl_) return Signal::Sell;
        return Signal::Hold;
    }
};
REGISTER_STRATEGY("ma_cross", MACrossover);