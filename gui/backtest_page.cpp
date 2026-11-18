#include "backtest_page.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>
#include <QPainter>
#include <QChartView>
#include <QChart>
#include <QLineSeries>
#include <QDialog>
#include <QFormLayout>
#include <QProcess>
#include <QCoreApplication>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QLineEdit>
#include <QFileInfo>
#include <QFileDialog>
#include <QInputDialog>
#include <QListWidget>
#include <QPlainTextEdit>

#include "strategy.hpp"
#include "strategies/all.hpp"
#include "strategy_params.hpp"
#include "python_strategy.hpp"
#include "python_strategy_manager.hpp"
#include "http_client.hpp"
#include "data_mode.hpp"
#include "fx_service.hpp"
#include "engine.hpp"
#include "data_source.hpp"
#include "trade_store.hpp"
#include "performance.hpp"
#include "stock.hpp"
#include "currency.hpp"

BacktestPage::BacktestPage(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);

    // ---- 按钮栏 ----
    auto *top = new QHBoxLayout();
    run_btn_ = new QPushButton("开始回测");
    download_btn_ = new QPushButton("下载数据");
    add_btn_ = new QPushButton("添加股票");
    remove_btn_ = new QPushButton("删除选中");
    auto *py_btn = new QPushButton("Python 策略");
    auto *py_hint = new QLabel("（全局共享：回测/模拟盘通用）");
    py_hint->setStyleSheet("color: gray;");
    top->addWidget(run_btn_);
    top->addWidget(download_btn_);
    top->addWidget(add_btn_);
    top->addWidget(remove_btn_);
    top->addWidget(py_btn);
    top->addWidget(py_hint);
    top->addWidget(new QLabel("曲线币种"));
    currency_combo_ = new QComboBox();
    currency_combo_->addItems({"人民币 (CNY)", "美元 (USD)", "加元 (CAD)"});
    top->addWidget(currency_combo_);
    top->addStretch();
    root->addLayout(top);

    // ---- 策略表（勾选 | 策略 | 参数） ----
    strategy_table_ = new QTableWidget(0, 3);
    strategy_table_->setHorizontalHeaderLabels({"勾选", "策略", "参数"});
    strategy_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    strategy_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    strategy_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    strategy_table_->setMaximumHeight(120);
    reloadStrategyTable();
    root->addWidget(strategy_table_);

    // ---- 股票列表 ----
    stock_table_ = new QTableWidget(0, 4);
    stock_table_->setHorizontalHeaderLabels({"代码", "数据文件", "市场", "昨收"});
    stock_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    root->addWidget(stock_table_, 1);

    // 交易规则说明（随数据源模式动态刷新）
    rules_box_ = tradingRulesWidget(globalDataMode());
    root->addWidget(rules_box_);

    // ---- 资金曲线图 ----
    equity_view_ = new QChartView();
    equity_view_->setRenderHint(QPainter::Antialiasing);
    root->addWidget(equity_view_, 2);

    // ---- 绩效标签 ----
    metrics_label_ = new QLabel("绩效：—");
    root->addWidget(metrics_label_);

    // ---- 交易表格 ----
    trade_model_ = new QStandardItemModel(this);
    trade_model_->setHorizontalHeaderLabels({"策略", "代码", "时间", "方向", "价格", "币种", "数量", "手续费(¥)", "盈亏(¥)"});
    trade_table_ = new QTableView();
    trade_table_->setModel(trade_model_);
    root->addWidget(trade_table_, 1);

    connect(run_btn_, &QPushButton::clicked, this, &BacktestPage::onRun);
    connect(download_btn_, &QPushButton::clicked, this, &BacktestPage::onDownload);
    connect(add_btn_, &QPushButton::clicked, this, &BacktestPage::onAddStock);
    connect(remove_btn_, &QPushButton::clicked, this, &BacktestPage::onRemoveStock);
    connect(py_btn, &QPushButton::clicked, this, &BacktestPage::onPythonStrategy);
    connect(currency_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
        requestFxRefresh();   // 切换币种时当场请求一次汇率
        renderChart();
    });
}

void BacktestPage::refreshRules() {
    updateTradingRules(rules_box_, globalDataMode());
}

