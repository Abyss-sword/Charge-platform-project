#pragma once

#include "shared/domain/decimal_value.h"

#include <QSqlDatabase>
#include <QString>

#include <optional>

// 用户记录（余额为两位小数 DecimalValue，status 为 NORMAL/FROZEN）。
struct UserRecord
{
    qint64 id = 0;
    QString phone;
    QString nickname;
    DecimalValue balanceYuan;
    QString status;
    QString avatarUrl;  // S1-C：服务端本地头像路径（重登录回读来源）。
};

// UserRepository：user_info 与登录防重的数据访问（以传入的 QSqlDatabase 连接执行 SQL，
// 非外部 ORM；查询用参数绑定，金额一律 DecimalValue 规范文本解析）。
class UserRepository
{
public:
    explicit UserRepository(QSqlDatabase database);

    // 按手机号 / 主键查询用户。
    std::optional<UserRecord> findByPhone(const QString &phone, QString *error = nullptr) const;
    std::optional<UserRecord> findById(qint64 userId, QString *error = nullptr) const;
    // 新用户注册：BEGIN IMMEDIATE 事务内“建用户(赠 100.00 元) + 写 INITIAL_CREDIT 流水 +
    // 记录登录防重”三步一起提交；若事务内发现已被并发注册则复用现有用户。
    std::optional<UserRecord> createWithInitialCredit(const QString &phone,
                                                       const QString &nickname,
                                                       const QString &requestId,
                                                       QString *error = nullptr);
    // 写入登录防重记录（request_dedup，action='LOGIN_OR_REGISTER'，结果存 JSON）。
    bool recordLoginRequest(const QString &requestId, const QString &phone,
                            qint64 userId, QString *error = nullptr);

private:
    QSqlDatabase database_;
};
