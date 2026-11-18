#pragma once
#include <string>
#include "stock.hpp"

// =============================================================================
// 币种与汇率（基准币种 = 人民币 CNY）
//
// 账户现金、资金曲线、盈亏、手续费统一以「人民币」为基准记录。
// 美股价格是美元、加股价格是加元，展示/交易时按汇率换算。
// 汇率由 GUI 的 FxService（open.er-api.com 免费源）周期刷新到 globalFxRates()，
// 未刷新前用下面的默认值兜底。
// =============================================================================

enum class Currency { CNY, USD, CAD };

inline Currency currencyOf(Market m) {
    switch (m) {
        case Market::US:     return Currency::USD;
        case Market::Canada: return Currency::CAD;
        default:             return Currency::CNY;
    }
}

inline const char* currencyCode(Currency c) {
    switch (c) {
        case Currency::USD: return "USD";
        case Currency::CAD: return "CAD";
        default:            return "CNY";
    }
}

inline const char* currencySymbol(Currency c) {
    switch (c) {
        case Currency::USD: return "US$";
        case Currency::CAD: return "C$";
        default:            return "¥";
    }
}

// 汇率（1 外币 = X 人民币；USD/CAD 为 1 美元 = X 加元）
struct FxRates {
    double usd_to_cny = 7.20;   // 美元 → 人民币（默认兜底，FxService 会刷新为实时值）
    double cad_to_cny = 5.28;   // 加元 → 人民币
    double usd_to_cad = 1.36;   // 美元 → 加元
};

inline FxRates& globalFxRates() {
    static FxRates rates;
    return rates;
}

// 外币金额 → 人民币（CNY 原样返回）
inline double nativeToCny(double amount, Currency c) {
    switch (c) {
        case Currency::USD: return amount * globalFxRates().usd_to_cny;
        case Currency::CAD: return amount * globalFxRates().cad_to_cny;
        default:            return amount;
    }
}

// 人民币 → 外币金额（CNY 原样返回；汇率非正时返回 0 防除零）
inline double cnyToNative(double cny, Currency c) {
    switch (c) {
        case Currency::USD: {
            double r = globalFxRates().usd_to_cny;
            return r > 0 ? cny / r : 0.0;
        }
        case Currency::CAD: {
            double r = globalFxRates().cad_to_cny;
            return r > 0 ? cny / r : 0.0;
        }
        default: return cny;
    }
}
