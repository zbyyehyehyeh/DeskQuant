#pragma once
#include <QString>
#include <QWidget>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QDialog>
#include <QFormLayout>
#include <QPushButton>
#include <vector>
#include <string>
#include "strategies/all.hpp"
#include "data_mode.hpp"

// 解析默认参数 "20,50" → [20, 50]
inline std::vector<double> parseDefaultParams(const std::string& name) {
    std::vector<double> v;
    QString s = QString::fromStdString(defaultParamsFor(name));
    for (const QString& part : s.split(',', Qt::SkipEmptyParts)) {
        bool ok = false; double d = part.trimmed().toDouble(&ok);
        if (ok) v.push_back(d);
    }
    return v;
}

// 一个策略的参数行：输入框 + 默认值（Python 策略用 is_python + python_code）
struct StrategyParamRow {
    bool is_python = false;
    std::string python_code;
    std::vector<QLineEdit*> edits;
    std::vector<double> defaults;
};

// 构建参数控件：每个参数 = 标签（不可改）+ 输入框（可改），空值用默认
inline QWidget* buildParamsWidget(const std::string& name, StrategyParamRow& row) {
    auto names = paramNamesFor(name);
    row.defaults = parseDefaultParams(name);
    auto* w = new QWidget();
    auto* layout = new QHBoxLayout(w);
    layout->setContentsMargins(2, 0, 2, 0);
    layout->setSpacing(3);
    for (size_t i = 0; i < names.size(); ++i) {
        auto* label = new QLabel(QString::fromStdString(names[i]));
        auto* edit = new QLineEdit();
        edit->setFixedWidth(50);
        edit->setText(i < row.defaults.size() ? QString::number(row.defaults[i]) : "");
        layout->addWidget(label);
        layout->addWidget(edit);
        row.edits.push_back(edit);
    }
    layout->addStretch();
    return w;
}

// 交易规则说明文本（按数据源模式动态切换）
inline QString tradingRulesText(DataMode mode) {
    if (mode == DataMode::China) {
        return QStringLiteral(
            "涨跌停：主板 ±10%，创业板/科创板 ±20%\n"
            "T+1：当天买入，下一交易日才能卖出\n"
            "一手：100 股整数倍\n"
            "费用：佣金 万2.5（最低 5 元）｜印花税 万5（卖出）｜过户费 万0.1（沪市收，深市免）");
    }
    return QStringLiteral(
        "涨跌停：无（美股/加股无涨跌停）\n"
        "T+0：当日买入当日可卖\n"
        "数量：1 股起（可碎股）\n"
        "费用：美股零佣金；加股 Questrade：C$0.01/股（最低 C$4.95，最高 C$9.95）+ ECN C$0.0035/股\n"
        "计价：账户/盈亏以人民币为基准，按汇率换算（USD/CAD → CNY；汇率源：新浪实时，Twelve Data/open.er-api 兜底）");
}

// 交易规则说明（带标题框）
inline QGroupBox* tradingRulesWidget(DataMode mode) {
    auto* box = new QGroupBox(mode == DataMode::China ? "交易规则（A股）" : "交易规则（美股/加股）");
    auto* label = new QLabel(box);
    label->setObjectName("tradingRulesLabel");
    label->setText(tradingRulesText(mode));
    label->setWordWrap(true);
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(8, 4, 8, 8);
    layout->addWidget(label);
    return box;
}

// 数据源模式切换后，刷新交易规则说明
inline void updateTradingRules(QGroupBox* box, DataMode mode) {
    if (!box) return;
    box->setTitle(mode == DataMode::China ? "交易规则（A股）" : "交易规则（美股/加股）");
    auto* label = box->findChild<QLabel*>("tradingRulesLabel");
    if (label) label->setText(tradingRulesText(mode));
}

// 通用输入弹框：多个带标签的输入框，返回输入值（字符串列表）
inline QStringList promptInputs(const QString& title, const QStringList& labels) {
    QStringList results;
    QDialog dlg;
    dlg.setWindowTitle(title);
    auto* form = new QFormLayout(&dlg);
    QList<QLineEdit*> edits;
    for (const QString& label : labels) {
        auto* edit = new QLineEdit();
        form->addRow(label + "：", edit);
        edits << edit;
    }
    auto* ok = new QPushButton("确定");
    form->addRow(ok);
    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    if (dlg.exec() == QDialog::Accepted) {
        for (auto* e : edits) results << e->text().trimmed();
    }
    return results;
}
