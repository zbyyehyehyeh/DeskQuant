#include <QApplication>
#include <QCoreApplication>
#include <QMainWindow>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QTimer>
#include "gui/backtest_page.h"
#include "gui/live_tabs.hpp"
#include "gui/data_mode.hpp"
#include "gui/fx_service.hpp"
#include "currency.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // 加载已保存的 Twelve Data API key（国外行情用，第一次在界面输入后自动存 apikey.txt）
    globalTwelveDataKey() = loadApiKeyFromFile();

    // 让嵌入的 Python 解释器找到打包的标准库（同学电脑无需装 Python）
    // pack.sh 会把标准库复制到 DeskQuant_SendOut/lib/python3.x/
    qputenv("PYTHONHOME", QCoreApplication::applicationDirPath().toLocal8Bit());

    QMainWindow win;
    win.setWindowTitle("desk-quant");
    win.resize(1000, 700);

    auto* central = new QWidget();
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(6, 6, 6, 6);

    // ---- 顶部：数据源选择（国内 / 国外）+ API key ----
    auto* top = new QHBoxLayout();
    top->addWidget(new QLabel("数据源"));
    auto* mode_combo = new QComboBox();
    mode_combo->addItems({"国内（A股）", "国外（美股/加股）"});
    mode_combo->setCurrentIndex(globalDataMode() == DataMode::China ? 0 : 1);
    top->addWidget(mode_combo);

    top->addWidget(new QLabel("Twelve Data API Key"));
    auto* key_edit = new QLineEdit(globalTwelveDataKey());
    key_edit->setPlaceholderText("国外行情需要，粘贴后自动保存");
    key_edit->setEnabled(globalDataMode() == DataMode::Abroad);
    top->addWidget(key_edit, 1);
    top->addStretch();
    root->addLayout(top);

    // 汇率显示栏（实时刷新，基准币种人民币；汇率源 open.er-api.com 免费无 key）
    auto* fx_label = new QLabel();
    fx_label->setStyleSheet("color: #1a6fb5;");
    root->addWidget(fx_label);

    // ---- 标签页：回测 / 模拟盘 ----
    auto* tabs = new QTabWidget();
    auto* backtest_page = new BacktestPage();
    auto* live_tabs = new LiveTabs();
    tabs->addTab(backtest_page, "回测");
    tabs->addTab(live_tabs, "模拟盘");
    root->addWidget(tabs);

    win.setCentralWidget(central);

    // 汇率服务：用到时当场请求（Twelve Data 实时优先，open.er-api 兜底），只存内存不落盘
    auto* fx_service = new FxService(&win);
    g_fxService = fx_service;
    fx_service->fetchNow();

    auto updateFxLabel = [fx_label]() {
        const FxRates& r = globalFxRates();
        const FxMeta& m = globalFxMeta();
        QString quota = (m.source == FxSource::Twelve && m.twelve_remaining >= 0)
            ? QString(" | 剩余 %1 次").arg(m.twelve_remaining) : "";
        fx_label->setText(QString("汇率（1 外币 = X 人民币）：USD/CNY %1 | CAD/CNY %2 | USD/CAD %3 | 源：%4%5")
                              .arg(r.usd_to_cny, 0, 'f', 4)
                              .arg(r.cad_to_cny, 0, 'f', 4)
                              .arg(r.usd_to_cad, 0, 'f', 4)
                              .arg(fxSourceName(m.source)).arg(quota));
    };
    updateFxLabel();

    // 每秒刷新汇率显示（只读内存 globalFxRates/globalFxMeta，不发请求）
    auto* fx_display_timer = new QTimer(&win);
    fx_display_timer->setInterval(1000);
    QObject::connect(fx_display_timer, &QTimer::timeout, updateFxLabel);
    fx_display_timer->start();

    // 切换数据源：写入全局模式 + 刷新各页交易规则
    QObject::connect(mode_combo, &QComboBox::currentIndexChanged,
                     [backtest_page, live_tabs, key_edit, updateFxLabel](int idx) {
        globalDataMode() = (idx == 0) ? DataMode::China : DataMode::Abroad;
        key_edit->setEnabled(globalDataMode() == DataMode::Abroad);
        backtest_page->refreshRules();
        live_tabs->refreshAllRules();
        updateFxLabel();
    });

    // API key 变更：写入全局 + 自动保存
    QObject::connect(key_edit, &QLineEdit::textChanged, [](const QString& text) {
        globalTwelveDataKey() = text.trimmed();
        saveApiKeyToFile(globalTwelveDataKey());
    });

    win.show();
    return app.exec();
}
