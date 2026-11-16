#pragma once
#include "types.hpp"

struct Account {
    double cash = 1'000'000;    // 初始资金 100 万
    double position = 0;        // 持仓股数
    double equity(const Bar& b) const { return cash + position * b.close; }
};

struct Fill {                   // 一次成交
    double price = 0;
    double qty = 0;             // 正=买入，负=卖出
    double fee = 0;
};