// 按当前选中币种，把人民币基准的曲线换算后渲染
void BacktestPage::renderChart() {
    if (last_curves_.empty()) return;   // 还没跑过回测
    Currency cur = static_cast<Currency>(currency_combo_->currentIndex());  // 0=CNY 1=USD 2=CAD

    auto* chart = new QChart();
    for (const auto& [name, curve] : last_curves_) {
        auto* line = new QLineSeries();
        line->setName(name);
        for (size_t i = 0; i < curve.size(); ++i)
            line->append(static_cast<double>(i), cnyToNative(curve[i], cur));
        chart->addSeries(line);
    }
    if (!last_baseline_.empty()) {
        auto* base = new QLineSeries();
        base->setName("买入持有基准");
        for (size_t i = 0; i < last_baseline_.size(); ++i)
            base->append(static_cast<double>(i), cnyToNative(last_baseline_[i], cur));
        chart->addSeries(base);
    }
    chart->createDefaultAxes();
    chart->setTitle(QString("资金曲线对比（%1）").arg(currency_combo_->currentText()));
    equity_view_->setChart(chart);
}

void BacktestPage::onAddStock() {
    QString f = QFileDialog::getOpenFileName(this, "选择数据文件", "", "CSV (*.csv)");
    if (f.isEmpty()) return;

    QFileInfo fi(f);
    QString base = fi.completeBaseName();   // "sh600000-5m" / "AAPL-d"
    QString code = base.split('-').first(); // "sh600000" / "AAPL"

    // 国内从文件名推导带点代码：sh600000 → sh.600000
    if (globalDataMode() == DataMode::China && (code.startsWith("sh") || code.startsWith("sz")))
        code.insert(2, '.');

    addStockRow(code, f);
}

void BacktestPage::onRemoveStock()
{
    int r = stock_table_->currentRow();
    if (r >= 0)
        stock_table_->removeRow(r);
}

void BacktestPage::addStockRow(const QString &symbol, const QString &file)
{
    int r = stock_table_->rowCount();
    stock_table_->insertRow(r);
    stock_table_->setItem(r, 0, new QTableWidgetItem(symbol));
    stock_table_->setItem(r, 1, new QTableWidgetItem(file));
    stock_table_->setItem(r, 2, new QTableWidgetItem(marketName(marketOf(symbol.toStdString()))));
    stock_table_->setItem(r, 3, new QTableWidgetItem("0"));  // 昨收（0=用首根开盘价）
}

void BacktestPage::onRun()
{
    if (stock_table_->rowCount() == 0)
    {
        metrics_label_->setText("请先添加股票");
        return;
    }

    // 1. 加载股票数据（一次，所有策略复用）；市场规则按代码自动判定
    std::vector<StockInfo> infos;
    std::vector<std::vector<Bar>> all_bars;
    for (int r = 0; r < stock_table_->rowCount(); ++r)
    {
        std::string sym = stock_table_->item(r, 0)->text().toStdString();
        std::string file = stock_table_->item(r, 1)->text().toStdString();
        double prev_close = stock_table_->item(r, 3)->text().toDouble();

        auto bars = LoadBars(file);
        if (bars.empty())
            continue;
        StockInfo info = makeStockInfo(sym, prev_close > 0 ? prev_close : bars.front().open);
        infos.push_back(info);
        all_bars.push_back(bars);
    }
    if (infos.empty())
    {
        metrics_label_->setText("没有有效数据");
        return;
    }

    // 2. 收集勾选的策略（C++ 和 Python，创建好实例）
    std::vector<std::pair<QString, std::shared_ptr<Strategy>>> selections;
    for (int r = 0; r < strategy_table_->rowCount(); ++r)
    {
        auto *check = strategy_table_->item(r, 0);
        if (check->checkState() != Qt::Checked)
            continue;
        QString name = strategy_table_->item(r, 1)->text();
        auto strat = createStrategyFromRow(param_rows_[r], name);
        if (strat) selections.push_back({name, strat});
    }
    if (selections.empty())
    {
        metrics_label_->setText("请勾选至少一个策略");
        return;
    }

    // 3. 对每个策略跑回测（曲线以人民币基准存下，按选中币种渲染）
    last_curves_.clear();
    last_baseline_.clear();
    trade_model_->removeRows(0, trade_model_->rowCount());
    QString summary;

    for (const auto &[sname, strat] : selections)
    {
        auto store = std::make_shared<MemoryTradeStore>();
        Engine eng(store, 0.01);
        for (size_t i = 0; i < infos.size(); ++i)
            eng.AddStock(infos[i], all_bars[i], strat);
        eng.run();

        last_curves_.push_back({sname, eng.equityCurve()});

        if (last_baseline_.empty())
            last_baseline_ = eng.baseline();

        auto perf = ComputePerformance(eng.equityCurve(), 11520.0, eng.tradePnl());
        summary += QString("%1: %2%  |  ").arg(sname).arg(perf.total_return * 100, 0, 'f', 2);

        for (const auto &t : store->LoadAll())
        {
            auto time = QDateTime::fromMSecsSinceEpoch((qint64)t.ts).toString("MM-dd hh:mm");
            QList<QStandardItem *> row;
            row << new QStandardItem(sname)
                << new QStandardItem(QString::fromStdString(t.symbol))
                << new QStandardItem(time)
                << new QStandardItem(t.qty > 0 ? "买" : "卖")
                << new QStandardItem(QString::number(t.price, 'f', 2))
                << new QStandardItem(QString::fromStdString(t.currency))
                << new QStandardItem(QString::number(std::abs(t.qty), 'f', 0))
                << new QStandardItem(QString::number(t.fee, 'f', 2))
                << new QStandardItem(QString::number(t.pnl, 'f', 2));
            trade_model_->appendRow(row);
        }
    }

    // 基准线（所有策略共用，与策略无关）
    renderChart();

    metrics_label_->setText(summary);
}

