#include "server/repositories/telemetry_repository.h"

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

QString sqlTime(const QDateTime &value)
{
    return value.toUTC().toString(Qt::ISODate);
}
}

TelemetryRepository::TelemetryRepository(QSqlDatabase database)
    : database_(std::move(database))
{
}

// 电桩全景查询：一条 SQL 用三个子查询取最近遥测（按设备时间排序）、
// 最近活动订单（id/status），供遥测校验与状态恢复决策使用。
std::optional<TelemetryPileRecord> TelemetryRepository::findPile(
    qint64 pileId, QString *error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT p.id,p.status,p.last_heartbeat_at,"
        "(SELECT recorded_at FROM pile_telemetry t WHERE t.pile_id=p.id "
        " ORDER BY t.recorded_at DESC,t.id DESC LIMIT 1),"
        "(SELECT energy_kwh FROM pile_telemetry t WHERE t.pile_id=p.id "
        " ORDER BY t.recorded_at DESC,t.id DESC LIMIT 1),"
        "(SELECT id FROM charge_order o WHERE o.pile_id=p.id "
        " AND o.status IN ('RESERVED','CHARGING','PENDING_PAYMENT') "
        " ORDER BY o.created_at DESC LIMIT 1),"
        "(SELECT status FROM charge_order o WHERE o.pile_id=p.id "
        " AND o.status IN ('RESERVED','CHARGING','PENDING_PAYMENT') "
        " ORDER BY o.created_at DESC LIMIT 1),"
        "COALESCE(p.admin_status_override,'') "
        "FROM pile_info p WHERE p.id=?"));
    query.addBindValue(pileId);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) {
        clearError(error);
        return std::nullopt;
    }

    TelemetryPileRecord result;
    result.id = query.value(0).toLongLong();
    result.status = query.value(1).toString();
    result.lastHeartbeatAt = QDateTime::fromString(query.value(2).toString(), Qt::ISODate);
    result.latestRecordedAt = QDateTime::fromString(query.value(3).toString(), Qt::ISODate);
    if (!query.value(4).isNull()) {
        result.latestEnergyKwh = DecimalValue::parseCanonical(query.value(4).toString());
        if (!result.latestEnergyKwh) {
            setError(error, QStringLiteral("invalid energy_kwh in database"));
            return std::nullopt;
        }
    }
    result.activeOrderId = query.value(5).toString();
    result.activeOrderStatus = query.value(6).toString();
    result.adminOverride = query.value(7).toString();
    clearError(error);
    return result;
}

// 心跳离线扫描候选：last_heartbeat_at 非空且 ≤ cutoff、当前非 OFFLINE；
// NULL 心跳（从未上报）的桩被显式排除。
QVector<TelemetryPileRecord> TelemetryRepository::stalePiles(
    const QDateTime &cutoff, QString *error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id FROM pile_info WHERE last_heartbeat_at IS NOT NULL "
        "AND last_heartbeat_at<=? AND status<>'OFFLINE' ORDER BY id"));
    query.addBindValue(sqlTime(cutoff));
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return {};
    }
    QVector<qint64> ids;
    while (query.next()) {
        ids.push_back(query.value(0).toLongLong());
    }
    QVector<TelemetryPileRecord> result;
    for (const qint64 id : ids) {
        const auto pile = findPile(id, error);
        if (!pile) {
            return {};
        }
        result.push_back(*pile);
    }
    clearError(error);
    return result;
}

bool TelemetryRepository::insertTelemetry(
    qint64 pileId, const QDateTime &recordedAt, double temperatureC,
    double voltageV, double currentA, int powerW, const DecimalValue &energyKwh,
    QString *error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO pile_telemetry(pile_id,recorded_at,temperature_c,voltage_v,"
        "current_a,power_w,energy_kwh) VALUES(?,?,?,?,?,?,?)"));
    query.addBindValue(pileId);
    query.addBindValue(sqlTime(recordedAt));
    query.addBindValue(temperatureC);
    query.addBindValue(voltageV);
    query.addBindValue(currentA);
    query.addBindValue(powerW);
    query.addBindValue(energyKwh.toString());
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    clearError(error);
    return true;
}

// 刷新心跳与状态：心跳写“服务端接收时间”（扫描判断依据），遥测明细另有“设备上报时间”。
bool TelemetryRepository::updateHeartbeatAndStatus(
    qint64 pileId, const QDateTime &recordedAt, const QString &status, QString *error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE pile_info SET last_heartbeat_at=?,status=? WHERE id=?"));
    query.addBindValue(sqlTime(recordedAt));
    query.addBindValue(status);
    query.addBindValue(pileId);
    if (!query.exec() || query.numRowsAffected() != 1) {
        setError(error, query.lastError().text());
        return false;
    }
    clearError(error);
    return true;
}

// 条件流转桩状态（离线扫描用）：WHERE status=期望旧值，避免覆盖遥测刚写入的新状态。
bool TelemetryRepository::changeStatus(qint64 pileId, const QString &expectedStatus,
                                       const QString &newStatus, QString *error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE pile_info SET status=? WHERE id=? AND status=?"));
    query.addBindValue(newStatus);
    query.addBindValue(pileId);
    query.addBindValue(expectedStatus);
    if (!query.exec() || query.numRowsAffected() != 1) {
        setError(error, query.lastError().text());
        return false;
    }
    clearError(error);
    return true;
}
