#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QTableWidget>
#include <QGroupBox>
#include <vector>
#include <memory>
#include "live_quote.hpp"
#include "strategy.hpp"
#include "order_book.hpp"
#include "strategy_params.hpp"
#include "stock.hpp"
#include <QElapsedTimer>

class QChartView;

class LivePage : public QWidget {
    Q_OBJECT
public:
    explicit LivePage(QWidget* parent = nullptr);

    // 数据源模式切换后，刷新交易规则说明
    void refreshRules();

private slots:
    void onStart();
    void onStop();
    void onQuote(const Quote& q);
    void onIntervalChanged(int idx);

private:
    struct PaperTrader {
        std::string name;
        std::shared_ptr<Strategy> strat;
        double cash = 1'000'000;
        int position = 0;
        uint64_t buy_day = 0;        // T+1（美股/加股不检查）
        std::vector<double> equity;
    };

    void feedBar(const Bar& bar);
    void updateChart();
    void applyManualFills(const std::vector<Fill>& fills);
    void updateManualLabel();
    void refreshOrdersTable();

    QLineEdit* symbol_edit_;
    QComboBox* interval_combo_;   // K线周期
    QPushButton* start_btn_;
    QPushButton* stop_btn_;
    QLabel* quote_label_;
    QTableWidget* strategy_table_;   // 策略表（勾选 | 策略 | 参数）
    std::vector<StrategyParamRow> param_rows_;
    QChartView* chart_view_;
    QLabel* summary_label_;
    QLabel* duration_label_;
    QElapsedTimer timer_;

    LiveQuoteSource* src_;
    std::vector<PaperTrader> traders_;
    double last_price_ = 0;
    double last_pre_close_ = 0;

    // 市场规则（onStart 按代码自动判定）
    Market market_ = Market::China;
    double price_limit_ = 0.10;
    int lot_size_ = 100;
    bool t_plus_1_ = true;
    int bars_ = 0;

    // K线聚合
    Bar current_bar_;
    uint64_t current_bucket_ = 0;
    bool has_bar_ = false;
    int interval_ms_ = 60000;

    // 手动交易
    OrderBook book_;
    bool book_initialized_ = false;
    double manual_cash_ = 1'000'000;
    int manual_position_ = 0;
    QLabel* manual_label_;
    QTableWidget* orders_table_;

    QGroupBox* rules_box_;
};
