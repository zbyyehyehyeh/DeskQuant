#pragma once
// =============================================================================
// 数据源模式（国内 / 国外）+ Twelve Data API Key 管理
//
// 全局单例 globalDataMode()：主窗口的「数据源」下拉框写入，回测页/模拟盘页/
// 行情源在运行时读取，决定用哪套数据接口。
//   China  → 东财（历史K线）+ 腾讯（实时行情），无需 API key
//   Abroad → Twelve Data（历史K线 + 实时行情），需要 API key（自动存 apikey.txt）
// =============================================================================
#include <QString>
#include <QFile>

enum class DataMode { China, Abroad };

inline DataMode& globalDataMode() {
    static DataMode mode = DataMode::China;
    return mode;
}

inline const char* dataModeName(DataMode m) {
    return m == DataMode::China ? "国内" : "国外";
}

// ---- Twelve Data API Key（免费额度 800 次/天、8 次/分钟）----
inline QString& globalTwelveDataKey() {
    static QString key;
    return key;
}

inline QString loadApiKeyFromFile() {
    QFile f("apikey.txt");
    if (f.open(QIODevice::ReadOnly)) return QString::fromUtf8(f.readAll()).trimmed();
    return "";
}

inline void saveApiKeyToFile(const QString& key) {
    QFile f("apikey.txt");
    if (f.open(QIODevice::WriteOnly)) { f.write(key.toUtf8()); f.close(); }
}
