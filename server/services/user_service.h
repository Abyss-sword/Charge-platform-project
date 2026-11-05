#pragma once

#include <QSqlDatabase>
#include <QString>

#include "shared/domain/decimal_value.h"

// 登录结果：restricted 表示账号被冻结（仍可登录查看，但业务操作受限）。
// M1-02 起：登录成功附带 userToken（明文仅此一次返回，库存摘要）与 UTC expiresAt。
struct LoginResult
{
    bool ok = false;
    QString errorCode;
    qint64 userId = 0;
    QString nickname;
    bool restricted = false;
    QString userToken;
    QString expiresAtUtc;
};

// 资料查询结果：手机号打码（138****1234）输出，余额为两位小数的 DecimalValue。
struct UserProfileResult
{
    bool ok = false;
    QString errorCode;
    qint64 userId = 0;
    QString phoneMasked;
    QString nickname;
    DecimalValue balanceYuan;
    QString status;
    QString avatarUrl;  // S1-C：服务端回读的头像路径（未设置时为空）。
};

// UserService：车主账号业务——教学模拟登录（手机号即身份，无短信验证码）
// 与资料查询。新手机号自动注册并赠送 100.00 元初始余额（同一事务写入流水）。
// 登录防重：同一 requestId 且同手机号回放原结果；重放不重复颁发新额度，
// 但会重新颁发会话 Token（令牌本身不是业务写操作，换 token≠新充值意图）。
class UserService
{
public:
    explicit UserService(QSqlDatabase database);
    // 登录或自动注册；requestId 兼做登录防重（同一编号重复请求且参数一致 → 返回原结果）。
    LoginResult loginOrRegister(const QString &phone, const QString &requestId);
    // 按 userId 查询资料，手机号掩码输出。
    UserProfileResult profile(qint64 userId) const;

private:
    QSqlDatabase database_;
};
