#pragma once
#include <vector>
#include <utility>
#include <cmath>
#include "types.hpp"

struct Account {
    double cash = 1'000'000;    // 初始资金 100 万（人民币基准）
    double position = 0;        // 持仓股数
    double equity(const Bar& b) const { return cash + position * b.close; }
};

struct Fill {                   // 一次成交
    double price = 0;
    double qty = 0;             // 正=买入，负=卖出
    double fee = 0;
};

// 把一组 Fill（一个订单吃多档产生的）聚合成 (总数量, 总成交额)，
// 供「按订单」收取佣金（加拿大 Questrade 有最低/最高，必须按整单算，不能逐档重复收最低）。
inline std::pair<double, double> aggregateFills(const std::vector<Fill>& fills) {
    double qty = 0, notional = 0;
    for (const auto& f : fills) {
        qty += std::abs(f.qty);
        notional += f.price * std::abs(f.qty);
    }
    return {qty, notional};
}