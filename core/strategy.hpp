#pragma once
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include "types.hpp"

enum class Signal { Hold, Buy, Sell };

// 策略接口：用户只写这两个函数
class Strategy {
public:
    virtual ~Strategy() = default;
    virtual void onBar(const Bar& bar) = 0;
    virtual Signal signal() const = 0;
};

// 策略工厂：按名字注册/创建
class StrategyFactory {
public:
    using Creator = std::function<std::shared_ptr<Strategy>()>;
    static StrategyFactory& instance() {
        static StrategyFactory f;
        return f;
    }
    void registerStrategy(const std::string& name, Creator c) { creators_[name] = std::move(c); }
    std::shared_ptr<Strategy> create(const std::string& name) const {
        auto it = creators_.find(name);
        return it != creators_.end() ? it->second() : nullptr;
    }
    std::vector<std::string> names() const {
        std::vector<std::string> v;
        for (auto& [k, _] : creators_) v.push_back(k);
        return v;
    }
private:
    std::map<std::string, Creator> creators_;
};

#define REGISTER_STRATEGY(name, type) \
    static bool _reg_##type = []() { \
        StrategyFactory::instance().registerStrategy(name, []() { return std::make_shared<type>(); }); \
        return true; \
    }()

// 末尾留一个空行，消除 backslash-newline 警告