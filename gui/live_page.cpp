#include "live_page.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QChartView>
#include <QChart>
#include <QLineSeries>
#include <QPainter>
#include "strategies/all.hpp"
#include "strategy_params.hpp"
#include "python_strategy_manager.hpp"
#include "data_mode.hpp"
#include "stock.hpp"
#include "currency.hpp"
#include "types.hpp"

LivePage::LivePage(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);

    // 控制栏：代码 + K线周期 + 开始/停止
    auto* top = new QHBoxLayout();
    symbol_edit_ = new QLineEdit(globalDataMode() == DataMode::China ? "sh600000" : "AAPL");
    interval_combo_ = new QComboBox();
    interval_combo_->addItems({"1分钟", "5分钟", "15分钟", "30分钟", "60分钟"});
    interval_combo_->setCurrentIndex(0);
    start_btn_ = new QPushButton("开始");
    stop_btn_ = new QPushButton("停止");
    stop_btn_->setEnabled(false);
    top->addWidget(new QLabel("代码"));
    top->addWidget(symbol_edit_);
    top->addWidget(new QLabel("K线"));
    top->addWidget(interval_combo_);
    top->addWidget(start_btn_);
    top->addWidget(stop_btn_);
    auto* py_btn = new QPushButton("Python 策略");
    auto* py_hint = new QLabel("（全局共享：回测/模拟盘通用）");
    py_hint->setStyleSheet("color: gray;");
    top->addWidget(py_btn);
    top->addWidget(py_hint);
    top->addStretch();
    root->addLayout(top);

    // 行情
    quote_label_ = new QLabel("行情：—");
    root->addWidget(quote_label_);

    // 交易规则说明（随数据源模式动态刷新）
    rules_box_ = tradingRulesWidget(globalDataMode());
    root->addWidget(rules_box_);

    // 策略表（勾选 | 策略 | 参数）
    strategy_table_ = new QTableWidget(0, 3);
    strategy_table_->setHorizontalHeaderLabels({"勾选", "策略", "参数"});
    strategy_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    strategy_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    strategy_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    strategy_table_->setMaximumHeight(120);
    populateStrategyTable(strategy_table_, param_rows_);
    root->addWidget(strategy_table_);

    // 手动交易
    auto* mt = new QHBoxLayout();
    mt->addWidget(new QLabel("手动交易"));
    auto* mb = new QPushButton("市价买");
    auto* ms = new QPushButton("市价卖");
    auto* lb = new QPushButton("限价买");
    auto* ls = new QPushButton("限价卖");
    auto* cx = new QPushButton("撤单");
    mt->addWidget(mb); mt->addWidget(ms); mt->addWidget(lb); mt->addWidget(ls); mt->addWidget(cx);
    mt->addStretch();
    root->addLayout(mt);

    manual_label_ = new QLabel("手动账户（人民币）：现金 ¥1,000,000 | 持仓 0 股");
    root->addWidget(manual_label_);

    orders_table_ = new QTableWidget(0, 4);
    orders_table_->setHorizontalHeaderLabels({"订单ID", "方向", "价格", "剩余数量"});
    orders_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    orders_table_->setMaximumHeight(100);
    root->addWidget(orders_table_);

    // 资金曲线图
    chart_view_ = new QChartView();
    chart_view_->setRenderHint(QPainter::Antialiasing);
    root->addWidget(chart_view_, 2);

    // 汇总
    summary_label_ = new QLabel("各策略收益：—");
    root->addWidget(summary_label_);

    duration_label_ = new QLabel("时长：—");
    root->addWidget(duration_label_);

    src_ = new LiveQuoteSource(this);
    connect(src_, &LiveQuoteSource::quoteUpdated, this, &LivePage::onQuote);
    connect(src_, &LiveQuoteSource::errorOccurred, this, [this](const QString& e) {
        quote_label_->setText("行情错误：" + e);
    });
    connect(start_btn_, &QPushButton::clicked, this, &LivePage::onStart);
    connect(stop_btn_, &QPushButton::clicked, this, &LivePage::onStop);
    connect(interval_combo_, &QComboBox::currentIndexChanged, this, &LivePage::onIntervalChanged);

    // Python 策略：编辑/保存/导入，同步到策略表（全局共享）
    connect(py_btn, &QPushButton::clicked, this, [this]() {
        openPythonStrategyDialog(this, [this](const QString&) {
            // 模拟盘的运行由「开始」按钮触发
        }, [this]() {
            populateStrategyTable(strategy_table_, param_rows_);
        });
    });

    // 手动交易
    connect(mb, &QPushButton::clicked, this, [this]() {
        auto v = promptInputs("市价买入", {"数量（股）"});
        if (v.size() < 1 || v[0].isEmpty()) return;
        applyManualFills(book_.matchMarketBuy(v[0].toInt()));
    });
    connect(ms, &QPushButton::clicked, this, [this]() {
        auto v = promptInputs("市价卖出", {"数量（股）"});
        if (v.size() < 1 || v[0].isEmpty()) return;
        applyManualFills(book_.matchMarketSell(v[0].toInt()));
    });
    connect(lb, &QPushButton::clicked, this, [this]() {
        auto v = promptInputs("限价买入", {"价格", "数量（股）"});
        if (v.size() < 2 || v[0].isEmpty() || v[1].isEmpty()) return;
        uint64_t id = 0;
        applyManualFills(book_.addLimitBuy(v[0].toDouble(), v[1].toInt(), &id));
    });
    connect(ls, &QPushButton::clicked, this, [this]() {
        auto v = promptInputs("限价卖出", {"价格", "数量（股）"});
        if (v.size() < 2 || v[0].isEmpty() || v[1].isEmpty()) return;
        uint64_t id = 0;
        applyManualFills(book_.addLimitSell(v[0].toDouble(), v[1].toInt(), &id));
    });
    connect(cx, &QPushButton::clicked, this, [this]() {
        auto v = promptInputs("撤单", {"订单ID"});
        if (v.size() < 1 || v[0].isEmpty()) return;
        book_.cancelById(v[0].toULongLong());
        refreshOrdersTable();
    });

    // 时长定时刷新
    auto* dur_timer = new QTimer(this);
    dur_timer->setInterval(1000);
    connect(dur_timer, &QTimer::timeout, this, [this]() {
        if (!timer_.isValid()) { duration_label_->setText("时长：—"); return; }
        qint64 ms = timer_.elapsed();
        duration_label_->setText(QString("已运行 %1分%2秒 | 已生成 %3 根K线")
            .arg(ms / 60000).arg((ms / 1000) % 60, 2, 10, QChar('0')).arg(bars_));
    });
    dur_timer->start();
}

