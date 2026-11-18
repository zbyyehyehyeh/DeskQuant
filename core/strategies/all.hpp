#pragma once
#include "buy_hold.hpp"
#include "ma_cross.hpp"
#include "rsi.hpp"
#include "bollinger.hpp"
#include "breakout.hpp"

// 用参数创建策略（params 为参数数组，缺省用默认值）
inline std::shared_ptr<Strategy> createStrategyWithParams(const std::string& name,
                                                          const std::vector<double>& p) {
    auto get = [&](size_t i, double d) { return i < p.size() ? p[i] : d; };
    if (name == "ma_cross")  return std::make_shared<MACrossover>((int)get(0, 20), (int)get(1, 50));
    if (name == "rsi")       return std::make_shared<RSIStrategy>((int)get(0, 14), get(1, 70), get(2, 30));
    if (name == "bollinger") return std::make_shared<BollingerStrategy>((int)get(0, 20), get(1, 2.0));
    if (name == "breakout")  return std::make_shared<BreakoutStrategy>((int)get(0, 20));
    return StrategyFactory::instance().create(name);
}

// 各策略默认参数（GUI 显示用）
inline std::string defaultParamsFor(const std::string& name) {
    if (name == "ma_cross") return "20,50";
    if (name == "rsi") return "14,70,30";
    if (name == "bollinger") return "20,2.0";
    if (name == "breakout") return "20";
    return "";
}

// 各策略参数名（GUI 显示含义用）
inline std::vector<std::string> paramNamesFor(const std::string& name) {
    if (name == "ma_cross") return {"短周期", "长周期"};
    if (name == "rsi") return {"周期", "超买阈值", "超卖阈值"};
    if (name == "bollinger") return {"周期", "标准差倍数"};
    if (name == "breakout") return {"周期"};
    return {};
}
