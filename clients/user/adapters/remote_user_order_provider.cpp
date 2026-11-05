#include "adapters/remote_user_order_provider.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>

#include <limits>
#include <utility>

namespace {

UserOrderResult errorResult(UserOrderAction action, const QString &message,
                            UserOrderResultKind kind = UserOrderResultKind::Error,
                            const QString &code = {})
{
    UserOrderResult result;
    result.kind = kind;
    result.action = action;
    result.message = message;
    result.errorCode = code;
    return result;
}

QString responseCode(const QJsonObject &response)
{
    const QString inner = response.value(QStringLiteral("error")).toObject()
                              .value(QStringLiteral("code")).toString();
    return inner.isEmpty() ? response.value(QStringLiteral("code")).toString() : inner;
}

QString failureMessage(const QJsonObject &response)
{
    const QString inner = response.value(QStringLiteral("error")).toObject()
                              .value(QStringLiteral("message")).toString();
    if (!inner.isEmpty()) return inner;
    const QString outer = response.value(QStringLiteral("message")).toString();
    return outer.isEmpty() ? QStringLiteral("订单操作失败，请稍后重试。") : outer;
}

bool readOrderId(const QJsonObject &data, QString *orderId)
{
    const QJsonValue value = data.value(QStringLiteral("orderId"));
    if (!value.isString() || value.toString().trimmed().isEmpty()) return false;
    *orderId = value.toString();
    return true;
}

bool readDecimal(const QJsonObject &data, const QString &name, QString *value)
{
    static const QRegularExpression decimal(QStringLiteral("^(0|[1-9]\\d*)\\.\\d{2}$"));
    const QJsonValue raw = data.value(name);
    if (!raw.isString() || !decimal.match(raw.toString()).hasMatch()) return false;
    *value = raw.toString();
    return true;
}

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

bool readOptionalString(const QJsonObject &data, const QString &name, QString *value)
{
    const QJsonValue raw = data.value(name);
    if (raw.isNull()) {
        value->clear();
        return true;
    }
    if (!raw.isString()) return false;
    *value = raw.toString();
    return true;
}

bool mapOrder(const QJsonValue &value, UserOrderSnapshot *result)
{
    if (!value.isObject()) return false;
    const QJsonObject data = value.toObject();
    qint64 userId = 0;
    qint64 pileId = 0;
    QString orderId;
    QString energy;
    QString unitPrice;
    QString amount;
    QString reservedUntil;
    QString startedAt;
    QString endedAt;
    QString createdAt;
    if (!readOrderId(data, &orderId)
        || !readInteger(data.value(QStringLiteral("userId")), &userId) || userId <= 0
        || !readInteger(data.value(QStringLiteral("pileId")), &pileId) || pileId <= 0
        || !data.value(QStringLiteral("status")).isString()
        || !readDecimal(data, QStringLiteral("energyKwh"), &energy)
        || !readDecimal(data, QStringLiteral("unitPriceYuanPerKwh"), &unitPrice)
        || !readDecimal(data, QStringLiteral("amountYuan"), &amount)
        || !readOptionalString(data, QStringLiteral("reservedUntil"), &reservedUntil)
        || !readOptionalString(data, QStringLiteral("startedAt"), &startedAt)
        || !readOptionalString(data, QStringLiteral("endedAt"), &endedAt)
        || !readOptionalString(data, QStringLiteral("createdAt"), &createdAt)) return false;
    *result = {orderId, userId, pileId, data.value(QStringLiteral("status")).toString(),
               energy, unitPrice, amount, reservedUntil, startedAt, endedAt, createdAt};
    return true;
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

} // namespace

RemoteUserOrderProvider::RemoteUserOrderProvider(QString host, quint16 port, QObject *parent)
    : IUserOrderProvider(parent)
    , client_(new ServiceClient(std::move(host), port, this))
{
    connectClient();
}

RemoteUserOrderProvider::RemoteUserOrderProvider(ServiceClient *client, QObject *parent)
    : IUserOrderProvider(parent)
    , client_(client)
{
    connectClient();
}

void RemoteUserOrderProvider::setUserToken(const QString &userToken)
{
    userToken_ = userToken;
}

void RemoteUserOrderProvider::reserve(qint64 userId, qint64 pileId)
{
    sendWrite(UserOrderAction::Reserve, QStringLiteral("ORDER_RESERVE"),
         {{QStringLiteral("userId"), userId}, {QStringLiteral("pileId"), pileId}});
}

void RemoteUserOrderProvider::start(const QString &orderId)
{
    sendWrite(UserOrderAction::Start, QStringLiteral("ORDER_START"),
         {{QStringLiteral("orderId"), orderId}});
}

void RemoteUserOrderProvider::stop(const QString &orderId)
{
    sendWrite(UserOrderAction::Stop, QStringLiteral("ORDER_STOP"),
         {{QStringLiteral("orderId"), orderId}});
}

void RemoteUserOrderProvider::cancel(const QString &orderId)
{
    sendWrite(UserOrderAction::Cancel, QStringLiteral("ORDER_CANCEL"),
         {{QStringLiteral("orderId"), orderId}});
}

void RemoteUserOrderProvider::pay(const QString &orderId)
{
    sendWrite(UserOrderAction::Pay, QStringLiteral("WALLET_PAY"),
         {{QStringLiteral("orderId"), orderId}});
}

void RemoteUserOrderProvider::recharge(qint64 userId, const QString &amountYuan)
{
    sendWrite(UserOrderAction::Recharge, QStringLiteral("WALLET_RECHARGE"),
         {{QStringLiteral("userId"), userId},
          {QStringLiteral("amountYuan"), amountYuan}});
}

void RemoteUserOrderProvider::queryActive(qint64 userId)
{
    sendQuery(Pending::Active, UserOrderQueryKind::Active, QStringLiteral("ORDER_ACTIVE"),
              {{QStringLiteral("userId"), userId}});
}

void RemoteUserOrderProvider::queryDetail(const QString &orderId)
{
    sendQuery(Pending::Detail, UserOrderQueryKind::Detail, QStringLiteral("ORDER_DETAIL"),
              {{QStringLiteral("orderId"), orderId}});
}

void RemoteUserOrderProvider::queryHistory(qint64 userId, int limit, int offset)
{
    sendQuery(Pending::History, UserOrderQueryKind::History, QStringLiteral("ORDER_HISTORY"),
              {{QStringLiteral("userId"), userId}, {QStringLiteral("limit"), limit},
               {QStringLiteral("offset"), offset}});
}

void RemoteUserOrderProvider::connectClient()
{
    if (!client_) return;
    connect(client_, &ServiceClient::connectionStateChanged, this,
            [this](bool connected) {
        if (connected) emit connectivityRestored();
        else emit connectivityLost();
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
        if (pending == Pending::Write) {
            emit operationFinished(errorResult(pendingAction_,
                                                QStringLiteral("订单服务不可用。"),
                                                UserOrderResultKind::Error,
                                                QStringLiteral("UNAVAILABLE")));
        } else {
            emit queryFinished({UserOrderResultKind::Error, pendingQuery_,
                                QStringLiteral("订单服务不可用。"),
                                QStringLiteral("UNAVAILABLE"), false, {}, {}});
        }
    });
}

void RemoteUserOrderProvider::sendWrite(UserOrderAction operation, const QString &action,
                                        const QJsonObject &payload)
{
    if (!client_) {
        emit operationFinished(errorResult(operation, QStringLiteral("订单服务不可用。")));
        return;
    }
    if (pending_ != Pending::None || client_->hasPendingRequest()) {
        emit operationFinished(errorResult(operation,
                                            QStringLiteral("已有订单操作正在处理中。")));
        return;
    }
    pendingAction_ = operation;
    pending_ = Pending::Write;
    QJsonObject authenticatedPayload = payload;
    authenticatedPayload.insert(QStringLiteral("userToken"), userToken_);
    if (client_->sendRequest(action, authenticatedPayload).isEmpty()) {
        pending_ = Pending::None;
        emit operationFinished(errorResult(operation,
                                            QStringLiteral("已有订单操作正在处理中。")));
    }
}

void RemoteUserOrderProvider::sendQuery(Pending pending, UserOrderQueryKind query,
                                        const QString &action, const QJsonObject &payload)
{
    if (!client_) {
        emit queryFinished({UserOrderResultKind::Error, query,
                            QStringLiteral("订单服务不可用。"),
                            QStringLiteral("UNAVAILABLE"), false, {}, {}});
        return;
    }
    if (pending_ != Pending::None || client_->hasPendingRequest()) {
        emit queryFinished({UserOrderResultKind::Error, query,
                            QStringLiteral("已有订单请求正在处理中。"),
                            QStringLiteral("REQUEST_BUSY"), false, {}, {}});
        return;
    }
    pending_ = pending;
    pendingQuery_ = query;
    QJsonObject authenticatedPayload = payload;
    authenticatedPayload.insert(QStringLiteral("userToken"), userToken_);
    if (client_->sendRequest(action, authenticatedPayload).isEmpty()) {
        pending_ = Pending::None;
        emit queryFinished({UserOrderResultKind::Error, query,
                            QStringLiteral("已有订单请求正在处理中。"),
                            QStringLiteral("REQUEST_BUSY"), false, {}, {}});
    }
}

void RemoteUserOrderProvider::handleResponse(const QJsonObject &response)
{
    const Pending pending = pending_;
    pending_ = Pending::None;
    if (!response.value(QStringLiteral("ok")).toBool()) {
        if (pending == Pending::Write) {
            emit operationFinished(errorResult(pendingAction_, failureMessage(response),
                                                UserOrderResultKind::Error,
                                                responseCode(response)));
        } else {
            emit queryFinished({UserOrderResultKind::Error, pendingQuery_,
                                failureMessage(response), responseCode(response),
                                false, {}, {}});
        }
        return;
    }

    const QJsonObject data = response.value(QStringLiteral("data")).toObject();
    if (pending != Pending::Write) {
        UserOrderQueryResult queryResult;
        queryResult.kind = UserOrderResultKind::Success;
        queryResult.query = pendingQuery_;
        queryResult.message = response.value(QStringLiteral("message")).toString();
        queryResult.errorCode = response.value(QStringLiteral("code")).toString();
        if (pending == Pending::Active) {
            const QJsonValue order = data.value(QStringLiteral("order"));
            if (!order.isObject()) {
                queryResult.kind = UserOrderResultKind::Error;
                queryResult.errorCode = QStringLiteral("MALFORMED_DATA");
                queryResult.message = QStringLiteral("活动订单响应格式无效。");
            } else if (!order.toObject().isEmpty()) {
                queryResult.hasOrder = mapOrder(order, &queryResult.order);
                if (!queryResult.hasOrder) {
                    queryResult.kind = UserOrderResultKind::Error;
                    queryResult.errorCode = QStringLiteral("MALFORMED_DATA");
                    queryResult.message = QStringLiteral("活动订单字段无效。");
                }
            }
        } else if (pending == Pending::Detail) {
            queryResult.hasOrder = mapOrder(data, &queryResult.order);
            if (!queryResult.hasOrder) {
                queryResult.kind = UserOrderResultKind::Error;
                queryResult.errorCode = QStringLiteral("MALFORMED_DATA");
                queryResult.message = QStringLiteral("订单详情字段无效。");
            }
        } else {
            const QJsonValue rows = data.value(QStringLiteral("rows"));
            if (!rows.isArray()) {
                queryResult.kind = UserOrderResultKind::Error;
                queryResult.errorCode = QStringLiteral("MALFORMED_DATA");
                queryResult.message = QStringLiteral("历史订单响应缺少 rows 数组。");
            } else {
                for (const QJsonValue &row : rows.toArray()) {
                    UserOrderSnapshot order;
                    if (!mapOrder(row, &order)) {
                        queryResult.kind = UserOrderResultKind::Error;
                        queryResult.errorCode = QStringLiteral("MALFORMED_DATA");
                        queryResult.message = QStringLiteral("历史订单字段无效。");
                        queryResult.orders.clear();
                        break;
                    }
                    queryResult.orders.append(order);
                }
            }
        }
        emit queryFinished(queryResult);
        return;
    }

    UserOrderResult result;
    result.kind = UserOrderResultKind::Success;
    result.action = pendingAction_;
    result.message = response.value(QStringLiteral("message")).toString();
    result.errorCode = response.value(QStringLiteral("code")).toString();
    if (pendingAction_ == UserOrderAction::Pay
        || pendingAction_ == UserOrderAction::Recharge) {
        emit operationFinished(result);
        return;
    }
    if (!readOrderId(data, &result.orderId)) {
        emit operationFinished(errorResult(pendingAction_,
                                            QStringLiteral("订单响应缺少有效 orderId。")));
        return;
    }
    if (pendingAction_ != UserOrderAction::Reserve
        && (!readDecimal(data, QStringLiteral("energyKwh"), &result.energyKwh)
            || !readDecimal(data, QStringLiteral("unitPriceYuanPerKwh"),
                            &result.unitPriceYuanPerKwh)
            || !readDecimal(data, QStringLiteral("amountYuan"), &result.amountYuan))) {
        emit operationFinished(errorResult(
            pendingAction_, QStringLiteral("订单响应的电量或金额字段无效。")));
        return;
    }
    emit operationFinished(result);
}

void RemoteUserOrderProvider::handleFailure(ServiceClientError error,
                                            const QString &message)
{
    const Pending pending = pending_;
    pending_ = Pending::None;
    const bool offline = error == ServiceClientError::Offline
        || error == ServiceClientError::Disconnected;
    const QString text = message.isEmpty()
        ? (offline ? QStringLiteral("无法连接订单服务。")
                   : QStringLiteral("订单请求失败，请稍后重试。"))
        : message;
    const UserOrderResultKind kind = offline ? UserOrderResultKind::Offline
                                              : UserOrderResultKind::Error;
    if (pending == Pending::Write) {
        emit operationFinished(errorResult(pendingAction_, text, kind, transportCode(error)));
    } else {
        emit queryFinished({kind, pendingQuery_, text, transportCode(error), false, {}, {}});
    }
}
