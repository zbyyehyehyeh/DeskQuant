# desk-quant

> 面向学习与实践的量化回测平台：C++20 回测引擎 + order book 撮合 + Qt 界面。

## 功能
- 回测引擎：事件驱动，信号用收盘价、成交用下一根开盘价（规避未来函数）
- order book：五档撮合（市价/限价单）
- 多股票：按时间归并 + 共享现金 + 独立策略实例
- A股约束：涨跌停、T+1、100 股整数倍
- 绩效：收益/年化/回撤/夏普/胜率/盈亏比
- 策略：ma_cross / rsi / bollinger / breakout / buy_hold + 工厂注册
- 数据：baostock（A股）

## 构建
# MSYS2 UCRT64 环境
cmake -B build -G "Unix Makefiles"
cmake --build build -j$(nproc)
./build/deskquant.exe