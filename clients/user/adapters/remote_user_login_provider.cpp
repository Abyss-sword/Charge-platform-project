#include "adapters/remote_user_login_provider.h"

#include <QJsonObject>
#include <QJsonValue>

#include <limits>
#include <utility>

namespace {

LoginResult errorResult(const QString &message)
{
    return {LoginResultKind::Error, message, 0, {}, false, {}, {}};
}

bool readInteger(const QJsonValue &value, qint64 *result)
{
    if (!value.isDouble()) {
        return false;
    }

    constexpr qint64 invalid = std::numeric_limits<qint64>::min();
    const qint64 integer = value.toInteger(invalid);
    if (integer == invalid && value != QJsonValue(invalid)) {
        return false;
    }
    if (value != QJsonValue(integer)) {
        return false;
    }

    *result = integer;
    return true;
}

QString failureMessage(const QJsonObject &response)
{
    const QString errorMessage = response.value(QStringLiteral("error"))
                                     .toObject()
                                     .value(QStringLiteral("message"))
                                     .toString();
    if (!errorMessage.isEmpty()) {
        return errorMessage;
    }

    const QString responseMessage = response.value(QStringLiteral("message")).toString();
    return responseMessage.isEmpty() ? QStringLiteral("登录失败，请稍后重试。")
                                     : responseMessage;
}

QString defaultTransportMessage(ServiceClientError error)
{
    switch (error) {
    case ServiceClientError::Offline:
        return QStringLiteral("无法连接服务器，请检查网络。");
    case ServiceClientError::Disconnected:
        return QStringLiteral("与服务器的连接已断开。");
    case ServiceClientError::Timeout:
        return QStringLiteral("登录请求超时，请稍后重试。");
    case ServiceClientError::MalformedJson:
    case ServiceClientError::MalformedEnvelope:
    case ServiceClientError::RequestIdMismatch:
    case ServiceClientError::ProtocolError:
        return QStringLiteral("服务器响应异常，请稍后重试。");
    }
    return QStringLiteral("登录失败，请稍后重试。");
}

} // namespace

RemoteUserLoginProvider::RemoteUserLoginProvider(QString host, quint16 port, QObject *parent)
    : IUserLoginProvider(parent)
    , client_(new ServiceClient(std::move(host), port, this))
{
    connectClient();
}

RemoteUserLoginProvider::RemoteUserLoginProvider(ServiceClient *client, QObject *parent)
    : IUserLoginProvider(parent)
    , client_(client)
{
    connectClient();
}

void RemoteUserLoginProvider::submit(const LoginRequest &request)
{
    if (!client_) {
        emit loginFinished(errorResult(QStringLiteral("登录服务不可用。")));
        return;
    }
    if (waitingForResponse_ || client_->hasPendingRequest()) {
        emit loginFinished(errorResult(QStringLiteral("登录请求正在处理中，请稍候。")));
        return;
    }

    waitingForResponse_ = true;
    const QString requestId = client_->sendRequest(
        QStringLiteral("USER_LOGIN"),
        QJsonObject{{QStringLiteral("phone"), request.phone}});
    if (requestId.isEmpty()) {
        waitingForResponse_ = false;
        emit loginFinished(errorResult(QStringLiteral("登录请求正在处理中，请稍候。")));
    }
}

void RemoteUserLoginProvider::connectClient()
{
    if (!client_) {
        return;
    }

    connect(client_, &ServiceClient::responseReceived,
            this, [this](const QJsonObject &response) {
        if (!waitingForResponse_) {
            return;
        }
        waitingForResponse_ = false;
        handleResponse(response);
    });
    connect(client_, &ServiceClient::requestFailed,
            this, [this](ServiceClientError error, const QString &message) {
        if (!waitingForResponse_) {
            return;
        }
        waitingForResponse_ = false;
        handleFailure(error, message);
    });
    connect(client_, &QObject::destroyed, this, [this] {
        client_.clear();
        if (waitingForResponse_) {
            waitingForResponse_ = false;
            emit loginFinished(errorResult(QStringLiteral("登录服务不可用。")));
        }
    });
}

void RemoteUserLoginProvider::handleResponse(const QJsonObject &response)
{
    if (!response.value(QStringLiteral("ok")).toBool()) {
        emit loginFinished(errorResult(failureMessage(response)));
        return;
    }

    const QJsonObject data = response.value(QStringLiteral("data")).toObject();
    qint64 userId = 0;
    if (!readInteger(data.value(QStringLiteral("userId")), &userId)) {
        emit loginFinished(errorResult(
            QStringLiteral("登录响应数据无效：userId 必须是整数。")));
        return;
    }
    if (!data.value(QStringLiteral("nickname")).isString()) {
        emit loginFinished(errorResult(
            QStringLiteral("登录响应数据无效：nickname 类型错误。")));
        return;
    }
    if (!data.value(QStringLiteral("restricted")).isBool()) {
        emit loginFinished(errorResult(
            QStringLiteral("登录响应数据无效：restricted 类型错误。")));
        return;
    }
    const QJsonValue userToken = data.value(QStringLiteral("userToken"));
    const QJsonValue expiresAt = data.value(QStringLiteral("expiresAt"));
    if (!userToken.isString() || userToken.toString().isEmpty()
        || !expiresAt.isString() || expiresAt.toString().isEmpty()) {
        emit loginFinished(errorResult(
            QStringLiteral("登录响应数据无效：用户会话字段缺失。")));
        return;
    }

    emit loginFinished(LoginResult{
        LoginResultKind::Success,
        response.value(QStringLiteral("message")).toString(),
        userId,
        data.value(QStringLiteral("nickname")).toString(),
        data.value(QStringLiteral("restricted")).toBool(),
        userToken.toString(),
        expiresAt.toString()
    });
}

void RemoteUserLoginProvider::handleFailure(ServiceClientError error,
                                            const QString &message)
{
    const LoginResultKind kind = error == ServiceClientError::Offline
            || error == ServiceClientError::Disconnected
        ? LoginResultKind::Offline
        : LoginResultKind::Error;
    emit loginFinished(LoginResult{
        kind,
        message.isEmpty() ? defaultTransportMessage(error) : message,
        0,
        {},
        false,
        {},
        {}
    });
}
