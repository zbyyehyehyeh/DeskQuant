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
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QLineEdit>
#include <QFileInfo>
#include <QFileDialog>   

#include "strategy.hpp"
#include "strategies/all.hpp"
#include "engine.hpp"
#include "data_source.hpp"
#include "trade_store.hpp"
#include "performance.hpp"
#include "stock.hpp"

BacktestPage::BacktestPage(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);

    // ---- 顶部控制栏 ----
    auto *top = new QHBoxLayout();
    strategy_combo_ = new QComboBox();
    for (auto &name : StrategyFactory::instance().names())
        strategy_combo_->addItem(QString::fromStdString(name));
    run_btn_ = new QPushButton("开始回测");
    download_btn_ = new QPushButton("下载数据");
    add_btn_ = new QPushButton("添加股票");
    remove_btn_ = new QPushButton("删除选中");
    top->addWidget(strategy_combo_);
    top->addWidget(run_btn_);
    top->addWidget(download_btn_);
    top->addWidget(add_btn_);
    top->addWidget(remove_btn_);
    root->addLayout(top);

    // ---- 股票列表 ----
    stock_table_ = new QTableWidget(0, 4);
    stock_table_->setHorizontalHeaderLabels({"代码", "数据文件", "涨跌停%", "昨收"});
    stock_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    root->addWidget(stock_table_, 1);

    // ---- 资金曲线图 ----
    equity_view_ = new QChartView();
    equity_view_->setRenderHint(QPainter::Antialiasing);
    root->addWidget(equity_view_, 2);

    // ---- 绩效标签 ----
    metrics_label_ = new QLabel("绩效：—");
    root->addWidget(metrics_label_);

    // ---- 交易表格 ----
    trade_model_ = new QStandardItemModel(this);
    trade_model_->setHorizontalHeaderLabels({"代码", "时间", "方向", "价格", "数量", "手续费", "盈亏"});
    trade_table_ = new QTableView();
    trade_table_->setModel(trade_model_);
    root->addWidget(trade_table_, 1);

    connect(run_btn_, &QPushButton::clicked, this, &BacktestPage::onRun);
    connect(download_btn_, &QPushButton::clicked, this, &BacktestPage::onDownload);
    connect(add_btn_, &QPushButton::clicked, this, &BacktestPage::onAddStock);
    connect(remove_btn_, &QPushButton::clicked, this, &BacktestPage::onRemoveStock);
}

void BacktestPage::onAddStock() {
    QString f = QFileDialog::getOpenFileName(this, "选择数据文件", "", "CSV (*.csv)");
    if (f.isEmpty()) return;

    // 从文件名推导代码：sh600000-5m.csv → sh.600000
    QFileInfo fi(f);
    QString code = fi.completeBaseName().split('-').first();   // "sh600000"
    if (code.startsWith("sh") || code.startsWith("sz"))
        code.insert(2, '.');                                   // "sh.600000"

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
    stock_table_->setItem(r, 2, new QTableWidgetItem("10")); // 涨跌停%
    stock_table_->setItem(r, 3, new QTableWidgetItem("0"));  // 昨收（0=用首根开盘价）
}

