#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>
#include <QVector>
#include <QStringList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <functional>
#include <memory>
#include "data_mode.hpp"

// =============================================================================
// 统一 HTTP 数据层：同一套 HttpClient，按 globalDataMode() 分发到
//   国内 → 东财（历史K线）+ 腾讯（实时行情）
//   国外 → Twelve Data（历史K线 + 实时行情，需要 apikey）
// =============================================================================

// 高性能 HTTP 客户端：异步 + 并发批量 GET
class HttpClient : public QObject {
public:
    explicit HttpClient(QObject* parent = nullptr) : QObject(parent) {
        nam_ = new QNetworkAccessManager(this);
    }

    // 单个 GET（异步回调：ok + 响应体）。referer 可按数据源指定（新浪需要 finance.sina.com.cn）
    void get(const QString& url, const std::function<void(bool, const QByteArray&)>& cb,
             const QByteArray& referer = "https://finance.eastmoney.com") {
        QNetworkRequest req{QUrl(url)};   // 用花括号避免 vexing parse
        req.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
        req.setRawHeader("Referer", referer);
        QNetworkReply* reply = nam_->get(req);
        connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
            bool ok = reply->error() == QNetworkReply::NoError;
            QByteArray body = reply->readAll();
            reply->deleteLater();
            cb(ok, body);
        });
    }

    // 批量并发 GET，全部完成后回调（顺序和 urls 一致，shared_ptr 无泄漏）
    void getBatch(const QStringList& urls, const std::function<void(const QVector<QByteArray>&)>& cb) {
        if (urls.isEmpty()) { cb({}); return; }
        struct State { int remain; QVector<QByteArray> results; };
        auto state = std::make_shared<State>();
        state->remain = urls.size();
        state->results.resize(urls.size());
        for (int i = 0; i < urls.size(); ++i) {
            get(urls[i], [state, cb, i](bool ok, const QByteArray& body) {
                state->results[i] = ok ? body : QByteArray();
                if (--(state->remain) == 0)
                    cb(state->results);
            });
        }
    }

private:
    QNetworkAccessManager* nam_;
};

// =============================================================================
// 国内：东财免费接口下载 K线
// =============================================================================
// symbol: "sh.600000" / "sz.000001"；freq: "5"/"15"/"30"/"60"/"d"；start/end: "2025-01-01"
inline void fetchEastmoneyKline(HttpClient* client, const QString& symbol, const QString& freq,
                                const QString& start, const QString& end,
                                const std::function<void(bool, const QString&)>& cb) {
    QStringList parts = symbol.split('.');
    if (parts.size() != 2) { cb(false, "代码格式错误"); return; }
    QString secid = (parts[0] == "sh") ? "1." + parts[1] : "0." + parts[1];   // 沪=1 深=0
    int klt = (freq == "d") ? 101 : freq.toInt();                              // 101=日线
    QString beg = start; beg.remove('-');
    QString en = end; en.remove('-');

    QString url = QString("http://push2his.eastmoney.com/api/qt/stock/kline/get"
                          "?secid=%1&fields1=f1,f2,f3,f4,f5,f6"
                          "&fields2=f51,f52,f53,f54,f55,f56,f57"
                          "&klt=%2&fqt=1&beg=%3&end=%4")
                      .arg(secid).arg(klt).arg(beg, en);

    client->get(url, [cb](bool ok, const QByteArray& body) {
        if (!ok) { cb(false, "网络错误"); return; }
        QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isNull()) { cb(false, "JSON 解析失败"); return; }
        QJsonObject data = doc.object()["data"].toObject();
        QJsonArray klines = data["klines"].toArray();
        if (klines.isEmpty()) { cb(false, "无数据"); return; }

        QString csv = "ts,open,high,low,close,volume,amount\n";
        for (const QJsonValue& v : klines) {
            QStringList f = v.toString().split(',');
            if (f.size() < 7) continue;
            // 东财字段：f[0]日期 f[1]开 f[2]收 f[3]高 f[4]低 f[5]量 f[6]额
            QString dt = f[0];
            double open = f[1].toDouble();
            double close = f[2].toDouble();
            double high = f[3].toDouble();
            double low = f[4].toDouble();
            double volume = f[5].toDouble();
            double amount = f[6].toDouble();
            QDateTime ts = QDateTime::fromString(dt, "yyyy-MM-dd HH:mm");
            if (!ts.isValid()) ts = QDateTime::fromString(dt, "yyyy-MM-dd");
            qint64 ms = ts.isValid() ? ts.toMSecsSinceEpoch() : 0;
            csv += QString("%1,%2,%3,%4,%5,%6,%7\n")
                       .arg(ms).arg(open, 0, 'f', 4).arg(high, 0, 'f', 4)
                       .arg(low, 0, 'f', 4).arg(close, 0, 'f', 4)
                       .arg(volume, 0, 'f', 0).arg(amount, 0, 'f', 2);
        }
        cb(true, csv);
    });
}

