#include "adapters/remote_user_account_provider.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

#include <limits>
#include <utility>

namespace {
bool readInteger(const QJsonValue &value, qint64 *result)
{
    if (!value.isDouble()) return false;
    constexpr qint64 invalid = std::numeric_limits<qint64>::min();
    const qint64 integer = value.toInteger(invalid);
    if ((integer == invalid && value != QJsonValue(invalid))
        || value != QJsonValue(integer)) return false;
    *result = integer;
    return true;
}

bool readDecimal(const QJsonValue &value, QString *result)
{
    static const QRegularExpression decimal(QStringLiteral("^(0|[1-9]\\d*)\\.\\d{2}$"));
    if (!value.isString() || !decimal.match(value.toString()).hasMatch()) return false;
    *result = value.toString();
    return true;
}

QString responseCode(const QJsonObject &response)
{
    const QString inner = response.value(QStringLiteral("error")).toObject()
                              .value(QStringLiteral("code")).toString();
    return inner.isEmpty() ? response.value(QStringLiteral("code")).toString() : inner;
}

QString responseMessage(const QJsonObject &response)
{
    const QString inner = response.value(QStringLiteral("error")).toObject()
                              .value(QStringLiteral("message")).toString();
    if (!inner.isEmpty()) return inner;
    const QString outer = response.value(QStringLiteral("message")).toString();
    return outer.isEmpty() ? QStringLiteral("账户查询失败，请稍后重试。") : outer;
}

QString transportCode(ServiceClientError error)
{
    switch (error) {
    case ServiceClientError::Timeout: return QStringLiteral("TIMEOUT");
    case ServiceClientError::Offline: return QStringLiteral("OFFLINE");
    case ServiceClientError::Disconnected: return QStringLiteral("DISCONNECTED");
    case ServiceClientError::MalformedJson: return QStringLiteral("MALFORMED_JSON");
    case ServiceClientError::MalformedEnvelope: return QStringLiteral("MALFORMED_ENVELOPE");
    case ServiceClientError::RequestIdMismatch: return QStringLiteral("REQUEST_ID_MISMATCH");
    case ServiceClientError::ProtocolError: return QStringLiteral("PROTOCOL_ERROR");
    }
    return QStringLiteral("PROTOCOL_ERROR");
}
}

RemoteUserAccountProvider::RemoteUserAccountProvider(QString host, quint16 port,
                                                     QObject *parent)
    : IUserAccountProvider(parent)
    , client_(new ServiceClient(std::move(host), port, this))
{
    connectClient();
}

RemoteUserAccountProvider::RemoteUserAccountProvider(ServiceClient *client, QObject *parent)
    : IUserAccountProvider(parent), client_(client)
{
    connectClient();
}

void RemoteUserAccountProvider::setUserToken(const QString &userToken)
{
    userToken_ = userToken;
}

void RemoteUserAccountProvider::queryProfile(qint64 userId)
{
    send(Pending::Profile, QStringLiteral("USER_PROFILE"), userId);
}

void RemoteUserAccountProvider::queryBalance(qint64 userId)
{
    send(Pending::Balance, QStringLiteral("WALLET_BALANCE"), userId);
}

void RemoteUserAccountProvider::updateProfile(qint64 userId, const QString &nickname,
                                               const QString &avatarPath)
{
    if (userId <= 0 || nickname.trimmed().isEmpty()) {
        emit profileUpdateFinished({UserAccountResultKind::Error, QStringLiteral("昵称不能为空。"),
                                    QStringLiteral("PARAM_INVALID"), userId, {}});
        return;
    }
    if (!client_ || pending_ != Pending::None || client_->hasPendingRequest()) {
        emit profileUpdateFinished({UserAccountResultKind::Error, QStringLiteral("账户请求正在处理中。"),
                                    QStringLiteral("REQUEST_BUSY"), userId, {}});
        return;
    }
    pending_ = Pending::ProfileUpdate;
    if (client_->sendRequest(QStringLiteral("USER_PROFILE_UPDATE"),
            {{QStringLiteral("userToken"), userToken_},
             {QStringLiteral("userId"), userId},
                              {QStringLiteral("nickname"), nickname},
                              {QStringLiteral("avatarPath"), avatarPath}}).isEmpty()) {
        pending_ = Pending::None;
        emit profileUpdateFinished({UserAccountResultKind::Error, QStringLiteral("账户服务不可用。"),
                                    QStringLiteral("REQUEST_BUSY"), userId, {}});
    }
}

void RemoteUserAccountProvider::send(Pending pending, const QString &action, qint64 userId)
{
    if (userId <= 0) {
        emitError(pending, UserAccountResultKind::Error, QStringLiteral("PARAM_INVALID"),
                  QStringLiteral("用户标识无效。"));
        return;
    }
    if (!client_) {
        emitError(pending, UserAccountResultKind::Error, QStringLiteral("UNAVAILABLE"),
                  QStringLiteral("账户服务不可用。"));
        return;
    }
    if (pending_ != Pending::None || client_->hasPendingRequest()) {
        emitError(pending, UserAccountResultKind::Error, QStringLiteral("REQUEST_BUSY"),
                  QStringLiteral("账户查询正在处理中。"));
        return;
    }
    pending_ = pending;
    if (client_->sendRequest(action, {{QStringLiteral("userToken"), userToken_},
                                      {QStringLiteral("userId"), userId}}).isEmpty()) {
        pending_ = Pending::None;
        emitError(pending, UserAccountResultKind::Error, QStringLiteral("REQUEST_BUSY"),
                  QStringLiteral("账户查询正在处理中。"));
    }
}

