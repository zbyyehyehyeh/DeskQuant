#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QPushButton>
#include "live_page.h"

// 模拟盘宿主：多个模拟盘同时运行，每个一个标签页
class LiveTabs : public QWidget {
public:
    explicit LiveTabs(QWidget* parent = nullptr) : QWidget(parent) {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);

        auto* top = new QHBoxLayout();
        auto* add_btn = new QPushButton("+ 新建模拟盘");
        top->addWidget(add_btn);
        top->addStretch();
        root->addLayout(top);

        tabs_ = new QTabWidget();
        tabs_->setTabsClosable(true);
        root->addWidget(tabs_);

        addTab();
        connect(add_btn, &QPushButton::clicked, this, [this]() { addTab(); });
        connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int i) {
            if (tabs_->count() > 1) {
                QWidget* w = tabs_->widget(i);
                tabs_->removeTab(i);
                w->deleteLater();
            }
        });
    }

    // 数据源模式切换后，刷新所有模拟盘的交易规则说明
    void refreshAllRules() {
        for (int i = 0; i < tabs_->count(); ++i) {
            if (auto* lp = qobject_cast<LivePage*>(tabs_->widget(i)))
                lp->refreshRules();
        }
    }

private:
    void addTab() {
        tabs_->addTab(new LivePage(), QString("模拟盘 %1").arg(tabs_->count() + 1));
    }

    QTabWidget* tabs_;
};
