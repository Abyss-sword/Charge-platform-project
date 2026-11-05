#pragma once

#include <QSqlDatabase>
#include <QString>

// 用户会话校验结果：ok=false 时 errorCode=AUTH_INVALID/USER_FROZEN 等。
struct UserSessionResult
{
    bool ok = false;
    QString errorCode;
    qint64 userId = 0;
    bool restricted = false;
};

// UserSessionService：教学用户会话颁发与校验（迁移 007 user_session 表）。
// 设计与 AdminService 会话一致：随机 32 字节 Token，库存 SHA-256 摘要，8 小时有效；
// 客户端只在内存持有明文。复用管理员会话的成熟做法，不引入自制加密。
class UserSessionService
{
public:
    explicit UserSessionService(QSqlDatabase database);

    // 颁发会话：生成随机 Token，写摘要+过期时间（UTC，8 小时）。
    // 返回空串表示失败。过期会话清理顺带完成。
    QString issue(qint64 userId, QString *expiresAtUtc = nullptr);
    // 校验 Token：命中未撤销、未过期的会话返回用户 id 与受限标志；
    // 无/过期/撤销一律 AUTH_INVALID（错误信息不区分原因，避免被探测）。
    UserSessionResult verify(const QString &token) const;

private:
    QSqlDatabase database_;
};