void LivePage::refreshRules() {
    updateTradingRules(rules_box_, globalDataMode());
}

void LivePage::onIntervalChanged(int) {
    static const int ms[] = {60000, 300000, 900000, 1800000, 3600000};
    interval_ms_ = ms[interval_combo_->currentIndex()];
}

void LivePage::onStart() {
    traders_.clear();
    for (int r = 0; r < strategy_table_->rowCount(); ++r) {
        auto* check = strategy_table_->item(r, 0);
        if (check->checkState() != Qt::Checked) continue;
        QString name = strategy_table_->item(r, 1)->text();
        auto strat = createStrategyFromRow(param_rows_[r], name);
        if (!strat) continue;
        PaperTrader t;
        t.name = name.toStdString();
        t.strat = strat;
        traders_.push_back(std::move(t));
    }
    // 重置 K线聚合 + 市场规则（按代码自动判定）
    current_bar_ = Bar{};
    current_bucket_ = 0;
    has_bar_ = false;
    bars_ = 0;
    timer_.start();
    StockInfo info = makeStockInfo(symbol_edit_->text().toStdString());
    market_ = info.market;
    price_limit_ = info.price_limit;
    lot_size_ = info.lot_size;
    t_plus_1_ = info.t_plus_1;
    onIntervalChanged(interval_combo_->currentIndex());
    src_->setSymbol(symbol_edit_->text());
    src_->start();
    start_btn_->setEnabled(false);
    stop_btn_->setEnabled(true);
}

void LivePage::onStop() {
    src_->stop();
    start_btn_->setEnabled(true);
    stop_btn_->setEnabled(false);
}

void LivePage::onQuote(const Quote& q) {
    last_price_ = q.last;
    last_pre_close_ = q.pre_close;
    if (!book_initialized_) {
        book_.applyQuote(q);
        book_initialized_ = true;
    }
    double chg = q.last - q.pre_close;
    double pct = q.pre_close > 0 ? chg / q.pre_close * 100 : 0;
    quote_label_->setText(
        QString("现价 %1  涨跌 %2 (%3%)  |  今开 %4  昨收 %5  |  买一 %6  卖一 %7")
            .arg(q.last).arg(chg, 0, 'f', 2).arg(pct, 0, 'f', 2)
            .arg(q.open).arg(q.pre_close).arg(q.bid_p[0]).arg(q.ask_p[0]));

    // K线聚合
    uint64_t bucket = q.ts / interval_ms_;
    if (has_bar_ && bucket != current_bucket_) {
        feedBar(current_bar_);
        current_bar_ = Bar{};
        has_bar_ = false;
    }
    if (!has_bar_) {
        current_bar_.ts = bucket * interval_ms_;
        current_bar_.open = q.last;
        current_bar_.high = q.last;
        current_bar_.low = q.last;
        current_bar_.volume = q.volume;
        has_bar_ = true;
    } else {
        current_bar_.high = std::max(current_bar_.high, q.last);
        current_bar_.low = std::min(current_bar_.low, q.last);
        current_bar_.volume += q.volume;
    }
    current_bar_.close = q.last;
    current_bucket_ = bucket;
}

