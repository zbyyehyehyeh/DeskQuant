#pragma once
#include <vector>
#include <string>
#include "types.hpp"

struct Trade {
    std::string symbol;   // ← 股票代码（多股票必须）
    uint64_t ts = 0;      // 成交时间
    double price = 0;     // 成交价（标的币种：CNY/USD/CAD）
    double qty = 0;       // 正买负卖
    double fee = 0;       // 手续费（人民币，已换算）
    double pnl = 0;       // 盈亏（人民币，已换算；卖出时非 0）
    std::string currency = "CNY";  // 标的币种
};

// 交易存储接口：Add 写入，LoadAll 按时间顺序回放
class ITradeStore {
public:
    virtual ~ITradeStore() = default;
    virtual void Add(const Trade& t) = 0;
    virtual std::vector<Trade> LoadAll() = 0;
};

// 内存版
class MemoryTradeStore : public ITradeStore {
public:
    void Add(const Trade& t) override { trades_.push_back(t); }
    std::vector<Trade> LoadAll() override { return trades_; }
private:
    std::vector<Trade> trades_;
};