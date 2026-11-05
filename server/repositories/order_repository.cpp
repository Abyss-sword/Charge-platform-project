#include "server/repositories/order_repository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QJsonDocument>
#include <QJsonObject>

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

QString sqlTime(const QDateTime &value)
{
    return value.toUTC().toString(Qt::ISODate);
}

// 公共写辅助：执行条件更新并要求“恰好影响 1 行”。
// 影响行数=0 意味着旧状态已被并发修改（或记录不存在），必须按失败处理——
// 这是防覆盖竞态的核心手段。
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

OrderRepository::OrderRepository(QSqlDatabase database)
    : database_(std::move(database))
{
}

bool OrderRepository::hasUnfinishedOrder(qint64 userId, QString *error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM charge_order WHERE user_id=? "
        "AND status IN ('RESERVED','CHARGING','PENDING_PAYMENT') LIMIT 1"));
    query.addBindValue(userId);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    clearError(error);
    return query.next();
}

bool OrderRepository::hasActiveOrderForPile(qint64 pileId, QString *error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM charge_order WHERE pile_id=? "
        "AND status IN ('RESERVED','CHARGING','PENDING_PAYMENT') LIMIT 1"));
    query.addBindValue(pileId);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    clearError(error);
    return query.next();
}

std::optional<QString> OrderRepository::userStatus(qint64 userId, QString *error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT status FROM user_info WHERE id=?"));
    query.addBindValue(userId);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return std::nullopt;
    }
    clearError(error);
    if (!query.next()) {
        return std::nullopt;
    }
    return query.value(0).toString();
}

// 桩状态查询：LEFT JOIN 站点营业状态——非 OPEN 站点统一返回内部标记
// STATION_UNAVAILABLE（Service 层再转成 PILE_UNAVAILABLE），防止对停运站预约。
std::optional<QString> OrderRepository::pileStatus(qint64 pileId, QString *error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT CASE WHEN s.status='OPEN' THEN p.status ELSE 'STATION_UNAVAILABLE' END "
        "FROM pile_info p JOIN station_info s ON s.id=p.station_id WHERE p.id=?"));
    query.addBindValue(pileId);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return std::nullopt;
    }
    clearError(error);
    if (!query.next()) {
        return std::nullopt;
    }
    return query.value(0).toString();
}

std::optional<OrderRecord> OrderRepository::findOrder(const QString &orderId,
                                                       QString *error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id,user_id,pile_id,status,energy_kwh,reserved_until,unit_price_yuan_per_kwh,price_locked "
        "FROM charge_order WHERE id=?"));
    query.addBindValue(orderId);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) {
        clearError(error);
        return std::nullopt;
    }
    const auto energy = DecimalValue::parseCanonical(query.value(4).toString());
    const auto price = DecimalValue::parseCanonical(query.value(6).toString());
    if (!energy || !price) {
        setError(error, QStringLiteral("invalid order decimal value in database"));
        return std::nullopt;
    }
    clearError(error);
    return OrderRecord{query.value(0).toString(), query.value(1).toLongLong(),
                       query.value(2).toLongLong(), query.value(3).toString(), *energy,
                       QDateTime::fromString(query.value(5).toString(), Qt::ISODate),
                       *price, query.value(7).toBool()};
}

std::optional<DecimalValue> OrderRepository::stationPriceForPile(qint64 pileId,
                                                                  QString *error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT s.price_yuan_per_kwh FROM pile_info p "
        "JOIN station_info s ON s.id=p.station_id WHERE p.id=?"));
    query.addBindValue(pileId);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) {
        clearError(error);
        return std::nullopt;
    }
    const auto price = DecimalValue::parseCanonical(query.value(0).toString());
    if (!price) {
        setError(error, QStringLiteral("invalid price_yuan_per_kwh in database"));
        return std::nullopt;
    }
    clearError(error);
    return price;
}

QVector<ExpiredReservation> OrderRepository::expiredReservations(
    const QDateTime &now, QString *error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id,pile_id FROM charge_order "
        "WHERE status='RESERVED' AND reserved_until<=?"));
    query.addBindValue(sqlTime(now));
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return {};
    }
    QVector<ExpiredReservation> result;
    while (query.next()) {
        result.push_back({query.value(0).toString(), query.value(1).toLongLong()});
    }
    clearError(error);
    return result;
}

std::optional<ReservationReplay> OrderRepository::reservationByRequest(
    const QString &requestId, QString *error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT result_json,action FROM request_dedup WHERE request_id=?"));
    query.addBindValue(requestId);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return std::nullopt;
    }
    clearError(error);
    if (!query.next()) {
        return std::nullopt;
    }
    const QJsonObject object = QJsonDocument::fromJson(query.value(0).toByteArray()).object();
    return ReservationReplay{object.value(QStringLiteral("userId")).toInteger(),
                             object.value(QStringLiteral("pileId")).toInteger(),
                             object.value(QStringLiteral("orderId")).toString(),
                             query.value(1).toString()};
}

