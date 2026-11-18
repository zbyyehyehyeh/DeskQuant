#pragma once
#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTableView>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QGroupBox>
#include <utility>
#include <vector>
#include "strategy_params.hpp"

class QChartView;

class BacktestPage : public QWidget {
    Q_OBJECT
public:
    explicit BacktestPage(QWidget* parent = nullptr);

    // 数据源模式切换后，刷新交易规则说明
    void refreshRules();

private slots:
    void onRun();
    void onDownload();
    void onAddStock();
    void onRemoveStock();
    void onPythonStrategy();
    void reloadStrategyTable();

private:
    void addStockRow(const QString& symbol, const QString& file);
    void runPythonBacktest(const QString& code);
    void renderChart();   // 按当前选中币种，把人民币基准曲线换算后渲染

    QTableWidget* strategy_table_;   // 策略表（勾选 | 策略 | 参数）
    std::vector<StrategyParamRow> param_rows_;
    QPushButton* run_btn_;
    QPushButton* download_btn_;
    QPushButton* add_btn_;
    QPushButton* remove_btn_;
    QTableWidget* stock_table_;
    QGroupBox* rules_box_;
    QComboBox* currency_combo_;      // 曲线显示币种（CNY/USD/CAD）
    QChartView* equity_view_;
    QLabel* metrics_label_;
    QTableView* trade_table_;
    QStandardItemModel* trade_model_;

    // 最近一次回测结果（人民币基准），供切换币种时重绘
    std::vector<std::pair<QString, std::vector<double>>> last_curves_;
    std::vector<double> last_baseline_;
};
