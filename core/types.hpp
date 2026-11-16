#pragma once
#include <cstdint>

// 通用 K线（Bar）
struct Bar {
    uint64_t ts = 0;          // 开盘时间（毫秒）
    double open = 0, high = 0, low = 0, close = 0;
    double volume = 0;        // 成交量（股）
    double amount = 0;        // 成交额（元，A股用；crypto 可忽略）
};

// 逐笔成交（Tick）
struct Tick {
    uint64_t ts = 0;
    double price = 0;
    double volume = 0;
    int direction = 0;        // +1 主动买 / -1 主动卖 / 0 中性
};

// Level-1 行情快照（五档）
struct Quote {
    uint64_t ts = 0;
    double last = 0, open = 0, high = 0, low = 0, pre_close = 0;
    double volume = 0, amount = 0;
    double bid_p[5] = {0}, bid_v[5] = {0};   // 买一~买五
    double ask_p[5] = {0}, ask_v[5] = {0};   // 卖一~卖五
};