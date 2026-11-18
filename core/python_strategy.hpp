#pragma once
// Qt 的 slots/signals 宏与 Python 头文件的成员名冲突，必须在 include pybind11 之前 undef
#ifdef slots
#undef slots
#endif
#ifdef signals
#undef signals
#endif
#include <pybind11/embed.h>
#include <string>
#include "strategy.hpp"
#include "types.hpp"

namespace py = pybind11;

// 运行时 Python 策略：exec 用户代码，包装成 Strategy 接口。
// 用户约定：写一个 Strategy 类，含 on_bar(dict) 和 signal() -> 'buy'/'sell'/'hold'
class PythonStrategy : public Strategy {
public:
    explicit PythonStrategy(const std::string& code) {
        // 进程内单例解释器（首次构造时初始化，程序退出时销毁）
        static py::scoped_interpreter guard{};

        try {
            py::dict local_ns;              // 每个策略独立命名空间，互不覆盖 class Strategy
            py::exec(code, local_ns);       // 执行用户代码
            instance_ = local_ns["Strategy"]();   // 创建策略实例
        } catch (const py::error_already_set& e) {
            error_ = e.what();
        }
    }

    void onBar(const Bar& bar) override {
        if (!instance_) return;
        py::dict d;
        d["open"] = bar.open;
        d["high"] = bar.high;
        d["low"] = bar.low;
        d["close"] = bar.close;
        d["volume"] = bar.volume;
        try {
            instance_.attr("on_bar")(d);
        } catch (const py::error_already_set& e) {
            error_ = e.what();
        }
    }

    Signal signal() const override {
        if (!instance_) return Signal::Hold;
        try {
            std::string s = py::str(instance_.attr("signal")());
            if (s == "buy")  return Signal::Buy;
            if (s == "sell") return Signal::Sell;
            return Signal::Hold;
        } catch (const py::error_already_set&) {
            return Signal::Hold;
        }
    }

    const std::string& error() const { return error_; }

private:
    py::object instance_;
    std::string error_;
};
