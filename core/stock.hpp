#pragma once
#include <string>
#include <string_view>
#include <algorithm>
#include <cctype>
#include <cmath>
#include "types.hpp"

// =============================================================================
// 统一市场模型（国内 A股 / 美股 / 加股）
//
// 一个 DeskQuant 同时支持三种市场。撮合规则（涨跌停 / T+1 / 一手 / 佣金）
// 全部按「股票代码」自动判定，不再区分国内版/国外版两个代码库。
//
// 判定规则（marketOf）：
//   加股  符号以 .TO（多伦多主板）/ .V（TSXV）/ .CA 结尾（不区分大小写）
//   国内  符号以 sh/sz 开头（sh600000、sz000001、sh.600000），或 6 位纯数字
//   美股  其余默认（AAPL、MSFT、BRK.B 等）
// =============================================================================

enum class Market { China, US, Canada, Unknown };

inline bool endsWithIgnoreCase(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        char a = static_cast<char>(std::toupper(static_cast<unsigned char>(s[s.size() - suffix.size() + i])));
        char b = static_cast<char>(std::toupper(static_cast<unsigned char>(suffix[i])));
        if (a != b) return false;
    }
    return true;
}

inline bool startsWithIgnoreCase(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        char a = static_cast<char>(std::toupper(static_cast<unsigned char>(s[i])));
        char b = static_cast<char>(std::toupper(static_cast<unsigned char>(prefix[i])));
        if (a != b) return false;
    }
    return true;
}

inline Market marketOf(const std::string& symbol) {
    if (symbol.empty()) return Market::Unknown;

    // 加股：.TO / .V / .CA
    if (endsWithIgnoreCase(symbol, ".TO") || endsWithIgnoreCase(symbol, ".V") ||
        endsWithIgnoreCase(symbol, ".CA"))
        return Market::Canada;

    // 国内：sh/sz 前缀
    if (startsWithIgnoreCase(symbol, "sh") || startsWithIgnoreCase(symbol, "sz"))
        return Market::China;

    // 国内：6 位纯数字代码
    bool all_digit = true;
    for (char c : symbol) {
        if (!std::isdigit(static_cast<unsigned char>(c))) { all_digit = false; break; }
    }
    if (all_digit && symbol.size() == 6) return Market::China;

    // 其余默认美股
    return Market::US;
}

inline const char* marketName(Market m) {
    switch (m) {
        case Market::China:   return "A股";
        case Market::US:      return "美股";
        case Market::Canada:  return "加股";
        default:              return "未知";
    }
}

// 股票元信息 + 约束
struct StockInfo {
    std::string symbol;
    Market market = Market::China;
    double price_limit = 0.10;   // 涨跌停幅度：主板0.10 创业板0.20；美股/加股 0=不限制
    int lot_size = 100;          // 一手股数：A股100，美股/加股1
    bool t_plus_1 = true;        // T+1：A股true，美股/加股false
    double prev_close = 0;       // 昨收（算涨跌停用）

    // 无涨跌停时返回 0，引擎据此跳过限制检查
    double limitUp()   const { return (price_limit > 0 && prev_close > 0) ? prev_close * (1 + price_limit) : 0; }
    double limitDown() const { return (price_limit > 0 && prev_close > 0) ? prev_close * (1 - price_limit) : 0; }
};

// 持仓（T+1 用 buy_day，盈亏用 avg_cost）
struct Position {
    double qty = 0;
    double avg_cost = 0;       // 总成本（平均成本法）
    uint64_t buy_day = 0;      // 买入的"日"（T+1 检查）
};

// 时间戳 → 自然日（T+1 简化用，精确到"日"即可）
inline uint64_t dayOf(uint64_t ts) { return ts / (24ull * 3600ull * 1000ull); }

// 根据股票代码判断涨跌停幅度（主板 10%，创业板/科创板 20%）
inline double priceLimitFor(const std::string& symbol) {
    std::string code = symbol;
    size_t dot = code.find('.');
    if (dot != std::string::npos) code = code.substr(dot + 1);
    if (code.rfind("300", 0) == 0 || code.rfind("301", 0) == 0) return 0.20;  // 创业板
    if (code.rfind("688", 0) == 0 || code.rfind("689", 0) == 0) return 0.20;  // 科创板
    return 0.10;   // 主板
}

// 沪市判断（过户费：沪市收，深市免）
inline bool isShanghaiSymbol(const std::string& sym) {
    if (startsWithIgnoreCase(sym, "sh")) return true;
    return !sym.empty() && sym[0] == '6';
}

// 按代码自动构造 StockInfo（涨跌停 / 一手 / T+1 全自动填好）
inline StockInfo makeStockInfo(const std::string& symbol, double prev_close = 0.0) {
    StockInfo info;
    info.symbol = symbol;
    info.market = marketOf(symbol);
    info.prev_close = prev_close;

    switch (info.market) {
        case Market::China:
            info.price_limit = priceLimitFor(symbol);
            info.lot_size = 100;
            info.t_plus_1 = true;
            break;
        case Market::US:
            info.price_limit = 0.0;
            info.lot_size = 1;
            info.t_plus_1 = false;
            break;
        case Market::Canada:
            info.price_limit = 0.0;   // 加股主板无涨跌停（有单日波动暂停，简化为不限制）
            info.lot_size = 1;
            info.t_plus_1 = false;    // 加股 T+0（多数账户当日可卖）
            break;
        default:
            info.price_limit = 0.0;
            info.lot_size = 1;
            info.t_plus_1 = false;
            break;
    }
    return info;
}

// =============================================================================
// 费用模型（按市场自动切换）
//
// 国内 A股（2024）：
//   佣金 万2.5（最低 5 元）+ 印花税 万5（仅卖出）+ 过户费 万0.1（仅沪市）
// 美股：
//   零佣金（Robinhood / 多数零售券商），无印花税，无过户费
// 加股（Questrade 模型，用户已确认）：
//   佣金 C$0.01/股，最低 C$4.95，最高 C$9.95；另加 ECN 费 C$0.0035/股
//   费用与 notional 同币种（加股以加元计价）
// =============================================================================
inline double calcFee(double notional, double qty, bool is_buy, const std::string& symbol) {
    const Market m = marketOf(symbol);

    if (m == Market::China) {
        double fee = std::max(5.0, notional * 0.00025);   // 佣金 万2.5，最低 5 元
        if (!is_buy) fee += notional * 0.0005;             // 印花税 万5（卖出）
        if (isShanghaiSymbol(symbol)) fee += notional * 0.00001; // 过户费 万0.1（沪市）
        return fee;
    }

    if (m == Market::Canada) {
        // Questrade：C$0.01/股，最低 4.95，最高 9.95；ECN C$0.0035/股（买卖均收）
        const double per_share  = qty * 0.01;
        const double commission = std::min(9.95, std::max(4.95, per_share));
        const double ecn        = qty * 0.0035;
        return commission + ecn;
    }

    // 美股（默认）：零佣金
    (void)notional; (void)qty; (void)is_buy;
    return 0.0;
}
