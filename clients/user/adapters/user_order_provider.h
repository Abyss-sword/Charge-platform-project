#pragma once

#include "models/user_order.h"

#include <QObject>

class IUserOrderProvider : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~IUserOrderProvider() override = default;

    virtual void reserve(qint64 userId, qint64 pileId) = 0;
    virtual void start(const QString &orderId) = 0;
    virtual void stop(const QString &orderId) = 0;
    virtual void cancel(const QString &orderId) = 0;
    virtual void pay(const QString &orderId) = 0;
    virtual void recharge(qint64 userId, const QString &amountYuan) = 0;
    virtual void setUserToken(const QString &) {}
    virtual void queryActive(qint64) {}
    virtual void queryDetail(const QString &) {}
    virtual void queryHistory(qint64, int = 20, int = 0) {}

signals:
    void operationFinished(const UserOrderResult &result);
    void queryFinished(const UserOrderQueryResult &result);
    void connectivityRestored();
    void connectivityLost();
};
