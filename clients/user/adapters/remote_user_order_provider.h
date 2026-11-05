#pragma once

#include "adapters/user_order_provider.h"
#include "network/service_client.h"

#include <QPointer>

class RemoteUserOrderProvider final : public IUserOrderProvider
{
    Q_OBJECT
public:
    explicit RemoteUserOrderProvider(QString host, quint16 port,
                                     QObject *parent = nullptr);
    // The injected client is non-owning and must be dedicated to this provider.
    explicit RemoteUserOrderProvider(ServiceClient *client, QObject *parent = nullptr);

    void reserve(qint64 userId, qint64 pileId) override;
    void start(const QString &orderId) override;
    void stop(const QString &orderId) override;
    void cancel(const QString &orderId) override;
    void pay(const QString &orderId) override;
    void recharge(qint64 userId, const QString &amountYuan) override;
    void setUserToken(const QString &userToken) override;
    void queryActive(qint64 userId) override;
    void queryDetail(const QString &orderId) override;
    void queryHistory(qint64 userId, int limit = 20, int offset = 0) override;

private:
    void connectClient();
    enum class Pending { None, Write, Active, Detail, History };
    void sendWrite(UserOrderAction operation, const QString &action,
                   const QJsonObject &payload);
    void sendQuery(Pending pending, UserOrderQueryKind query, const QString &action,
                   const QJsonObject &payload);
    void handleResponse(const QJsonObject &response);
    void handleFailure(ServiceClientError error, const QString &message);

    QPointer<ServiceClient> client_;
    UserOrderAction pendingAction_ = UserOrderAction::Reserve;
    UserOrderQueryKind pendingQuery_ = UserOrderQueryKind::Detail;
    Pending pending_ = Pending::None;
    QString userToken_;
};