void RemoteUserAccountProvider::connectClient()
{
    if (!client_) return;
    connect(client_, &ServiceClient::connectionStateChanged, this, [this](bool connected) {
        if (connected) emit connectivityRestored(); else emit connectivityLost();
    });
    connect(client_, &ServiceClient::responseReceived, this,
            [this](const QJsonObject &response) {
        if (pending_ == Pending::None) return;
        handleResponse(response);
    });
    connect(client_, &ServiceClient::requestFailed, this,
            [this](ServiceClientError error, const QString &message) {
        if (pending_ == Pending::None) return;
        handleFailure(error, message);
    });
    connect(client_, &QObject::destroyed, this, [this] {
        client_.clear();
        if (pending_ == Pending::None) return;
        const Pending pending = pending_;
        pending_ = Pending::None;
        emitError(pending, UserAccountResultKind::Error, QStringLiteral("UNAVAILABLE"),
                  QStringLiteral("账户服务不可用。"));
    });
}

void RemoteUserAccountProvider::handleResponse(const QJsonObject &response)
{
    const Pending pending = pending_;
    pending_ = Pending::None;
    if (!response.value(QStringLiteral("ok")).toBool()) {
        emitError(pending, UserAccountResultKind::Error, responseCode(response),
                  responseMessage(response));
        return;
    }
    const QJsonObject data = response.value(QStringLiteral("data")).toObject();
    qint64 userId = 0;
    QString balance;
    if (pending == Pending::ProfileUpdate) {
        const QJsonValue responseUserId = data.value(QStringLiteral("userId"));
        const QJsonValue nickname = data.value(QStringLiteral("nickname"));
        if (!readInteger(responseUserId, &userId) || userId <= 0 || !nickname.isString()) {
            emit profileUpdateFinished({UserAccountResultKind::Error,
                                        QStringLiteral("用户资料更新响应无效。"),
                                        QStringLiteral("MALFORMED_DATA"), userId, {}});
            return;
        }
        emit profileUpdateFinished({UserAccountResultKind::Success,
                                    response.value(QStringLiteral("message")).toString(),
                                    response.value(QStringLiteral("code")).toString(), userId,
                                    nickname.toString()});
        return;
    }
    if (!readInteger(data.value(QStringLiteral("userId")), &userId) || userId <= 0
        || !readDecimal(data.value(QStringLiteral("balanceYuan")), &balance)) {
        emitError(pending, UserAccountResultKind::Error,
                  QStringLiteral("MALFORMED_DATA"),
                  QStringLiteral("账户响应中的用户或余额字段无效。"));
        return;
    }
    if (pending == Pending::Balance) {
        emit balanceFinished({UserAccountResultKind::Success,
                              response.value(QStringLiteral("message")).toString(),
                              response.value(QStringLiteral("code")).toString(), userId, balance});
        return;
    }
    const QJsonValue phone = data.value(QStringLiteral("phoneMasked"));
    const QJsonValue nickname = data.value(QStringLiteral("nickname"));
    const QJsonValue status = data.value(QStringLiteral("status"));
    if (!phone.isString() || !nickname.isString() || !status.isString()) {
        emitError(pending, UserAccountResultKind::Error,
                  QStringLiteral("MALFORMED_DATA"),
                  QStringLiteral("用户资料响应字段无效。"));
        return;
    }
    emit profileFinished({UserAccountResultKind::Success,
                          response.value(QStringLiteral("message")).toString(),
                          response.value(QStringLiteral("code")).toString(),
                          {userId, phone.toString(), nickname.toString(), balance,
                           status.toString(),
                           data.value(QStringLiteral("avatarUrl")).toString()}});
}

void RemoteUserAccountProvider::handleFailure(ServiceClientError error,
                                              const QString &message)
{
    const Pending pending = pending_;
    pending_ = Pending::None;
    const bool offline = error == ServiceClientError::Offline
        || error == ServiceClientError::Disconnected;
    emitError(pending, offline ? UserAccountResultKind::Offline
                               : UserAccountResultKind::Error,
              transportCode(error),
              message.isEmpty() ? (offline ? QStringLiteral("无法连接账户服务。")
                                           : QStringLiteral("账户查询失败，请稍后重试。"))
                                : message);
}

void RemoteUserAccountProvider::emitError(Pending pending, UserAccountResultKind kind,
                                          const QString &code, const QString &message)
{
    if (pending == Pending::Profile)
        emit profileFinished({kind, message, code, {}});
    else if (pending == Pending::Balance)
        emit balanceFinished({kind, message, code, 0, {}});
    else if (pending == Pending::ProfileUpdate)
        emit profileUpdateFinished({kind, message, code, 0, {}});
}
