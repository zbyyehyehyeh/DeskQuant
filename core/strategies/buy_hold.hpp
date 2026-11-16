#pragma once
#include "strategy.hpp"

class BuyAndHold : public Strategy {
    mutable bool bought_ = false;
public:
    void onBar(const Bar&) override {}
    Signal signal() const override {
        if (!bought_) { bought_ = true; return Signal::Buy; }
        return Signal::Hold;
    }
};
REGISTER_STRATEGY("buy_hold", BuyAndHold);