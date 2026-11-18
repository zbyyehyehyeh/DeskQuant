#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QInputDialog>
#include <QMessageBox>
#include <QLineEdit>
#include <QWidget>
#include <QSslError>
#include <functional>

// =============================================================================
// 极简客户端：把策略上传到 QuantPlatform server（HTTPS）。
//
// 接口：POST /upload?name=<策略名>，body 为 Python 源码（text/plain）。
// 响应：200 "OK <name>" / 400 "ERR <原因>"。
// 自签证书：这里直接忽略 SSL 校验；生产环境应信任正式 CA 证书并启用校验。
// =============================================================================
class ServerClient : public QObject {
public:
    explicit ServerClient(QObject* parent = nullptr) : QObject(parent) {
        nam_ = new QNetworkAccessManager(this);
    }

    // baseUrl 形如 "https://127.0.0.1:9100"（不带尾部斜杠）
    void upload(const QString& baseUrl, const QString& name, const QString& code,
                const std::function<void(bool, const QString&)>& cb) {
        QUrl url(baseUrl.trimmed());
        url.setPath(url.path() + "/upload");
        QUrlQuery q;
        q.addQueryItem("name", name);
        url.setQuery(q);

        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain; charset=utf-8");
        req.setTransferTimeout(8000);

        QNetworkReply* reply = nam_->post(req, code.toUtf8());

        connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QByteArray body = reply->readAll();
            reply->deleteLater();
            cb(status == 200, QString::fromUtf8(body));
        });

        // 自签证书：忽略校验（生产环境请信任正式 CA 证书并删除这段）
        connect(reply, &QNetworkReply::sslErrors, this, [reply](const QList<QSslError>&) {
            reply->ignoreSslErrors();
        });
    }

private:
    QNetworkAccessManager* nam_;
};

// 弹框询问服务器地址并上传（name 已在调用前取好）。结果用 QMessageBox 显示。
inline void uploadStrategyToServer(QWidget* parent, const QString& name, const QString& code) {
    bool ok = false;
    QString baseUrl = QInputDialog::getText(parent, "上传到服务器",
                                            "服务器地址（https://host:port）:",
                                            QLineEdit::Normal, "https://127.0.0.1:9100", &ok);
    if (!ok || baseUrl.trimmed().isEmpty()) return;

    auto* client = new ServerClient();   // 无父对象，回调完成后 deleteLater 自释放
    client->upload(baseUrl.trimmed(), name, code, [client](bool ok, const QString& msg) {
        if (ok) QMessageBox::information(nullptr, "上传", "上传成功：" + msg);
        else    QMessageBox::warning(nullptr, "上传失败", msg);
        client->deleteLater();
    });
}
