#pragma once
#include <string>
#include "types.hpp"

// 股票元信息 + 约束
struct StockInfo {
    std::string symbol;
    double price_limit = 0.10;   // 涨跌停幅度：主板0.10 创业板0.20 ST 0.05
    int lot_size = 100;          // 一手股数
    bool t_plus_1 = true;        // T+1
    double prev_close = 0;       // 昨收（算涨跌停用）

    double limitUp()   const { return prev_close > 0 ? prev_close * (1 + price_limit) : 0; }
    double limitDown() const { return prev_close > 0 ? prev_close * (1 - price_limit) : 0; }
};

// 持仓（T+1 用 buy_day，盈亏用 avg_cost）
struct Position {
    double qty = 0;
    double avg_cost = 0;       // 总成本（平均成本法）
    uint64_t buy_day = 0;      // 买入的"日"（T+1 检查）
};

// 时间戳 → 自然日（T+1 简化用，精确到"日"即可）
inline uint64_t dayOf(uint64_t ts) { return ts / (24ull * 3600ull * 1000ull); }