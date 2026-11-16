#pragma once
#include <algorithm>
#include "types.hpp"

// 从一根 Bar 的 OHLC 模拟五档盘口。
// mid = 开盘价（成交发生在开盘附近），按 tick 铺五档，量按成交量分摊。
inline Quote SimulateBook(const Bar& bar, double tick = 0.01, double vol_ratio = 0.2) {
    Quote q;
    q.ts = bar.ts;
    q.last = bar.open;
    q.open = bar.open;
    q.high = bar.high;
    q.low = bar.low;
    q.pre_close = 0.0;              // 可外部传昨收
    q.volume = bar.volume;
    q.amount = bar.amount;

    double mid = bar.open;
    double t = std::max(tick, mid * 1e-4);   // 最小 tick（A股 0.01，crypto 可设更小）
    double level_vol = bar.volume * vol_ratio;  // 每档挂单量 = 成交量 × 比例

    for (int i = 0; i < 5; ++i) {
        q.bid_p[i] = mid - t * (i + 1);
        q.bid_v[i] = level_vol;
        q.ask_p[i] = mid + t * (i + 1);
        q.ask_v[i] = level_vol;
    }
    return q;
}