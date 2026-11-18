#pragma once
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QInputDialog>
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QHeaderView>
#include <QListWidget>
#include <QAbstractItemView>
#include <QMessageBox>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include "strategy_params.hpp"
#include "python_strategy.hpp"
#include "server_client.hpp"

// Python 策略模板（注释列出可调用的参数和方法）
inline QString pythonStrategyTemplate() {
    return QStringLiteral(
        "# ==================================================\n"
        "# Python 策略模板\n"
        "# 实现 Strategy 类，包含两个方法：\n"
        "#\n"
        "# 【on_bar(self, bar)】每来一根 K线调用一次\n"
        "#   bar 是 dict，可用字段：\n"
        "#     open   (float) 开盘价\n"
        "#     high   (float) 最高价\n"
        "#     low    (float) 最低价\n"
        "#     close  (float) 收盘价\n"
        "#     volume (float) 成交量\n"
        "#\n"
        "# 【signal(self)】返回信号字符串：\n"
        "#   'buy'  买入（全仓）\n"
        "#   'sell' 卖出（清仓）\n"
        "#   'hold' 持有/空仓\n"
        "#\n"
        "# 可 import 标准库（math、statistics 等）\n"
        "# ==================================================\n"
        "\n"
        "class Strategy:\n"
        "    def __init__(self):\n"
        "        self.closes = []\n"
        "\n"
        "    def on_bar(self, bar):\n"
        "        self.closes.append(bar['close'])\n"
        "\n"
        "    def signal(self):\n"
        "        return 'hold'\n");
}

// ---- 策略文件存储（strategies_py/<名字>.py，全局共享）----
inline QStringList listPythonStrategies() {
    QStringList names;
    QDir dir("strategies_py");
    if (dir.exists())
        for (const QFileInfo& fi : dir.entryInfoList({"*.py"}, QDir::Files))
            names << fi.baseName();
    return names;
}

inline QString loadPythonStrategy(const QString& name) {
    QFile f("strategies_py/" + name + ".py");
    if (!f.open(QIODevice::ReadOnly)) return "";
    return QString::fromUtf8(f.readAll());
}

inline void savePythonStrategy(const QString& name, const QString& code) {
    QDir().mkpath("strategies_py");
    QFile f("strategies_py/" + name + ".py");
    if (f.open(QIODevice::WriteOnly)) f.write(code.toUtf8());
}

inline void deletePythonStrategy(const QString& name) {
    QFile::remove("strategies_py/" + name + ".py");
}

// ---- 填充策略表（C++ 策略带参数 + Python 策略无参数）----
inline void populateStrategyTable(QTableWidget* table, std::vector<StrategyParamRow>& rows) {
    table->setRowCount(0);
    rows.clear();

    // C++ 策略
    for (auto& name : StrategyFactory::instance().names()) {
        int r = table->rowCount();
        table->insertRow(r);
        auto* check = new QTableWidgetItem();
        check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        check->setCheckState(Qt::Checked);
        table->setItem(r, 0, check);
        table->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(name)));
        StrategyParamRow prow;
        table->setCellWidget(r, 2, buildParamsWidget(name, prow));
        rows.push_back(std::move(prow));
    }

    // Python 策略（无参数，名带 py: 前缀）
    for (const QString& pname : listPythonStrategies()) {
        int r = table->rowCount();
        table->insertRow(r);
        auto* check = new QTableWidgetItem();
        check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        check->setCheckState(Qt::Checked);
        table->setItem(r, 0, check);
        table->setItem(r, 1, new QTableWidgetItem("py:" + pname));
        table->setItem(r, 2, new QTableWidgetItem("-"));
        StrategyParamRow prow;
        prow.is_python = true;
        prow.python_code = loadPythonStrategy(pname).toStdString();
        rows.push_back(std::move(prow));
    }
}

// ---- 从策略表一行创建策略实例（失败返回 nullptr）----
inline std::shared_ptr<Strategy> createStrategyFromRow(const StrategyParamRow& prow, const QString& name) {
    if (prow.is_python) {
        auto s = std::make_shared<PythonStrategy>(prow.python_code);
        if (!s->error().empty()) return nullptr;
        return s;
    }
    std::vector<double> params;
    for (size_t i = 0; i < prow.edits.size(); ++i) {
        bool ok = false;
        double d = prow.edits[i]->text().toDouble(&ok);
        params.push_back(ok ? d : prow.defaults[i]);
    }
    return createStrategyWithParams(name.toStdString(), params);
}