void BacktestPage::onRun()
{
    if (stock_table_->rowCount() == 0)
    {
        metrics_label_->setText("请先添加股票");
        return;
    }
    QString strat_name = strategy_combo_->currentText();

    auto store = std::make_shared<MemoryTradeStore>();
    Engine eng(store, 0.00025, 0.001, 0.01); // 多股票引擎：先构造，再 AddStock

    std::vector<std::pair<StockInfo, std::vector<Bar>>> stocks;
    for (int r = 0; r < stock_table_->rowCount(); ++r)
    {
        std::string sym = stock_table_->item(r, 0)->text().toStdString();
        std::string file = stock_table_->item(r, 1)->text().toStdString();
        double limit = stock_table_->item(r, 2)->text().toDouble() / 100.0;
        double prev_close = stock_table_->item(r, 3)->text().toDouble();

        auto bars = LoadBars(file);
        if (bars.empty())
            continue;
        StockInfo info{sym, limit, 100, true, prev_close > 0 ? prev_close : bars.front().open};
        stocks.push_back({info, bars});
        eng.AddStock(info, bars, StrategyFactory::instance().create(strat_name.toStdString()));
    }
    if (stocks.empty())
    {
        metrics_label_->setText("没有有效数据");
        return;
    }

    eng.run();
    auto perf = ComputePerformance(eng.equityCurve(), 11520.0, eng.tradePnl());
    auto trades = store->LoadAll();

    // ---- 资金曲线（合并）+ 基准 ----
    auto *equity_chart = new QChart();
    auto *strat_line = new QLineSeries();
    strat_line->setName("策略");
    for (size_t i = 0; i < eng.equityCurve().size(); ++i)
        strat_line->append((double)i, eng.equityCurve()[i]);

    // 基准：用第一只股票做"买入持有"（简化）
    // 删掉原来的"第一只股票算基准"那段，改成：
    auto *base_line = new QLineSeries();
    base_line->setName("买入持有基准");
    for (size_t i = 0; i < eng.baseline().size(); ++i)
        base_line->append((double)i, eng.baseline()[i]);

    equity_chart->addSeries(strat_line);
    equity_chart->addSeries(base_line);
    equity_chart->createDefaultAxes();
    equity_chart->setTitle("资金曲线对比（多股票合并）");
    equity_view_->setChart(equity_chart);

    // ---- 绩效 ----
    metrics_label_->setText(
        QString("总收益 %1% | 年化 %2% | 回撤 %3% | 夏普 %4 | 交易 %5 次 | 胜率 %6%")
            .arg(perf.total_return * 100, 0, 'f', 2)
            .arg(perf.annualized * 100, 0, 'f', 2)
            .arg(perf.max_drawdown * 100, 0, 'f', 2)
            .arg(perf.sharpe, 0, 'f', 2)
            .arg(perf.trades)
            .arg(perf.win_rate * 100, 0, 'f', 2));

    // ---- 交易表格（加代码列） ----
    trade_model_->removeRows(0, trade_model_->rowCount());
    for (const auto &t : trades)
    {
        auto time = QDateTime::fromMSecsSinceEpoch((qint64)t.ts).toString("MM-dd hh:mm");
        QList<QStandardItem *> row;
        row << new QStandardItem(QString::fromStdString(t.symbol))
            << new QStandardItem(time)
            << new QStandardItem(t.qty > 0 ? "买" : "卖")
            << new QStandardItem(QString::number(t.price, 'f', 2))
            << new QStandardItem(QString::number(std::abs(t.qty), 'f', 0))
            << new QStandardItem(QString::number(t.fee, 'f', 2))
            << new QStandardItem(QString::number(t.pnl, 'f', 2));
        trade_model_->appendRow(row);
    }
}

void BacktestPage::onDownload()
{
    auto *dlg = new QDialog(this);
    dlg->setWindowTitle("下载行情数据");
    auto *form = new QFormLayout(dlg);

    auto *sym_edit = new QLineEdit("sh.600000");
    auto *freq_combo = new QComboBox();
    freq_combo->addItems({"5", "15", "30", "60", "d"});
    auto *start_edit = new QLineEdit("2025-01-01");
    auto *end_edit = new QLineEdit("2025-12-31");
    auto *status_lbl = new QLabel("就绪");
    auto *btn = new QPushButton("下载");

    form->addRow("代码", sym_edit);
    form->addRow("周期", freq_combo);
    form->addRow("开始", start_edit);
    form->addRow("结束", end_edit);
    form->addRow("状态", status_lbl);
    form->addRow(btn);

    connect(btn, &QPushButton::clicked, [=, this]()
            {
                QStringList args;
                args << "fetch_baostock.py"
                     << sym_edit->text()
                     << freq_combo->currentText()
                     << start_edit->text()
                     << end_edit->text();

                auto *proc = new QProcess(this);
                status_lbl->setText("下载中...");
                btn->setEnabled(false);

                connect(proc, &QProcess::finished, [=, this](int code, QProcess::ExitStatus)
                        {
            btn->setEnabled(true);
            if (code == 0) {
                status_lbl->setText("下载完成");
                QString symbol = sym_edit->text();
                QString file = QString("data/%1-%2m.csv")
                                   .arg(symbol.replace('.', ""))
                                   .arg(freq_combo->currentText());
                addStockRow(symbol, file);
                dlg->accept();
            } else {
                status_lbl->setText("下载失败，检查 python/baostock");
            } });

                proc->start("python", args); // 如果 python 不在 PATH，改成绝对路径
            });

    dlg->exec();
}