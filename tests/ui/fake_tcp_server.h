#pragma once

#include "shared/protocol/frame_codec.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>

#include <functional>

class FakeTcpServer final : public QObject
{
public:
    using RequestHandler = std::function<void(QTcpSocket *, const QJsonObject &)>;

    explicit FakeTcpServer(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            client_ = server_.nextPendingConnection();
            codec_.reset();
            connect(client_, &QTcpSocket::readyRead, this, [this] {
                const QList<QByteArray> frames = codec_.feed(client_->readAll());
                for (const QByteArray &frame : frames) {
                    const QJsonDocument document = QJsonDocument::fromJson(frame);
                    if (!document.isObject()) {
                        continue;
                    }
                    requests.append(document.object());
                    if (handler) {
                        handler(client_, requests.constLast());
                    }
                }
            });
        });
    }

    bool listen(quint16 port = 0)
    {
        return server_.listen(QHostAddress::LocalHost, port);
    }

    void close() { server_.close(); }

    quint16 port() const { return server_.serverPort(); }
    QTcpSocket *client() const { return client_; }

    static QByteArray frame(const QJsonObject &object)
    {
        return FrameCodec::encode(QJsonDocument(object).toJson(QJsonDocument::Compact));
    }

    static QJsonObject response(const QString &requestId,
                                const QJsonObject &data = {})
    {
        return {
            {QStringLiteral("requestId"), requestId},
            {QStringLiteral("ok"), true},
            {QStringLiteral("code"), QStringLiteral("OK")},
            {QStringLiteral("message"), QStringLiteral("success")},
            {QStringLiteral("data"), data}
        };
    }

    QList<QJsonObject> requests;
    RequestHandler handler;

private:
    QTcpServer server_;
    QPointer<QTcpSocket> client_;
    FrameCodec codec_;
};
