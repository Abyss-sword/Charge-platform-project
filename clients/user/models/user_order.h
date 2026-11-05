#pragma once

#include <QList>
#include <QMetaType>
#include <QString>

enum class UserOrderAction
{
    Reserve,
    Start,
    Stop,
    Cancel,
    Pay,
    Recharge
};

enum class UserOrderResultKind
{
    Success,
    Error,
    Offline
};

enum class UserOrderQueryKind
{
    Active,
    Detail,
    History
};

struct UserOrderResult
{
    UserOrderResultKind kind = UserOrderResultKind::Error;
    UserOrderAction action = UserOrderAction::Reserve;
    QString message;
    QString orderId;
    QString energyKwh;
    QString unitPriceYuanPerKwh;
    QString amountYuan;
    QString errorCode;
};

struct UserOrderSnapshot
{
    QString orderId;
    qint64 userId = 0;
    qint64 pileId = 0;
    QString status;
    QString energyKwh;
    QString unitPriceYuanPerKwh;
    QString amountYuan;
    QString reservedUntil;
    QString startedAt;
    QString endedAt;
    QString createdAt;
};

struct UserOrderQueryResult
{
    UserOrderResultKind kind = UserOrderResultKind::Error;
    UserOrderQueryKind query = UserOrderQueryKind::Detail;
    QString message;
    QString errorCode;
    bool hasOrder = false;
    UserOrderSnapshot order;
    QList<UserOrderSnapshot> orders;
};

Q_DECLARE_METATYPE(UserOrderResult)
Q_DECLARE_METATYPE(UserOrderSnapshot)
Q_DECLARE_METATYPE(UserOrderQueryResult)
