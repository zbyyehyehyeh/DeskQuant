#pragma once
#include <vector>
#include <cmath>
#include <cstdint>

struct Performance {
    double total_return = 0;      // 总收益率
    double annualized = 0;        // 年化收益率
    double max_drawdown = 0;      // 最大回撤
    double sharpe = 0;            // 夏普比率
    double win_rate = 0;          // 胜率
    double profit_factor = 0;     // 盈亏比
    int trades = 0;               // 交易次数（回合）
};

// equity: 资金曲线；bars_per_year: 每年 bar 数（5分钟线 ≈ 11520）；
// trade_pnl: 每笔回合交易的盈亏（买→卖）
inline Performance ComputePerformance(const std::vector<double>& equity,
                                      double bars_per_year,
                                      const std::vector<double>& trade_pnl) {
    Performance p;
    size_t n = equity.size();
    if (n < 2) return p;

    // ① 总收益
    p.total_return = equity.back() / equity.front() - 1;

    // ② 年化收益（几何复利）
    double years = n / bars_per_year;
    p.annualized = (years > 0) ? std::pow(1 + p.total_return, 1.0 / years) - 1 : 0;

    // ③ 最大回撤
    double peak = equity[0], max_dd = 0;
    for (double e : equity) {
        peak = std::max(peak, e);
        max_dd = std::max(max_dd, (peak - e) / peak);
    }
    p.max_drawdown = max_dd;

    // ④ 夏普比率：per-bar 收益的 均值/标准差 × sqrt(每年 bar 数)
    double mean = 0, var = 0;
    for (size_t i = 1; i < n; ++i) {
        double r = equity[i] / equity[i - 1] - 1;
        mean += r;
        var += r * r;
    }
    mean /= (n - 1);
    var = var / (n - 1) - mean * mean;
    double stddev = std::sqrt(std::max(var, 0.0));
    p.sharpe = (stddev > 0) ? mean / stddev * std::sqrt(bars_per_year) : 0;

    // ⑤ 胜率 + 盈亏比
    double wins = 0, profit = 0, loss = 0;
    for (double pnl : trade_pnl) {
        if (pnl > 0) { wins += 1; profit += pnl; }
        else         { loss += -pnl; }
    }
    p.trades = static_cast<int>(trade_pnl.size());
    p.win_rate = p.trades ? wins / p.trades : 0;
    p.profit_factor = (loss > 0) ? profit / loss : (profit > 0 ? INFINITY : 0);

    return p;
}