bool OrderRepository::createReservation(const QString &orderId, qint64 userId, qint64 pileId,
                                        const QDateTime &reservedUntil,
                                        const QDateTime &createdAt, QString *error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO charge_order(id,user_id,pile_id,status,reserved_until,created_at) "
        "VALUES(?,?,?,'RESERVED',?,?)"));
    query.addBindValue(orderId);
    query.addBindValue(userId);
    query.addBindValue(pileId);
    query.addBindValue(sqlTime(reservedUntil));
    query.addBindValue(sqlTime(createdAt));
    return execChanged(query, error);
}

// 桩状态条件流转：UPDATE pile_info SET status=新 WHERE id=? AND status=期望旧值。
// 并发下只有看到“正确旧状态”的一方才能改成功，这是防抢占的关键写入。
bool OrderRepository::changePileStatus(qint64 pileId, const QString &from, const QString &to,
                                       QString *error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE pile_info SET status=? WHERE id=? AND status=?"));
    query.addBindValue(to);
    query.addBindValue(pileId);
    query.addBindValue(from);
    return execChanged(query, error);
}

// 开始充电：RESERVED→CHARGING，同时锁定开始时单价快照（price_locked=1），
// 之后站点改价不影响本单结算。
bool OrderRepository::startOrder(const QString &orderId, const QDateTime &startedAt,
                                 const DecimalValue &unitPriceYuanPerKwh,
                                 const QString &meterStartKwh, QString *error)
{
    // M1-03：锁价与表计基准同事务写入；meter_last_at 初始化为基准读数时间。
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE charge_order SET status='CHARGING',started_at=?,unit_price_yuan_per_kwh=?,"
        "price_locked=1,meter_start_kwh=?,meter_last_at=started_at "
        "WHERE id=? AND status='RESERVED'"));
    query.addBindValue(sqlTime(startedAt));
    query.addBindValue(unitPriceYuanPerKwh.toString());
    query.addBindValue(meterStartKwh);
    query.addBindValue(orderId);
    return execChanged(query, error);
}

bool OrderRepository::expireOrder(const QString &orderId, const QDateTime &endedAt,
                                  QString *error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE charge_order SET status='EXPIRED',ended_at=?,end_reason='RESERVATION_TIMEOUT' "
        "WHERE id=? AND status='RESERVED'"));
    query.addBindValue(sqlTime(endedAt));
    query.addBindValue(orderId);
    return execChanged(query, error);
}

bool OrderRepository::cancelOrder(const QString &orderId, const QDateTime &endedAt,
                                  QString *error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE charge_order SET status='CANCELED',ended_at=?,end_reason='USER_CANCEL' "
        "WHERE id=? AND status='RESERVED'"));
    query.addBindValue(sqlTime(endedAt));
    query.addBindValue(orderId);
    return execChanged(query, error);
}

// 电量 CAS 更新：WHERE 带旧电量值，两个并发上报只有一个能成功，天然防回退/防覆盖。
bool OrderRepository::updateEnergy(const QString &orderId,
                                   const DecimalValue &previousEnergyKwh,
                                   const DecimalValue &energyKwh, QString *error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE charge_order SET energy_kwh=? "
        "WHERE id=? AND status='CHARGING' AND energy_kwh=?"));
    query.addBindValue(energyKwh.toString());
    query.addBindValue(orderId);
    query.addBindValue(previousEnergyKwh.toString());
    return execChanged(query, error);
}

// 停止结算落库：CHARGING→PENDING_PAYMENT，写入结算单价与金额、结束时间与原因。
bool OrderRepository::stopOrder(const QString &orderId,
                                const DecimalValue &unitPriceYuanPerKwh,
                                const DecimalValue &amountYuan,
                                const QDateTime &endedAt, QString *error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE charge_order SET status='PENDING_PAYMENT',unit_price_yuan_per_kwh=?,"
        "amount_yuan=?,ended_at=?,end_reason='USER_STOP' "
        "WHERE id=? AND status='CHARGING'"));
    query.addBindValue(unitPriceYuanPerKwh.toString());
    query.addBindValue(amountYuan.toString());
    query.addBindValue(sqlTime(endedAt));
    query.addBindValue(orderId);
    return execChanged(query, error);
}

// 写预约防重记录：request_dedup(action='RESERVE')，结果 JSON 含用户/桩/订单，
// 供重复请求复放与参数一致性比对。
bool OrderRepository::recordReservationRequest(const QString &requestId, qint64 userId,
                                                qint64 pileId, const QString &orderId,
                                                QString *error)
{
    const QByteArray json = QJsonDocument(QJsonObject{
        {QStringLiteral("userId"), userId},
        {QStringLiteral("pileId"), pileId},
        {QStringLiteral("orderId"), orderId}
    }).toJson(QJsonDocument::Compact);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO request_dedup(request_id,action,result_json) VALUES(?,'RESERVE',?)"));
    query.addBindValue(requestId);
    query.addBindValue(QString::fromUtf8(json));
    return execChanged(query, error);
}
