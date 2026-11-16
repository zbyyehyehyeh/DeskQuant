// include/order_book.hpp
#pragma once
#include <map>
#include <vector>
#include <algorithm>
#include "types.hpp"
#include "account.hpp"   // Fill 结构

// 价格分档 Order Book：买卖两侧各用 std::map 维护"价位 → 总挂单量"。
// bid 从高到低（greater），ask 从低到高，所以 begin() 永远是最优价。
class OrderBook {
public:
    // 用五档快照重建盘口（模拟盘：收到实时 Quote 时调用）
    void applyQuote(const Quote& q) {
        bids_.clear();
        asks_.clear();
        for (int i = 0; i < 5; ++i) {
            if (q.bid_p[i] > 0 && q.bid_v[i] > 0) bids_[q.bid_p[i]] = q.bid_v[i];
            if (q.ask_p[i] > 0 && q.ask_v[i] > 0) asks_[q.ask_p[i]] = q.ask_v[i];
        }
    }

    // 最优价（O(1)：map 有序，begin() 即最优）
    double bestBid() const { return bids_.empty() ? 0.0 : bids_.begin()->first; }
    double bestAsk() const { return asks_.empty() ? 0.0 : asks_.begin()->first; }

    // 市价买单：从卖一往上吃，成交价 = 各档卖价（可能多档）
    std::vector<Fill> matchMarketBuy(double qty) {
        return consume(asks_, qty, +1);
    }

    // 市价卖单：从买一往下吃
    std::vector<Fill> matchMarketSell(double qty) {
        return consume(bids_, qty, -1);
    }

    // 限价买单：限价 ≥ bestAsk 则立即成交，否则挂单（成交价 = 对手价，享受价格改善）
    std::vector<Fill> addLimitBuy(double price, double qty) {
        if (!asks_.empty() && price >= bestAsk())
            return matchMarketBuy(qty);     // 交叉：吃单
        bids_[price] += qty;                // 未交叉：挂单
        return {};
    }

    // 限价卖单
    std::vector<Fill> addLimitSell(double price, double qty) {
        if (!bids_.empty() && price <= bestBid())
            return matchMarketSell(qty);
        asks_[price] += qty;
        return {};
    }

    // 撤单（挂单侧）
        // 撤单（挂单侧）
    void cancel(bool is_buy, double price, double qty) {
        if (is_buy) {
            // 在买单侧撤单
            auto it = bids_.find(price);
            if (it != bids_.end()) {
                it->second -= qty;
                if (it->second <= 0) bids_.erase(it);
            }
        } else {
            // 在卖单侧撤单
            auto it = asks_.find(price);
            if (it != asks_.end()) {
                it->second -= qty;
                if (it->second <= 0) asks_.erase(it);
            }
        }
    }

    // 盘口深度（档位数），供诊断/风控
    size_t bidLevels() const { return bids_.size(); }
    size_t askLevels() const { return asks_.size(); }

private:
    private:
    // 吃单：从对手方最优价位开始，逐档成交，直到 qty 耗尽或对手盘空
    // 模板参数兼容 bids_(greater) 和 asks_(less) 两种排序
    template <typename Compare>
    std::vector<Fill> consume(std::map<double, double, Compare>& opposite,
                              double qty, int sign) {
        std::vector<Fill> fills;
        double remain = qty;
        auto it = opposite.begin();                 // 对手最优价
        while (remain > 0 && it != opposite.end()) {
            double price = it->first;
            double take  = std::min(remain, it->second);
            fills.push_back(Fill{price, sign * take, 0.0});   // sign:+1买 -1卖
            remain -= take;
            it->second -= take;
            if (it->second <= 0) it = opposite.erase(it);     // 档位吃光，删掉
        }
        return fills;
    }

    std::map<double, double, std::greater<double>> bids_;   // 高 → 低
    std::map<double, double> asks_;                         // 低 → 高
};