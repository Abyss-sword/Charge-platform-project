#include "network/service_client.h"

#include <QAbstractSocket>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUuid>

#include <utility>

namespace {

bool hasString(const QJsonObject &object, const QString &key)
{
    return object.contains(key) && object.value(key).isString();
}

QString validateEnvelope(const QJsonObject &object)
{
    if (!hasString(object, QStringLiteral("requestId"))
        || object.value(QStringLiteral("requestId")).toString().isEmpty()) {
        return QStringLiteral("response requestId is missing or invalid");
    }
    if (!object.contains(QStringLiteral("ok"))
        || !object.value(QStringLiteral("ok")).isBool()) {
        return QStringLiteral("response ok is missing or invalid");
    }
    if (!hasString(object, QStringLiteral("code"))) {
        return QStringLiteral("response code is missing or invalid");
    }
    if (!hasString(object, QStringLiteral("message"))) {
        return QStringLiteral("response message is missing or invalid");
    }
    if (!object.contains(QStringLiteral("data"))
        || !object.value(QStringLiteral("data")).isObject()) {
        return QStringLiteral("response data is missing or invalid");
    }

    const QJsonValue error = object.value(QStringLiteral("error"));
    if (!error.isUndefined()) {
        if (!error.isObject()) {
            return QStringLiteral("response error is invalid");
        }
        const QJsonObject errorObject = error.toObject();
        if (!hasString(errorObject, QStringLiteral("code"))
            || !hasString(errorObject, QStringLiteral("message"))) {
            return QStringLiteral("response error fields are invalid");
        }
    }
    return {};
}

} // namespace

ServiceClient::ServiceClient(QString host, quint16 port, QObject *parent)
    : QObject(parent)
    , host_(std::move(host))
    , port_(port)
{
    timeoutTimer_.setSingleShot(true);
    reconnectTimer_.setSingleShot(true);
    reconnectTimer_.setInterval(1000);

    connect(&socket_, &QTcpSocket::connected, this, [this] {
        reconnectTimer_.stop();
        if (!connected_) {
            connected_ = true;
            if (connectionLost_) {
                connectionLost_ = false;
                emit connectionStateChanged(true);
            }
        }
        writePendingRequest();
    });
    connect(&socket_, &QTcpSocket::readyRead, this, [this] {
        const QList<QByteArray> frames = codec_.feed(socket_.readAll());
        if (codec_.hasProtocolError()) {
            fail(ServiceClientError::ProtocolError, codec_.lastError());
            return;
        }
        for (const QByteArray &frame : frames) {
            if (!pending_) {
                break;
            }
            handleFrame(frame);
        }
    });
    connect(&socket_, &QTcpSocket::disconnected, this, [this] {
        noteDisconnected();
        if (pending_) {
            fail(requestSent_ ? ServiceClientError::Disconnected
                              : ServiceClientError::Offline,
                 requestSent_ ? QStringLiteral("server disconnected before responding")
                              : QStringLiteral("unable to connect to server"));
        }
    });
    connect(&socket_, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
        noteDisconnected();
        if (!pending_) {
            return;
        }
        fail(requestSent_ ? ServiceClientError::Disconnected
                          : ServiceClientError::Offline,
             socket_.errorString());
    });
    connect(&timeoutTimer_, &QTimer::timeout, this, [this] {
        fail(ServiceClientError::Timeout, QStringLiteral("request timed out"));
    });
    connect(&reconnectTimer_, &QTimer::timeout, this, [this] {
        if (!pending_ && socket_.state() == QAbstractSocket::UnconnectedState)
            socket_.connectToHost(host_, port_);
    });
}

ServiceClient::~ServiceClient()
{
    disconnect(&socket_, nullptr, this, nullptr);
    timeoutTimer_.stop();
    reconnectTimer_.stop();
    socket_.abort();
}

void ServiceClient::setRequestTimeout(int timeoutMs)
{
    if (timeoutMs > 0) {
        requestTimeoutMs_ = timeoutMs;
    }
}

void ServiceClient::setReconnectInterval(int intervalMs)
{
    if (intervalMs > 0) reconnectTimer_.setInterval(intervalMs);
}

QString ServiceClient::sendRequest(const QString &action, const QJsonObject &payload)
{
    if (pending_) {
        return {};
    }

    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    pendingRequestId_ = requestId;
    const QJsonObject envelope{
        {QStringLiteral("version"), QStringLiteral("1.0")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("action"), action},
        {QStringLiteral("payload"), payload}
    };
    const QByteArray body = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    codec_.reset();
    pending_ = true;
    requestSent_ = false;

    if (action.isEmpty()) {
        fail(ServiceClientError::ProtocolError, QStringLiteral("request action is empty"));
        return requestId;
    }
    if (body.isEmpty() || body.size() > static_cast<qsizetype>(FrameCodec::MaxPayload)) {
        fail(ServiceClientError::ProtocolError,
             QStringLiteral("request payload exceeds the protocol limit"));
        return requestId;
    }

    pendingFrame_ = FrameCodec::encode(body);
    timeoutTimer_.start(requestTimeoutMs_);

    if (socket_.state() == QAbstractSocket::ConnectedState) {
        writePendingRequest();
    } else {
        if (socket_.state() != QAbstractSocket::UnconnectedState) {
            socket_.abort();
        }
        socket_.connectToHost(host_, port_);
    }
    return requestId;
}

void ServiceClient::writePendingRequest()
{
    if (!pending_ || requestSent_) {
        return;
    }
    if (socket_.write(pendingFrame_) < 0) {
        fail(ServiceClientError::Disconnected, socket_.errorString());
        return;
    }
    requestSent_ = true;
}

void ServiceClient::handleFrame(const QByteArray &frame)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(frame, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(ServiceClientError::MalformedJson,
             parseError.error == QJsonParseError::NoError
                 ? QStringLiteral("response JSON is not an object")
                 : parseError.errorString());
        return;
    }

    const QJsonObject response = document.object();
    const QString envelopeError = validateEnvelope(response);
    if (!envelopeError.isEmpty()) {
        fail(ServiceClientError::MalformedEnvelope, envelopeError);
        return;
    }
    if (response.value(QStringLiteral("requestId")).toString() != pendingRequestId_) {
        fail(ServiceClientError::RequestIdMismatch,
             QStringLiteral("response requestId does not match the pending request"));
        return;
    }

    finishRequest();
    emit responseReceived(response);
}

void ServiceClient::fail(ServiceClientError error, const QString &message)
{
    if (!pending_) {
        return;
    }
    finishRequest();
    socket_.abort();
    emit requestFailed(error, message);
}

void ServiceClient::finishRequest()
{
    timeoutTimer_.stop();
    codec_.reset();
    pendingFrame_.clear();
    pendingRequestId_.clear();
    pending_ = false;
    requestSent_ = false;
}

void ServiceClient::noteDisconnected()
{
    const bool notify = !connectionLost_;
    connectionLost_ = true;
    if (connected_) {
        connected_ = false;
    }
    if (notify) emit connectionStateChanged(false);
    if (!reconnectTimer_.isActive()) reconnectTimer_.start();
}
