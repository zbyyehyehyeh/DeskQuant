# desk-quant

> 面向学习与实践的量化回测平台：C++20 回测引擎 + order book 撮合 + Qt 界面。
> **单一版本，同时支持国内 A股 / 美股 / 加股**，通过顶部「数据源」下拉框切换，不再区分国内版/国外版。

## 功能
- 回测引擎：事件驱动，信号用收盘价、成交用下一根开盘价（规避未来函数）
- order book：五档撮合（市价/限价/撤单）
- 多股票：按时间归并 + 共享现金 + 独立策略实例
- 数据源切换：国内（东财历史 + 腾讯实时）/ 国外（Twelve Data，需要 apikey）
- 市场规则按股票代码自动判定：
  - **A股**：涨跌停（主板 ±10%，创业板/科创板 ±20%）、T+1、100 股整数倍
  - **美股**：无涨跌停、T+0、1 股起、零佣金
  - **加股**：无涨跌停、T+0、1 股起、Questrade 佣金（按整单收：C$0.01/股，最低 C$4.95，最高 C$9.95，另加 ECN C$0.0035/股）
- **计价**：账户/盈亏/手续费统一以**人民币**为基准；美股/加股按**汇率**换算回人民币（汇率源：新浪实时优先，Twelve Data / open.er-api 兜底；详见 `docs/汇率与币种方案.md`）
- **曲线币种切换**：回测页顶部「曲线币种」下拉框，可选人民币/美元/加元，切换即按对应币种重绘策略曲线和买入持有基准
- **上传策略**：Python 策略对话框里的「上传到服务器」按钮，列出所有自定义策略勾选批量上传（上传前做代码安全校验）
- 绩效：收益/年化/回撤/夏普/胜率/盈亏比
- 策略：ma_cross / rsi / bollinger / breakout / buy_hold + 工厂注册 + Python 策略（pybind11）

## 市场与币种判定（core/stock.hpp 的 marketOf）
- 加股（CAD）：符号以 `.TO` / `.V` / `.CA` 结尾
- A股（CNY）：符号以 `sh`/`sz` 开头，或 6 位纯数字
- 美股（USD）：其余默认

## 构建
```bash
# MSYS2 UCRT64 环境
cmake -B build -G "Unix Makefiles"
cmake --build build -j$(nproc)
./build/deskquant.exe
```

## 数据源与计价
- **国内**：免费接口（东财历史 K线、腾讯实时五档），无需任何 key，全部人民币计价。
- **国外**：Twelve Data（`https://twelvedata.com` 免费注册，800 次/天、8 次/分钟）。
  首次使用时在顶部「Twelve Data API Key」输入框粘贴 key，自动保存到 `apikey.txt`。
  美股/加股价格按汇率换算回人民币，汇率源：新浪（实时）→ Twelve Data → open.er-api 兜底。

## 目录
```
core/  纯 C++ 引擎（stock/currency/engine/order_book/策略/撮合/绩效，header-only）
gui/   Qt 界面（回测页/模拟盘页/行情源/汇率服务/HTTP 客户端/数据源模式）
```