void BacktestPage::onDownload()
{
    auto *dlg = new QDialog(this);
    dlg->setWindowTitle("批量下载行情数据（全局统一时间）");
    auto *form = new QFormLayout(dlg);

    // 全局统一时间（所有股票共用）
    auto *freq_combo = new QComboBox();
    freq_combo->addItems({"5", "15", "30", "60", "d"});
    auto *start_edit = new QLineEdit("2025-01-01");
    auto *end_edit = new QLineEdit("2025-12-31");

    // 股票列表
    auto *stock_list = new QListWidget();
    stock_list->setFixedHeight(100);
    auto *add_btn = new QPushButton("添加股票");
    auto *remove_btn = new QPushButton("删除选中");

    auto *status_lbl = new QLabel("就绪");
    auto *btn = new QPushButton("批量下载");

    form->addRow("周期", freq_combo);
    form->addRow("开始", start_edit);
    form->addRow("结束", end_edit);
    form->addRow("股票列表", stock_list);
    auto *list_btns = new QHBoxLayout();
    list_btns->addWidget(add_btn);
    list_btns->addWidget(remove_btn);
    list_btns->addStretch();
    form->addRow(list_btns);
    form->addRow("状态", status_lbl);
    form->addRow(btn);

    // 数据文件命名（与 downloadAndSaveKline 一致）
    auto dataFileFor = [](const QString& code, const QString& freq) {
        QString suffix = (freq == "d") ? "d" : freq + "m";
        if (globalDataMode() == DataMode::China) {
            QString c = code; c.remove('.');
            return QString("data/%1-%2.csv").arg(c, suffix);
        }
        return QString("data/%1-%2.csv").arg(toMarketSymbol(code), suffix);
    };

    // 添加股票：弹输入框（提示随数据源切换）
    connect(add_btn, &QPushButton::clicked, [stock_list]() {
        bool ok = false;
        QString hint = (globalDataMode() == DataMode::China)
            ? "股票代码（如 sh.600000）:"
            : "股票代码（如 AAPL / SHOP.TO）:";
        QString code = QInputDialog::getText(nullptr, "添加股票", hint,
                                             QLineEdit::Normal, "", &ok);
        if (ok && !code.trimmed().isEmpty())
            stock_list->addItem(code.trimmed());
    });

    // 删除选中
    connect(remove_btn, &QPushButton::clicked, [stock_list]() {
        delete stock_list->takeItem(stock_list->currentRow());
    });

    // 批量下载：按数据源分发（东财免费接口 / Twelve Data），并发异步
    connect(btn, &QPushButton::clicked, [=, this]() {
        if (stock_list->count() == 0) {
            status_lbl->setText("请先添加股票");
            return;
        }
        QString freq = freq_combo->currentText();
        QString start = start_edit->text();
        QString end = end_edit->text();

        // 1. 收集需要下载的股票（已存在的跳过）
        QStringList codes;
        int skipped = 0;
        for (int i = 0; i < stock_list->count(); ++i) {
            QString code = stock_list->item(i)->text().trimmed();
            QString file = dataFileFor(code, freq);
            if (QFileInfo::exists(file)) {
                addStockRow(code, file);
                ++skipped;
                continue;
            }
            codes << code;
        }
        if (codes.isEmpty()) {
            status_lbl->setText(QString("全部已存在，跳过 %1 只").arg(skipped));
            return;
        }

        // 2. 并发下载（一个 HttpClient 同时发多个请求，全异步）
        status_lbl->setText(QString("并发下载 %1 只...").arg(codes.size()));
        auto* client = new HttpClient(this);
        struct State { int done = 0; int ok = 0; };
        auto state = std::make_shared<State>();
        for (int i = 0; i < codes.size(); ++i) {
            downloadAndSaveKline(client, codes[i], freq, start, end,
                [this, codes, status_lbl, skipped, state, i](bool ok, const QString& file) {
                if (ok) { addStockRow(codes[i], file); state->ok++; }
                if (++(state->done) == (int)codes.size()) {
                    status_lbl->setText(QString("下载完成 %1 只，跳过 %2 只").arg(state->ok).arg(skipped));
                }
            });
        }
    });

    dlg->exec();
}