// ---- 策略代码安全验证（上传前）：先静态黑名单，再 Python 语法 + 接口检查 ----
inline bool validateStrategyCode(const QString& code, QString& err) {
    // 1. 静态安全检查（在 exec 之前，避免执行到恶意代码）
    static const QStringList badMods = {
        "os", "subprocess", "socket", "sys", "shutil", "pathlib",
        "pickle", "ctypes", "importlib", "builtins", "tempfile", "pty"
    };
    static const QStringList badCalls = {
        "eval(", "exec(", "execfile(", "__import__(", "compile(", "open(", "input("
    };

    const QStringList lines = code.split('\n');
    for (QString line : lines) {
        int hash = line.indexOf('#');
        if (hash >= 0) line = line.left(hash);   // 去掉行内注释（简化，不处理字符串里的 #）
        QString t = line.trimmed();
        if (t.isEmpty()) continue;

        if (t.startsWith("import ") || t.startsWith("from ")) {
            QString mod;
            if (t.startsWith("from ")) {
                mod = t.mid(5);
                int sp = mod.indexOf(' ');
                if (sp >= 0) mod = mod.left(sp);
            } else {
                mod = t.mid(7).split(',')[0].trimmed();
                int sp = mod.indexOf(' ');
                if (sp >= 0) mod = mod.left(sp);
            }
            mod = mod.split('.')[0].trimmed();   // 顶级模块名
            if (badMods.contains(mod)) { err = "禁止导入模块: " + mod; return false; }
        }
        for (const QString& d : badCalls) {
            if (t.contains(d)) { err = "禁止使用: " + d; return false; }
        }
    }

    // 2. Python 语法 + Strategy 类检查
    PythonStrategy probe(code.toStdString());
    if (!probe.error().empty()) { err = QString::fromStdString(probe.error()); return false; }

    return true;
}

// ---- 批量上传：列出所有自定义 Python 策略，勾选后上传 ----
inline void openUploadToServerDialog(QWidget* parent) {
    const QStringList names = listPythonStrategies();
    if (names.isEmpty()) {
        QMessageBox::information(parent, "上传到服务器", "还没有自定义 Python 策略，请先保存/导入一个。");
        return;
    }

    QDialog dlg(parent);
    dlg.setWindowTitle("上传策略到服务器");
    dlg.resize(360, 440);

    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel("选择要上传的策略（可多选）:"));

    auto* list = new QListWidget();
    list->setSelectionMode(QAbstractItemView::MultiSelection);
    for (const QString& n : names) list->addItem(n);
    layout->addWidget(list, 1);

    auto* btns = new QHBoxLayout();
    auto* all_btn = new QPushButton("全选");
    auto* ok_btn = new QPushButton("上传选中");
    auto* cancel_btn = new QPushButton("取消");
    btns->addWidget(all_btn);
    btns->addWidget(ok_btn);
    btns->addStretch();
    btns->addWidget(cancel_btn);
    layout->addLayout(btns);

    QObject::connect(all_btn, &QPushButton::clicked, [list]() {
        for (int i = 0; i < list->count(); ++i) list->item(i)->setSelected(true);
    });
    QObject::connect(cancel_btn, &QPushButton::clicked, &dlg, &QDialog::reject);

    QObject::connect(ok_btn, &QPushButton::clicked, [&dlg, list]() {
        const QList<QListWidgetItem*> sel = list->selectedItems();
        if (sel.isEmpty()) { QMessageBox::information(&dlg, "上传", "请先选择要上传的策略"); return; }

        bool ok = false;
        QString baseUrl = QInputDialog::getText(&dlg, "上传到服务器", "服务器地址（https://host:port）:",
                                                QLineEdit::Normal, "https://127.0.0.1:9100", &ok);
        if (!ok || baseUrl.trimmed().isEmpty()) return;

        // 先本地校验，拆出「可上传」和「失败」
        struct Item { QString name; QString code; };
        std::vector<Item> toUpload;
        QStringList failPre;
        for (QListWidgetItem* it : sel) {
            QString name = it->text();
            QString code = loadPythonStrategy(name);
            if (code.isEmpty()) { failPre << name + "（读不到源码）"; continue; }
            QString err;
            if (!validateStrategyCode(code, err)) { failPre << name + "（" + err + "）"; continue; }
            toUpload.push_back({name, code});
        }

        if (toUpload.empty()) {
            QMessageBox::information(&dlg, "上传", "没有通过校验的策略：\n" + failPre.join("\n"));
            return;
        }

        struct BatchState { int done = 0; int ok = 0; };
        auto state = std::make_shared<BatchState>();
        auto fails = std::make_shared<QStringList>(failPre);
        const int total = static_cast<int>(toUpload.size());

        for (const Item& item : toUpload) {
            auto* client = new ServerClient();   // 无父对象，回调里 deleteLater
            client->upload(baseUrl.trimmed(), item.name, item.code,
                [state, fails, total, item, client](bool o, const QString& m) {
                    if (o) state->ok++;
                    else   fails->push_back(item.name + "（" + m + "）");
                    if (++(state->done) == total) {
                        QMessageBox::information(nullptr, "上传完成",
                            QString("成功 %1 个，失败 %2 个").arg(state->ok).arg(fails->size())
                            + (fails->isEmpty() ? "" : "\n" + fails->join("\n")));
                    }
                    client->deleteLater();
                });
        }
        dlg.accept();
    });

    dlg.exec();
}