void LivePage::feedBar(const Bar& bar) {
    double limit_up = (price_limit_ > 0 && last_pre_close_ > 0) ? last_pre_close_ * (1 + price_limit_) : 0;
    double limit_down = (price_limit_ > 0 && last_pre_close_ > 0) ? last_pre_close_ * (1 - price_limit_) : 0;
    uint64_t day = dayOf(bar.ts);
    std::string sym = symbol_edit_->text().toStdString();
    Currency cur = currencyOf(market_);

    for (auto& t : traders_) {
        t.strat->onBar(bar);
        Signal sig = t.strat->signal();

        if (sig == Signal::Buy && t.position == 0) {
            if (limit_up > 0 && last_price_ > limit_up) continue;   // 涨停买不进（仅A股）
            double native_cash = cnyToNative(t.cash, cur);          // 人民币 → 外币
            int qty = (int)(native_cash / (last_price_ * 1.001)) / lot_size_ * lot_size_;
            if (qty >= lot_size_) {
                double notional = last_price_ * qty;                 // 外币
                double fee = calcFee(notional, qty, true, sym);      // 外币
                t.cash -= nativeToCny(notional + fee, cur);          // 扣人民币
                t.position = qty;
                t.buy_day = day;
            }
        } else if (sig == Signal::Sell && t.position > 0) {
            if (t_plus_1_ && t.buy_day >= day) continue;             // T+1（仅A股）
            if (limit_down > 0 && last_price_ < limit_down) continue; // 跌停卖不出（仅A股）
            double notional = last_price_ * t.position;              // 外币
            double fee = calcFee(notional, t.position, false, sym);  // 外币
            t.cash += nativeToCny(notional - fee, cur);              // 加人民币
            t.position = 0;
        }
        t.equity.push_back(t.cash + nativeToCny(t.position * last_price_, cur));
    }
    bars_++;
    updateChart();
}

void LivePage::updateChart() {
    auto* chart = new QChart();
    for (auto& t : traders_) {
        auto* line = new QLineSeries();
        line->setName(QString::fromStdString(t.name));
        for (size_t i = 0; i < t.equity.size(); ++i)
            line->append((double)i, t.equity[i]);
        chart->addSeries(line);
    }
    chart->createDefaultAxes();
    chart->setTitle(QString("多策略资金曲线对比（实时 K线 · %1 · 人民币）")
                        .arg(QString::fromLatin1(marketName(market_))));
    chart_view_->setChart(chart);

    QString s;
    for (auto& t : traders_) {
        double ret = t.equity.empty() ? 0 : (t.equity.back() / 1'000'000 - 1) * 100;
        s += QString("%1: %2%   ").arg(QString::fromStdString(t.name)).arg(ret, 0, 'f', 2);
    }
    summary_label_->setText(s);
}

void LivePage::applyManualFills(const std::vector<Fill>& fills) {
    if (fills.empty()) return;
    std::string sym = symbol_edit_->text().toStdString();
    Currency cur = currencyOf(market_);
    bool is_buy = fills.front().qty > 0;
    auto [qty, notional] = aggregateFills(fills);   // 整单聚合
    double fee = calcFee(notional, qty, is_buy, sym);   // 外币，按整单收

    if (is_buy) {
        manual_cash_ -= nativeToCny(notional + fee, cur);
        manual_position_ += (int)qty;
    } else {
        manual_cash_ += nativeToCny(notional - fee, cur);
        manual_position_ -= (int)qty;
    }
    updateManualLabel();
    refreshOrdersTable();
}

void LivePage::updateManualLabel() {
    double market = manual_position_ * last_price_;
    Currency cur = currencyOf(market_);
    double market_cny = nativeToCny(market, cur);
    manual_label_->setText(QString("手动账户（人民币）：现金 ¥%1 | 持仓 %2 股 | 市值 ¥%3")
        .arg(manual_cash_, 0, 'f', 2).arg(manual_position_).arg(market_cny, 0, 'f', 2));
}

void LivePage::refreshOrdersTable() {
    orders_table_->setRowCount(0);
    for (const auto& o : book_.allOrders()) {
        int r = orders_table_->rowCount();
        orders_table_->insertRow(r);
        orders_table_->setItem(r, 0, new QTableWidgetItem(QString::number(o.id)));
        orders_table_->setItem(r, 1, new QTableWidgetItem(o.is_buy ? "买" : "卖"));
        orders_table_->setItem(r, 2, new QTableWidgetItem(QString::number(o.price, 'f', 2)));
        orders_table_->setItem(r, 3, new QTableWidgetItem(QString::number(o.qty, 'f', 0)));
    }
}
