#pragma once
#include <QObject>
#include <QUrl>
#include <QUrlQuery>
#include <QString>
#include <QStringList>
#include <QJsonDocument>
#include <QJsonObject>
#include <functional>
#include <memory>
#include "http_client.hpp"
#include "currency.hpp"
#include "data_mode.hpp"

// =============================================================================
// 汇率源（按准确度优先级；结果只存内存，不落盘）
//   1. 新浪财经（国内源，实时中间价，免费无 key）——首选，回测/模拟盘都用它
//   2. Twelve Data（实时，带 key）——新浪失败时兜底
//   3. open.er-api（日更，免费无 key）——最终兜底
// =============================================================================
enum class FxSource { None, Sina, Twelve, OpenErApi };

inline const char* fxSourceName(FxSource s) {
    switch (s) {
        case FxSource::Sina:      return "新浪（实时中间价，免费无 key）";
        case FxSource::Twelve:    return "Twelve Data（实时，限 800 次/天）";
        case FxSource::OpenErApi: return "open.er-api（日更，非实时）";
        default:                  return "未知";
    }
}

struct FxMeta {
    FxSource source = FxSource::None;
    int twelve_remaining = -1;   // Twelve Data 剩余调用次数（-1 = 未知/未启用）
};
inline FxMeta& globalFxMeta() { static FxMeta m; return m; }

class FxService : public QObject {
public:
    explicit FxService(QObject* parent = nullptr) : QObject(parent) {
        client_ = new HttpClient(this);
    }

    // 用到时当场请求；按 新浪 → Twelve Data → open.er-api 依次降级
    void fetchNow() { fetchSina(); }

private:
    // ---- 首选：新浪财经（实时中间价；新浪只提供「X 对人民币」牌价） ----
    void fetchSina() {
        // 新浪外汇代码只有「外币兑人民币」：fx_susdcny、fx_scadcny
        // USD/CAD 由两者算出：usd_to_cad = usd_to_cny / cad_to_cny
        const QString url = "https://hq.sinajs.cn/list=fx_susdcny,fx_scadcny";
        client_->get(url, [this](bool ok, const QByteArray& body) {
            if (!ok) { fetchFallback(); return; }
            double usd_cny = 0, cad_cny = 0;
            // 数字字段是 ASCII，用 Latin1 解析即可（中文名是 GBK，不取）
            const QString text = QString::fromLatin1(body);
            for (const QString& line : text.split('\n')) {
                int a = line.indexOf('"');
                int b = line.lastIndexOf('"');
                if (a < 0 || b <= a) continue;
                const QStringList f = line.mid(a + 1, b - a - 1).split(',');
                if (f.size() < 4) continue;
                double middle = f[3].toDouble();   // 中间价
                if (middle <= 0) continue;
                if (line.contains("fx_susdcny"))      usd_cny = middle;
                else if (line.contains("fx_scadcny")) cad_cny = middle;
            }
            if (usd_cny > 0 && cad_cny > 0) {
                globalFxRates().usd_to_cny = usd_cny;
                globalFxRates().cad_to_cny = cad_cny;
                globalFxRates().usd_to_cad = usd_cny / cad_cny;
                globalFxMeta().source = FxSource::Sina;
            } else {
                fetchFallback();
            }
        }, "https://finance.sina.com.cn");
    }

    // ---- 兜底：Twelve Data（有 key）→ open.er-api ----
    void fetchFallback() {
        const QString key = globalTwelveDataKey().trimmed();
        if (key.isEmpty()) { fetchOpenErApi(); return; }
        fetchTwelveQuota(key);
        fetchTwelveForex(key);
    }

    void fetchTwelveQuota(const QString& key) {
        QUrl url("https://api.twelvedata.com/api_usage");
        QUrlQuery q; q.addQueryItem("apikey", key); url.setQuery(q);
        client_->get(url.toString(), [](bool ok, const QByteArray& body) {
            if (!ok) return;
            QJsonDocument doc = QJsonDocument::fromJson(body);
            if (doc.isNull()) return;
            QJsonObject o = doc.object();
            int credits = o["api_credits"].toInt();
            int used = o["api_credits_used"].toInt();
            if (credits > 0) globalFxMeta().twelve_remaining = credits - used;
        });
    }

    // 拉 USD/CNY、USD/CAD 两个货币对，算出 CAD/CNY；全成功才算，否则继续降级
    void fetchTwelveForex(const QString& key) {
        struct State { int done = 0; int ok = 0; double usd_cny = 0; double usd_cad = 0; };
        auto state = std::make_shared<State>();

        auto finish = [state, this]() {
            if (state->ok == 2 && state->usd_cny > 0 && state->usd_cad > 0) {
                globalFxRates().usd_to_cny = state->usd_cny;
                globalFxRates().usd_to_cad = state->usd_cad;
                globalFxRates().cad_to_cny = state->usd_cny / state->usd_cad;
                globalFxMeta().source = FxSource::Twelve;
            } else {
                fetchOpenErApi();
            }
        };

        fetchTwelvePair("USD/CNY", key, [state, finish](bool ok, double v) {
            if (ok) { state->usd_cny = v; state->ok++; }
            if (++(state->done) == 2) finish();
        });
        fetchTwelvePair("USD/CAD", key, [state, finish](bool ok, double v) {
            if (ok) { state->usd_cad = v; state->ok++; }
            if (++(state->done) == 2) finish();
        });
    }

    void fetchTwelvePair(const QString& symbol, const QString& key,
                         const std::function<void(bool, double)>& cb) {
        QUrl url("https://api.twelvedata.com/quote");
        QUrlQuery q;
        q.addQueryItem("symbol", symbol);
        q.addQueryItem("apikey", key);
        url.setQuery(q);

        client_->get(url.toString(), [cb](bool ok, const QByteArray& body) {
            if (!ok) { cb(false, 0); return; }
            QJsonDocument doc = QJsonDocument::fromJson(body);
            if (doc.isNull()) { cb(false, 0); return; }
            QJsonObject o = doc.object();
            if (o.contains("status") && o["status"].toString() == "error") { cb(false, 0); return; }
            double close = o["close"].toString().toDouble();
            if (close > 0) { cb(true, close); return; }
            cb(false, 0);
        });
    }

    // ---- 最终兜底：open.er-api（日更） ----
    void fetchOpenErApi() {
        const QString url = "https://open.er-api.com/v6/latest/USD";
        client_->get(url, [](bool ok, const QByteArray& body) {
            if (!ok) return;
            QJsonDocument doc = QJsonDocument::fromJson(body);
            if (doc.isNull()) return;
            QJsonObject o = doc.object();
            if (o["result"].toString() != "success") return;
            QJsonObject rates = o["rates"].toObject();
            double cny = rates["CNY"].toDouble();
            double cad = rates["CAD"].toDouble();
            if (cny > 0) globalFxRates().usd_to_cny = cny;
            if (cad > 0) globalFxRates().usd_to_cad = cad;
            if (cny > 0 && cad > 0) globalFxRates().cad_to_cny = cny / cad;
            globalFxMeta().source = FxSource::OpenErApi;
        });
    }

    HttpClient* client_;
};

// 全局入口：main 里创建并赋值；页面用到汇率时调用 requestFxRefresh()
inline FxService* g_fxService = nullptr;
inline void requestFxRefresh() { if (g_fxService) g_fxService->fetchNow(); }
