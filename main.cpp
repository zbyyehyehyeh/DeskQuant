#include <QApplication>
#include <QMainWindow>
#include <QTabWidget>
#include <QLabel>
#include "gui/backtest_page.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QMainWindow win;
    win.setWindowTitle("desk-quant");
    win.resize(1000, 700);

    auto* tabs = new QTabWidget();
    tabs->addTab(new BacktestPage(), "回测");
    tabs->addTab(new QLabel("模拟盘（数据源接入后实现）"), "模拟盘");

    win.setCentralWidget(tabs);
    win.show();
    return app.exec();
}