// =============================================================================
// 国外：Twelve Data 下载 K线（需要 apikey）
// =============================================================================
// 符号规范化：大写；加股 .CA → .TO（Twelve Data 用 .TO 表示多伦多）
inline QString toMarketSymbol(const QString& symbol) {
    QString s = symbol.trimmed().toUpper();
    if (s.endsWith(".CA")) { s.chop(3); return s + ".TO"; }
    return s;
}

// symbol: "AAPL" / "SHOP.TO"；freq: "d"/"5"/"15"/"30"/"60"；start/end: "2025-01-01"
inline void fetchKline(HttpClient* client, const QString& symbol, const QString& freq,
                       const QString& start, const QString& end,
                       const std::function<void(bool, const QString&)>& cb) {
    // interval 映射：d→1day，60→1h，其余→Nmin
    QString interval = (freq == "d") ? "1day" : (freq == "60" ? "1h" : freq + "min");

    QUrl url("https://api.twelvedata.com/time_series");
    QUrlQuery q;
    q.addQueryItem("symbol", toMarketSymbol(symbol));
    q.addQueryItem("interval", interval);
    q.addQueryItem("outputsize", "5000");
    if (!start.isEmpty()) q.addQueryItem("start_date", start);
    if (!end.isEmpty())   q.addQueryItem("end_date", end);
    q.addQueryItem("apikey", globalTwelveDataKey());
    url.setQuery(q);

    client->get(url.toString(), [cb](bool ok, const QByteArray& body) {
        if (!ok) { cb(false, "网络错误"); return; }
        QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isNull()) { cb(false, "JSON 解析失败"); return; }
        QJsonObject root = doc.object();
        if (root["status"].toString() != "ok") {
            cb(false, "API 错误：" + root["message"].toString());
            return;
        }
        QJsonArray values = root["values"].toArray();   // 降序（最新在前）
        if (values.isEmpty()) { cb(false, "无数据"); return; }

        QString csv = "ts,open,high,low,close,volume,amount\n";
        for (int i = values.size() - 1; i >= 0; --i) {   // 反转为升序
            QJsonObject v = values[i].toObject();
            QString dt = v["datetime"].toString();
            double open = v["open"].toString().toDouble();
            double high = v["high"].toString().toDouble();
            double low = v["low"].toString().toDouble();
            double close = v["close"].toString().toDouble();
            double volume = v["volume"].toString().toDouble();
            QDateTime ts = QDateTime::fromString(dt, "yyyy-MM-dd HH:mm:ss");
            if (!ts.isValid()) ts = QDateTime::fromString(dt, "yyyy-MM-dd");
            qint64 ms = ts.isValid() ? ts.toMSecsSinceEpoch() : 0;
            if (ms == 0) continue;
            csv += QString("%1,%2,%3,%4,%5,%6,0\n")
                       .arg(ms).arg(open, 0, 'f', 4).arg(high, 0, 'f', 4)
                       .arg(low, 0, 'f', 4).arg(close, 0, 'f', 4).arg(volume, 0, 'f', 0);
        }
        cb(true, csv);
    });
}

// 下载并保存到 data/<符号>-<周期>.csv，按数据源模式分发，返回文件路径
inline void downloadAndSaveKline(HttpClient* client, const QString& symbol, const QString& freq,
                                 const QString& start, const QString& end,
                                 const std::function<void(bool, const QString&)>& cb) {
    if (globalDataMode() == DataMode::China) {
        fetchEastmoneyKline(client, symbol, freq, start, end, [symbol, freq, cb](bool ok, const QString& csv) {
            if (!ok) { cb(false, csv); return; }
            QDir().mkpath("data");
            QString code = symbol;
            code.remove('.');
            QString suffix = (freq == "d") ? "d" : freq + "m";
            QString file = QString("data/%1-%2.csv").arg(code, suffix);
            QFile f(file);
            if (!f.open(QIODevice::WriteOnly)) { cb(false, "写文件失败"); return; }
            f.write(csv.toUtf8());
            f.close();
            cb(true, file);
        });
    } else {
        fetchKline(client, symbol, freq, start, end, [symbol, freq, cb](bool ok, const QString& csv) {
            if (!ok) { cb(false, csv); return; }
            QDir().mkpath("data");
            QString sym = toMarketSymbol(symbol);
            QString suffix = (freq == "d") ? "d" : freq + "m";
            QString file = QString("data/%1-%2.csv").arg(sym, suffix);
            QFile f(file);
            if (!f.open(QIODevice::WriteOnly)) { cb(false, "写文件失败"); return; }
            f.write(csv.toUtf8());
            f.close();
            cb(true, file);
        });
    }
}
