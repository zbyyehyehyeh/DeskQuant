#pragma once
#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include "types.hpp"
#include "data_mode.hpp"
#include "http_client.hpp"

// 统一实时行情源：按 globalDataMode() 分发
//   国内 → 腾讯免费行情（3 秒轮询，真实五档）
//   国外 → Twelve Data（15 秒轮询，免费额度 8 次/分钟；无盘口，合成五档）
class LiveQuoteSource : public QObject {
    Q_OBJECT
public:
    explicit LiveQuoteSource(QObject* parent = nullptr) : QObject(parent) {
        client_ = new HttpClient(this);
        timer_ = new QTimer(this);
        timer_->setInterval(3000);
        connect(timer_, &QTimer::timeout, this, &LiveQuoteSource::fetch);
    }

    void setSymbol(const QString& symbol) {
        symbol_ = symbol;
        timer_->setInterval(globalDataMode() == DataMode::China ? 3000 : 15000);
        fetch();
    }
    void start() { timer_->start(); }
    void stop()  { timer_->stop(); }

signals:
    void quoteUpdated(const Quote& q);
    void errorOccurred(const QString& msg);

private:
    void fetch() {
        if (symbol_.isEmpty()) return;
        if (globalDataMode() == DataMode::China) fetchChina(); else fetchAbroad();
    }

    // ---- 国内：腾讯行情 ----
    void fetchChina() {
        client_->get("http://qt.gtimg.cn/q=" + symbol_, [this](bool ok, const QByteArray& body) {
            if (!ok) { emit errorOccurred("网络错误"); return; }
            // 响应里数字是 ASCII，用 Latin1 直接解析（股票名是 GBK，不取名字）
            QString text = QString::fromLatin1(body);
            int a = text.indexOf('"');
            int b = text.lastIndexOf('"');
            if (a < 0 || b <= a) { emit errorOccurred("解析失败"); return; }
            QStringList f = text.mid(a + 1, b - a - 1).split('~');
            if (f.size() < 40) { emit errorOccurred("字段不足"); return; }

            Quote q;
            q.ts        = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
            q.last      = f[3].toDouble();   // 现价
            q.open      = f[5].toDouble();   // 今开
            q.pre_close = f[4].toDouble();   // 昨收
            q.high      = f[33].toDouble();  // 最高
            q.low       = f[34].toDouble();  // 最低
            q.volume    = f[6].toDouble();   // 成交量（手）
            for (int i = 0; i < 5; ++i) {
                q.bid_p[i] = f[9  + i * 2].toDouble();
                q.bid_v[i] = f[10 + i * 2].toDouble();
                q.ask_p[i] = f[19 + i * 2].toDouble();
                q.ask_v[i] = f[20 + i * 2].toDouble();
            }
            emit quoteUpdated(q);
        });
    }

    // ---- 国外：Twelve Data ----
    void fetchAbroad() {
        QUrl url("https://api.twelvedata.com/quote");
        QUrlQuery q;
        q.addQueryItem("symbol", toMarketSymbol(symbol_));
        q.addQueryItem("apikey", globalTwelveDataKey());
        url.setQuery(q);

        client_->get(url.toString(), [this](bool ok, const QByteArray& body) {
            if (!ok) { emit errorOccurred("网络错误"); return; }
            QJsonDocument doc = QJsonDocument::fromJson(body);
            if (doc.isNull()) { emit errorOccurred("JSON 解析失败"); return; }
            QJsonObject o = doc.object();
            if (o.contains("status") && o["status"].toString() == "error") {
                emit errorOccurred(o["message"].toString());
                return;
            }
            double open = o["open"].toString().toDouble();
            double high = o["high"].toString().toDouble();
            double low = o["low"].toString().toDouble();
            double close = o["close"].toString().toDouble();
            double pre_close = o["previous_close"].toString().toDouble();
            double volume = o["volume"].toString().toDouble();

            Quote q;
            q.ts        = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
            q.last      = close;
            q.open      = open;
            q.high      = high;
            q.low       = low;
            q.pre_close = pre_close > 0 ? pre_close : open;
            q.volume    = volume;
            // 合成五档（Twelve Data 免费接口无盘口，用 ±0.01 模拟；挂单量给足以支持手动撮合）
            for (int i = 0; i < 5; ++i) {
                q.bid_p[i] = close - 0.01 * (i + 1);
                q.bid_v[i] = 1e9;
                q.ask_p[i] = close + 0.01 * (i + 1);
                q.ask_v[i] = 1e9;
            }
            emit quoteUpdated(q);
        });
    }

    QString symbol_;
    HttpClient* client_;
    QTimer* timer_;
};
