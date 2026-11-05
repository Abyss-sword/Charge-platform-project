#pragma once

#include "shared/protocol/frame_codec.h"

#include <QJsonObject>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QTimer>

enum class ServiceClientError
{
    Timeout,
    Offline,
    Disconnected,
    MalformedJson,
    MalformedEnvelope,
    RequestIdMismatch,
    ProtocolError
};

class ServiceClient final : public QObject
{
    Q_OBJECT
public:
    explicit ServiceClient(QString host, quint16 port, QObject *parent = nullptr);
    ~ServiceClient() override;

    void setRequestTimeout(int timeoutMs);
    void setReconnectInterval(int intervalMs);
    int requestTimeout() const noexcept { return requestTimeoutMs_; }
    bool hasPendingRequest() const noexcept { return pending_; }

    // Returns the generated request id, or an empty string when another request is pending.
    QString sendRequest(const QString &action, const QJsonObject &payload);

signals:
    void responseReceived(const QJsonObject &response);
    void requestFailed(ServiceClientError error, const QString &message);
    void connectionStateChanged(bool connected);

private:
    void writePendingRequest();
    void handleFrame(const QByteArray &frame);
    void fail(ServiceClientError error, const QString &message);
    void finishRequest();
    void noteDisconnected();

    QString host_;
    quint16 port_ = 0;
    QTcpSocket socket_;
    QTimer timeoutTimer_;
    QTimer reconnectTimer_;
    FrameCodec codec_;
    QByteArray pendingFrame_;
    QString pendingRequestId_;
    int requestTimeoutMs_ = 10000;
    bool pending_ = false;
    bool requestSent_ = false;
    bool connected_ = false;
    bool connectionLost_ = false;
};

Q_DECLARE_METATYPE(ServiceClientError)
