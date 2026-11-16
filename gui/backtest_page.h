#pragma once
#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTableView>
#include <QStandardItemModel>
#include <QTableWidget>

class QChartView;

class BacktestPage : public QWidget {
    Q_OBJECT
public:
    explicit BacktestPage(QWidget* parent = nullptr);

private slots:
    void onRun();
    void onDownload();
    void onAddStock();
    void onRemoveStock();

private:
    void addStockRow(const QString& symbol, const QString& file);

    QComboBox* strategy_combo_;
    QPushButton* run_btn_;
    QPushButton* download_btn_;
    QPushButton* add_btn_;
    QPushButton* remove_btn_;
    QTableWidget* stock_table_;
    QChartView* equity_view_;       // 资金曲线（多股票合并）
    QLabel* metrics_label_;
    QTableView* trade_table_;
    QStandardItemModel* trade_model_;
};