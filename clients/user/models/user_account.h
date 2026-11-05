#pragma once

#include <QMetaType>
#include <QString>
#include <QtGlobal>

enum class UserAccountResultKind
{
    Success,
    Error,
    Offline
};

struct UserProfile
{
    qint64 userId = 0;
    QString phoneMasked;
    QString nickname;
    QString balanceYuan;
    QString status;
    QString avatarPath;  // S1-C：服务端回读的头像路径（重登录/重启后渲染来源）。
};

struct UserProfileQueryResult
{
    UserAccountResultKind kind = UserAccountResultKind::Error;
    QString message;
    QString errorCode;
    UserProfile profile;
};

struct UserProfileUpdateResult
{
    UserAccountResultKind kind = UserAccountResultKind::Error;
    QString message;
    QString errorCode;
    qint64 userId = 0;
    QString nickname;
};

struct WalletBalanceQueryResult
{
    UserAccountResultKind kind = UserAccountResultKind::Error;
    QString message;
    QString errorCode;
    qint64 userId = 0;
    QString balanceYuan;
};

Q_DECLARE_METATYPE(UserProfileQueryResult)
Q_DECLARE_METATYPE(UserProfileUpdateResult)
Q_DECLARE_METATYPE(WalletBalanceQueryResult)
