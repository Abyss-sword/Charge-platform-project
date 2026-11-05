#pragma once

#include "models/user_account.h"

#include <QObject>

class IUserAccountProvider : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~IUserAccountProvider() override = default;

    virtual void queryProfile(qint64 userId) = 0;
    virtual void queryBalance(qint64 userId) = 0;
    virtual void setUserToken(const QString &) {}
    virtual void updateProfile(qint64 userId, const QString &nickname,
                               const QString &avatarPath = {})
    {
        Q_UNUSED(userId); Q_UNUSED(nickname); Q_UNUSED(avatarPath);
        emit profileUpdateFinished({UserAccountResultKind::Error,
                                     QStringLiteral("资料更新服务不可用。"),
                                     QStringLiteral("UNAVAILABLE"), 0, {}});
    }

signals:
    void profileFinished(const UserProfileQueryResult &result);
    void balanceFinished(const WalletBalanceQueryResult &result);
    void profileUpdateFinished(const UserProfileUpdateResult &result);
    void connectivityRestored();
    void connectivityLost();
};
