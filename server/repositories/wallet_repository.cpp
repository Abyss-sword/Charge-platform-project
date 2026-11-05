#include "server/repositories/wallet_repository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <utility>

namespace {
void setError(QString *target, const QString &message)
{
    if (target) {
        *target = message;
    }
}

void clearError(QString *target)
{
    if (target) {
        target->clear();
    }
}

bool execChanged(QSqlQuery &query, QString *error)
{
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    clearError(error);
    return query.numRowsAffected() == 1;
}
}

WalletRepository::WalletRepository(QSqlDatabase database)
    : database_(std::move(database))
{
}

std::optional<WalletRequestRecord> WalletRepository::findRequest(
    const QString &requestId, QString *error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT action,result_json FROM request_dedup WHERE request_id=?"));
    query.addBindValue(requestId);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return std::nullopt;
    }
    clearError(error);
    if (!query.next()) {
        return std::nullopt;
    }
    return WalletRequestRecord{query.value(0).toString(), query.value(1).toString()};
}

std::optional<WalletOrderRecord> WalletRepository::findOrder(
    const QString &orderId, QString *error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id,user_id,pile_id,status,amount_yuan FROM charge_order WHERE id=?"));
    query.addBindValue(orderId);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) {
        clearError(error);
        return std::nullopt;
    }
    const auto amount = DecimalValue::parseCanonical(query.value(4).toString());
    if (!amount) {
        setError(error, QStringLiteral("invalid amount_yuan in database"));
        return std::nullopt;
    }
    clearError(error);
    return WalletOrderRecord{query.value(0).toString(), query.value(1).toLongLong(),
                             query.value(2).toLongLong(), query.value(3).toString(),
                             *amount};
}

std::optional<DecimalValue> WalletRepository::balanceYuan(qint64 userId,
                                                           QString *error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT balance_yuan FROM user_info WHERE id=?"));
    query.addBindValue(userId);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) {
        clearError(error);
        return std::nullopt;
    }
    const auto balance = DecimalValue::parseCanonical(query.value(0).toString());
    if (!balance) {
        setError(error, QStringLiteral("invalid balance_yuan in database"));
        return std::nullopt;
    }
    clearError(error);
    return balance;
}

bool WalletRepository::recordRequest(const QString &requestId, const QString &action,
                                     const QString &resultJson, QString *error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO request_dedup(request_id,action,result_json) VALUES(?,?,?)"));
    query.addBindValue(requestId);
    query.addBindValue(action);
    query.addBindValue(resultJson);
    return execChanged(query, error);
}

// CAS 余额更新：WHERE balance_yuan=预期旧值。扣款/充值都必须基于刚读到的余额，
// 余额一旦被并发修改，本次更新影响 0 行 → 失败重算，防止丢失更新。
bool WalletRepository::setBalance(qint64 userId, const DecimalValue &expectedBalance,
                                  const DecimalValue &newBalance, QString *error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE user_info SET balance_yuan=? WHERE id=? AND balance_yuan=?"));
    query.addBindValue(newBalance.toString());
    query.addBindValue(userId);
    query.addBindValue(expectedBalance.toString());
    return execChanged(query, error);
}

bool WalletRepository::addTransaction(qint64 userId, const QString &type,
                                      const DecimalValue &amountYuan,
                                      const QString &orderId,
                                      const QString &requestId, QString *error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO wallet_transaction(user_id,transaction_type,amount_yuan,order_id,request_id) "
        "VALUES(?,?,?,?,?)"));
    query.addBindValue(userId);
    query.addBindValue(type);
    query.addBindValue(amountYuan.toString());
    if (orderId.isEmpty()) {
        query.addBindValue(QVariant{});
    } else {
        query.addBindValue(orderId);
    }
    query.addBindValue(requestId);
    return execChanged(query, error);
}

// 完成订单：仅 PENDING_PAYMENT 可完成——重复支付已完成订单在这里影响 0 行而失败。
bool WalletRepository::finishOrder(const QString &orderId, QString *error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE charge_order SET status='FINISHED' "
        "WHERE id=? AND status='PENDING_PAYMENT'"));
    query.addBindValue(orderId);
    return execChanged(query, error);
}

// 释放电桩（容错设计）：正常桩 CHARGING→IDLE；
// 若桩已不是 CHARGING（如结算期间转 FAULT/OFFLINE），复查后接受 IDLE/FAULT/OFFLINE
// ——支付成功不应把故障/离线桩“错误恢复”成空闲。
bool WalletRepository::releasePile(qint64 pileId, QString *error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE pile_info SET status='IDLE' WHERE id=? AND status='CHARGING'"));
    query.addBindValue(pileId);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() == 1) {
        clearError(error);
        return true;
    }

    QSqlQuery current(database_);
    current.prepare(QStringLiteral("SELECT status FROM pile_info WHERE id=?"));
    current.addBindValue(pileId);
    if (!current.exec()) {
        setError(error, current.lastError().text());
        return false;
    }
    if (!current.next()) {
        clearError(error);
        return false;
    }
    const QString status = current.value(0).toString();
    clearError(error);
    return status == QStringLiteral("IDLE") || status == QStringLiteral("FAULT")
        || status == QStringLiteral("OFFLINE");
}

bool WalletRepository::updateProfile(qint64 userId, const QString &nickname,
                                     const QString &avatarPath, QString *error)
{
    QSqlQuery query(database_);
    if (avatarPath.isEmpty()) {
        query.prepare(QStringLiteral("UPDATE user_info SET nickname=? WHERE id=?"));
        query.addBindValue(nickname);
        query.addBindValue(userId);
    } else {
        query.prepare(QStringLiteral(
            "UPDATE user_info SET nickname=?,avatar_url=? WHERE id=?"));
        query.addBindValue(nickname);
        query.addBindValue(avatarPath);
        query.addBindValue(userId);
    }
    return execChanged(query, error);
}