void BacktestPage::onPythonStrategy() {
    openPythonStrategyDialog(this, [this](const QString& code) {
        runPythonBacktest(code);
    }, [this]() {
        reloadStrategyTable();
    });
}

void BacktestPage::runPythonBacktest(const QString& code) {
    auto strat = std::make_shared<PythonStrategy>(code.toStdString());
    if (!strat->error().empty()) {
        metrics_label_->setText("Python 错误：" + QString::fromStdString(strat->error()));
        return;
    }

    std::vector<StockInfo> infos;
    std::vector<std::vector<Bar>> all_bars;
    for (int r = 0; r < stock_table_->rowCount(); ++r) {
        std::string sym = stock_table_->item(r, 0)->text().toStdString();
        std::string file = stock_table_->item(r, 1)->text().toStdString();
        double prev_close = stock_table_->item(r, 3)->text().toDouble();
        auto bars = LoadBars(file);
        if (bars.empty()) continue;
        StockInfo info = makeStockInfo(sym, prev_close > 0 ? prev_close : bars.front().open);
        infos.push_back(info);
        all_bars.push_back(bars);
    }
    if (infos.empty()) { metrics_label_->setText("没有有效数据"); return; }

    auto store = std::make_shared<MemoryTradeStore>();
    Engine eng(store, 0.01);
    for (size_t i = 0; i < infos.size(); ++i)
        eng.AddStock(infos[i], all_bars[i], strat);
    eng.run();

    last_curves_.clear();
    last_curves_.push_back({"Python策略", eng.equityCurve()});
    last_baseline_ = eng.baseline();
    renderChart();

    auto perf = ComputePerformance(eng.equityCurve(), 11520.0, eng.tradePnl());
    metrics_label_->setText(
        QString("总收益 %1% | 回撤 %2% | 夏普 %3 | 交易 %4 次")
            .arg(perf.total_return * 100, 0, 'f', 2)
            .arg(perf.max_drawdown * 100, 0, 'f', 2)
            .arg(perf.sharpe, 0, 'f', 2)
            .arg(perf.trades));

    trade_model_->removeRows(0, trade_model_->rowCount());
    for (const auto& t : store->LoadAll()) {
        auto time = QDateTime::fromMSecsSinceEpoch((qint64)t.ts).toString("MM-dd hh:mm");
        QList<QStandardItem*> row;
        row << new QStandardItem("Python")
            << new QStandardItem(QString::fromStdString(t.symbol))
            << new QStandardItem(time)
            << new QStandardItem(t.qty > 0 ? "买" : "卖")
            << new QStandardItem(QString::number(t.price, 'f', 2))
            << new QStandardItem(QString::fromStdString(t.currency))
            << new QStandardItem(QString::number(std::abs(t.qty), 'f', 0))
            << new QStandardItem(QString::number(t.fee, 'f', 2))
            << new QStandardItem(QString::number(t.pnl, 'f', 2));
        trade_model_->appendRow(row);
    }
}

void BacktestPage::reloadStrategyTable() {
    populateStrategyTable(strategy_table_, param_rows_);
}
