// include/order_book.hpp
#pragma once
#include <map>
#include <vector>
#include <algorithm>
#include <cmath>
#include "types.hpp"
#include "account.hpp"   // Fill 结构
#include "MemPool.hpp"

// 挂单（订单）
struct Order {
    uint64_t id = 0;
    double qty = 0;
};

// 池化订单链表（替代 std::deque<Order>），用 ZeroKV 的线程本地内存池。
// 同价位 FIFO：新单尾插，吃单从头部吃，吃光节点归还内存池。
struct OrderList {
    struct Node {
        Order order;
        Node* next = nullptr;
    };

    OrderList() = default;
    OrderList(const OrderList&) = delete;
    OrderList& operator=(const OrderList&) = delete;
    ~OrderList() { clear(); }

    void push(const Order& o) {
        Node* n = TlsMemPoolManager<Node>::instance().Alloc(o, nullptr);
        if (!tail_) { head_ = tail_ = n; }
        else { tail_->next = n; tail_ = n; }
    }

    bool empty() const { return head_ == nullptr; }
    Order& front() { return head_->order; }

    void pop_front() {
        Node* n = head_;
        head_ = head_->next;
        if (!head_) tail_ = nullptr;
        TlsMemPoolManager<Node>::instance().Free(n);
    }

    void clear() {
        while (head_) pop_front();
    }

    // 按订单 ID 删除节点（撤单用）
    bool cancel(uint64_t id) {
        Node* prev = nullptr;
        for (Node* n = head_; n; n = n->next) {
            if (n->order.id == id) {
                if (prev) prev->next = n->next; else head_ = n->next;
                if (n == tail_) tail_ = prev;
                TlsMemPoolManager<Node>::instance().Free(n);
                return true;
            }
            prev = n;
        }
        return false;
    }

    Node* head_ = nullptr;
    Node* tail_ = nullptr;
};

// 价格优先 + 时间优先（FIFO）Order Book
class OrderBook {
public:
    // 用五档快照重建盘口（回测：每根 bar 重建）
    void applyQuote(const Quote& q) {
        bids_.clear();
        asks_.clear();
        next_id_ = 1;
        for (int i = 0; i < 5; ++i) {
            if (q.bid_p[i] > 0 && q.bid_v[i] > 0) bids_[q.bid_p[i]].push(Order{next_id_++, q.bid_v[i]});
            if (q.ask_p[i] > 0 && q.ask_v[i] > 0) asks_[q.ask_p[i]].push(Order{next_id_++, q.ask_v[i]});
        }
    }

    double bestBid() const { return bids_.empty() ? 0.0 : bids_.begin()->first; }
    double bestAsk() const { return asks_.empty() ? 0.0 : asks_.begin()->first; }

    std::vector<Fill> matchMarketBuy(double qty) { return consume(asks_, qty, +1); }
    std::vector<Fill> matchMarketSell(double qty) { return consume(bids_, qty, -1); }

    std::vector<Fill> addLimitBuy(double price, double qty, uint64_t* order_id = nullptr) {
        std::vector<Fill> fills;
        if (!asks_.empty() && price >= bestAsk())
            fills = consume(asks_, qty, +1, price);
        double rest = qty - sumQty(fills);
        if (rest > 0) {
            uint64_t id = next_id_++;
            bids_[price].push(Order{id, rest});
            if (order_id) *order_id = id;
        }
        return fills;
    }

    std::vector<Fill> addLimitSell(double price, double qty, uint64_t* order_id = nullptr) {
        std::vector<Fill> fills;
        if (!bids_.empty() && price <= bestBid())
            fills = consume(bids_, qty, -1, price);
        double rest = qty - sumQty(fills);
        if (rest > 0) {
            uint64_t id = next_id_++;
            asks_[price].push(Order{id, rest});
            if (order_id) *order_id = id;
        }
        return fills;
    }

    bool cancelById(uint64_t id) {
        if (cancelIn(bids_, id)) return true;
        return cancelIn(asks_, id);
    }

    struct RestingOrder {
        bool is_buy;
        double price;
        uint64_t id;
        double qty;
    };
    std::vector<RestingOrder> allOrders() const {
        std::vector<RestingOrder> result;
        for (const auto& [price, orders] : bids_)
            for (const OrderList::Node* n = orders.head_; n; n = n->next)
                result.push_back({true, price, n->order.id, n->order.qty});
        for (const auto& [price, orders] : asks_)
            for (const OrderList::Node* n = orders.head_; n; n = n->next)
                result.push_back({false, price, n->order.id, n->order.qty});
        return result;
    }

    size_t bidLevels() const { return bids_.size(); }
    size_t askLevels() const { return asks_.size(); }

private:
    template <typename Map>
    static bool cancelIn(Map& side, uint64_t id) {
        for (auto mit = side.begin(); mit != side.end(); ++mit) {
            if (mit->second.cancel(id)) {
                if (mit->second.empty()) side.erase(mit);
                return true;
            }
        }
        return false;
    }

    static double sumQty(const std::vector<Fill>& fills) {
        double q = 0;
        for (const auto& f : fills) q += std::abs(f.qty);
        return q;
    }

    template <typename Compare>
    std::vector<Fill> consume(std::map<double, OrderList, Compare>& opposite,
                              double qty, int sign, double limit = 0.0) {
        std::vector<Fill> fills;
        fills.reserve(5);   // 最多吃 5 档，预分配避免 vector 重分配
        double remain = qty;
        auto it = opposite.begin();
        while (remain > 0 && it != opposite.end()) {
            double price = it->first;
            if (limit > 0) {
                if (sign > 0 && price > limit) break;
                if (sign < 0 && price < limit) break;
            }
            auto& orders = it->second;
            while (remain > 0 && !orders.empty()) {
                Order& o = orders.front();
                double take = std::min(remain, o.qty);
                fills.push_back(Fill{price, sign * take, 0.0});
                remain -= take;
                o.qty -= take;
                if (o.qty <= 0) orders.pop_front();   // 吃光，节点归还内存池
            }
            if (orders.empty()) it = opposite.erase(it);
            else ++it;
        }
        return fills;
    }

    std::map<double, OrderList, std::greater<double>> bids_;  // 高 → 低
    std::map<double, OrderList> asks_;                        // 低 → 高
    uint64_t next_id_ = 1;
};