// ---- Python 策略对话框（编辑 + 保存 + 导入），runFn 是"运行"回调 ----
inline void openPythonStrategyDialog(QWidget* parent,
                                     const std::function<void(const QString&)>& runFn,
                                     const std::function<void()>& reloadFn) {
    QDialog dlg(parent);
    dlg.setWindowTitle("Python 策略");
    dlg.resize(640, 540);
    auto* layout = new QVBoxLayout(&dlg);
    auto* editor = new QPlainTextEdit();
    editor->setPlainText(pythonStrategyTemplate());
    auto* status = new QLabel("就绪");
    layout->addWidget(editor, 1);

    auto* btns = new QHBoxLayout();
    auto* save_btn = new QPushButton("保存策略");
    auto* run_btn = new QPushButton("运行");
    auto* upload_btn = new QPushButton("上传到服务器");
    auto* import_btn = new QPushButton("导入 .py");
    btns->addWidget(save_btn);
    btns->addWidget(run_btn);
    btns->addWidget(upload_btn);
    btns->addStretch();
    btns->addWidget(import_btn);
    layout->addLayout(btns);
    layout->addWidget(status);

    QObject::connect(save_btn, &QPushButton::clicked, [&]() {
        bool ok = false;
        QString name = QInputDialog::getText(&dlg, "保存策略", "策略名（英文）:",
                                             QLineEdit::Normal, "", &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        savePythonStrategy(name.trimmed(), editor->toPlainText());
        reloadFn();
        status->setText("已保存：" + name.trimmed());
    });

    QObject::connect(run_btn, &QPushButton::clicked, [&]() {
        QString code = editor->toPlainText();
        if (code.trimmed().isEmpty()) { status->setText("请输入代码"); return; }
        runFn(code);
        dlg.accept();
    });

    // 批量上传：列出已保存的自定义 Python 策略，勾选后上传（上传前会做安全校验）
    QObject::connect(upload_btn, &QPushButton::clicked, [&]() {
        openUploadToServerDialog(&dlg);
    });

    QObject::connect(import_btn, &QPushButton::clicked, [&]() {
        QStringList files = QFileDialog::getOpenFileNames(&dlg, "导入 Python 策略", "", "Python (*.py)");
        if (files.isEmpty()) return;
        QDir().mkpath("strategies_py");
        int ok = 0, fail = 0;
        for (const QString& f : files) {
            QFileInfo fi(f);
            QString target = "strategies_py/" + fi.fileName();
            if (!QFile::copy(f, target)) { ++fail; continue; }
            PythonStrategy probe(loadPythonStrategy(fi.baseName()).toStdString());
            if (probe.error().empty()) ++ok;
            else { QFile::remove(target); ++fail; }
        }
        reloadFn();
        status->setText(QString("导入 %1 个，失败 %2 个").arg(ok).arg(fail));
    });

    dlg.exec();
}
