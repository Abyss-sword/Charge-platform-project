#include "server/repositories/user_repository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
// 行读取辅助：余额文本必须能按规范两位格式解析，否则视为库数据损坏而失败（不静默继续）。
std::optional<UserRecord> readUser(QSqlQuery &query, QString *error)
{
    if (!query.next()) {
        return std::nullopt;
    }
    const auto balance = DecimalValue::parseCanonical(query.value(3).toString());
    if (!balance) {
        if (error) {
            *error = QStringLiteral("invalid balance_yuan in database");
        }
        return std::nullopt;
    }
    if (error) {
        error->clear();
    }
    return UserRecord{query.value(0).toLongLong(), query.value(1).toString(),
                      query.value(2).toString(), *balance, query.value(4).toString(),
                      query.value(5).toString()};
}

void setError(QString *target, const QString &message)
{
    if (target) {
        *target = message;
    }
}
}

UserRepository::UserRepository(QSqlDatabase database)
    : database_(std::move(database))
{
}

std::optional<UserRecord> UserRepository::findByPhone(const QString &phone, QString *error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id,phone,nickname,balance_yuan,status FROM user_info WHERE phone=?"));
    query.addBindValue(phone);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return std::nullopt;
    }
    return readUser(query, error);
}

std::optional<UserRecord> UserRepository::findById(qint64 userId, QString *error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id,phone,nickname,balance_yuan,status,avatar_url FROM user_info WHERE id=?"));
    query.addBindValue(userId);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return std::nullopt;
    }
    return readUser(query, error);
}

bool UserRepository::recordLoginRequest(const QString &requestId, const QString &phone,
                                        qint64 userId, QString *error)
{
    const QByteArray json = QJsonDocument(QJsonObject{
        {QStringLiteral("phone"), phone},
        {QStringLiteral("userId"), userId}
    }).toJson(QJsonDocument::Compact);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO request_dedup(request_id,action,result_json) VALUES(?,'LOGIN_OR_REGISTER',?)"));
    query.addBindValue(requestId);
    query.addBindValue(QString::fromUtf8(json));
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

// 注册 + 赠送初始余额（答辩高频“同一用户/请求不会重复赠送”）：
// BEGIN IMMEDIATE → 事务内复查（防并发重复注册）→ 插入用户（balance '100.00'）→
// 写 INITIAL_CREDIT 流水 → 记录登录防重 → 提交；任一步失败整体回滚，
// 因此赠送与注册同生共死，不会出现“没建户却送了钱”或反之。
std::optional<UserRecord> UserRepository::createWithInitialCredit(
    const QString &phone, const QString &nickname, const QString &requestId, QString *error)
{
    QSqlQuery begin(database_);
    if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
        setError(error, begin.lastError().text());
        return std::nullopt;
    }

    QString lookupError;
    if (const auto existing = findByPhone(phone, &lookupError)) {
        if (!recordLoginRequest(requestId, phone, existing->id, error)
            || !database_.commit()) {
            if (error && error->isEmpty()) {
                *error = database_.lastError().text();
            }
            database_.rollback();
            return std::nullopt;
        }
        return existing;
    }
    if (!lookupError.isEmpty()) {
        setError(error, lookupError);
        database_.rollback();
        return std::nullopt;
    }

    QSqlQuery insertUser(database_);
    insertUser.prepare(QStringLiteral(
        "INSERT INTO user_info(phone,nickname,balance_yuan,status) "
        "VALUES(?,?,'100.00','NORMAL')"));
    insertUser.addBindValue(phone);
    insertUser.addBindValue(nickname);
    if (!insertUser.exec()) {
        setError(error, insertUser.lastError().text());
        database_.rollback();
        return std::nullopt;
    }
    const qint64 userId = insertUser.lastInsertId().toLongLong();

    QSqlQuery credit(database_);
    credit.prepare(QStringLiteral(
        "INSERT INTO wallet_transaction(user_id,transaction_type,amount_yuan,request_id) "
        "VALUES(?,'INITIAL_CREDIT','100.00',?)"));
    credit.addBindValue(userId);
    credit.addBindValue(QStringLiteral("INITIAL-") + requestId);
    if (!credit.exec()) {
        setError(error, credit.lastError().text());
        database_.rollback();
        return std::nullopt;
    }

    if (!recordLoginRequest(requestId, phone, userId, error)) {
        database_.rollback();
        return std::nullopt;
    }

    if (!database_.commit()) {
        setError(error, database_.lastError().text());
        database_.rollback();
        return std::nullopt;
    }
    if (error) {
        error->clear();
    }
    return findByPhone(phone, error);
}
