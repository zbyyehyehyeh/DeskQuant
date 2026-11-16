#pragma once
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "types.hpp"
#include "data_loader.hpp"

// 数据源接口：不断产出 Bar（三种模式共用）
class IDataSource {
public:
    virtual ~IDataSource() = default;
    virtual bool Next(Bar& out) = 0;   // false = 流结束
};

// 历史数据源（回测）：CSV 一次读入，逐根返回
class HistoricalSource : public IDataSource {
public:
    explicit HistoricalSource(const std::string& path) : bars_(LoadBars(path)) {}
    explicit HistoricalSource(std::vector<Bar> bars) : bars_(std::move(bars)) {} 
    bool Next(Bar& out) override {
        if (idx_ >= bars_.size()) return false;
        out = bars_[idx_++];
        return true;
    }
    size_t size() const { return bars_.size(); }
private:
    std::vector<Bar> bars_;
    size_t idx_ = 0;
};

// 实时数据源（模拟盘）：行情接收线程 Push，Next() 阻塞等待
class LiveSource : public IDataSource {
public:
    bool Next(Bar& out) override {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [this] { return !q_.empty() || closed_; });
        if (q_.empty() && closed_) return false;
        out = q_.front();
        q_.pop();
        return true;
    }
    void Push(const Bar& b) {          // 行情线程调用
        std::lock_guard<std::mutex> lk(mu_);
        q_.push(b);
        cv_.notify_one();
    }
    void Close() {
        std::lock_guard<std::mutex> lk(mu_);
        closed_ = true;
        cv_.notify_all();
    }
private:
    std::queue<Bar> q_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool closed_ = false;
};