#include "server/services/alarm_service.h"

#include "server/repositories/alarm_repository.h"
#include "server/services/linkage.h"

#include <QSqlQuery>

#include <utility>

// 运维处置实现：每个动作 = 参数校验 → BEGIN IMMEDIATE → 状态守卫 → 条件更新 + 审计日志 → 提交。
// 条件更新（WHERE status=期望旧状态）保证并发下不会覆盖别人已推进的状态。
namespace {
AlarmActionResult failure(const QString &errorCode)
{
    return {false, errorCode};
}

bool beginImmediate(QSqlDatabase database)
{
    QSqlQuery query(database);
    return query.exec(QStringLiteral("BEGIN IMMEDIATE"));
}

// 提交失败时回滚；提交前原子抬升联动版本（S1-A 契约，见 linkage.h）。
bool commitOrRollback(QSqlDatabase database)
{
    if (linkage::bumpVersion(database) < 0) {
        database.rollback();
        return false;
    }
    if (database.commit()) {
        return true;
    }
    database.rollback();
    return false;
}

bool validIdentity(const QString &value)
{
    return !value.trimmed().isEmpty();
}
}

AlarmService::AlarmService(QSqlDatabase database, Clock clock)
    : database_(std::move(database))
    , clock_(std::move(clock))
{
}

// 告警确认：仅 OPEN 状态可确认（重复确认/跳步返回 STATE_CONFLICT），
// OPEN→ACKNOWLEDGED 与审计日志同一事务。
AlarmActionResult AlarmService::acknowledgeAlarm(qint64 alarmId, const QString &actorId)
{
    if (alarmId <= 0 || !validIdentity(actorId)) {
        return failure(QStringLiteral("PARAM_INVALID"));
    }
    if (!database_.isOpen() || !beginImmediate(database_)) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    AlarmRepository repository(database_);
    QString error;
    const auto alarm = repository.findAlarm(alarmId, &error);
    const QDateTime now = clock_().toUTC();
    if (!error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (!alarm || alarm->status != QStringLiteral("OPEN")) {
        database_.rollback();
        return failure(QStringLiteral("STATE_CONFLICT"));
    }
    if (!repository.updateAlarmStatus(alarmId, QStringLiteral("OPEN"),
                                      QStringLiteral("ACKNOWLEDGED"), now, &error)
        || !repository.addOperationLog(actorId, QStringLiteral("ACKNOWLEDGE_ALARM"),
                                       QStringLiteral("ALARM"), alarmId, now, &error)
        || !commitOrRollback(database_)) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    return {true, QStringLiteral("OK")};
}

// 开始处理工单：仅 OPEN 可开始，登记负责人（assignee 必填）→ PROCESSING。
AlarmActionResult AlarmService::startTicket(qint64 ticketId, const QString &assignee,
                                             const QString &actorId)
{
    if (ticketId <= 0 || !validIdentity(assignee) || !validIdentity(actorId)) {
        return failure(QStringLiteral("PARAM_INVALID"));
    }
    if (!database_.isOpen() || !beginImmediate(database_)) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    AlarmRepository repository(database_);
    QString error;
    const auto ticket = repository.findTicket(ticketId, &error);
    const QDateTime now = clock_().toUTC();
    if (!error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (!ticket || ticket->status != QStringLiteral("OPEN")) {
        database_.rollback();
        return failure(QStringLiteral("STATE_CONFLICT"));
    }
    if (!repository.startTicket(ticketId, assignee.trimmed(), now, &error)
        || !repository.addOperationLog(actorId, QStringLiteral("START_TICKET"),
                                       QStringLiteral("TICKET"), ticketId, now, &error)
        || !commitOrRollback(database_)) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    return {true, QStringLiteral("OK")};
}

// 解决工单：仅 PROCESSING 可解决，处理说明（notes）必填 → RESOLVED。
AlarmActionResult AlarmService::resolveTicket(qint64 ticketId, const QString &notes,
                                               const QString &actorId)
{
    if (ticketId <= 0 || notes.trimmed().isEmpty() || !validIdentity(actorId)) {
        return failure(QStringLiteral("PARAM_INVALID"));
    }
    if (!database_.isOpen() || !beginImmediate(database_)) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    AlarmRepository repository(database_);
    QString error;
    const auto ticket = repository.findTicket(ticketId, &error);
    const QDateTime now = clock_().toUTC();
    if (!error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (!ticket || ticket->status != QStringLiteral("PROCESSING")) {
        database_.rollback();
        return failure(QStringLiteral("STATE_CONFLICT"));
    }
    if (!repository.resolveTicket(ticketId, notes.trimmed(), now, &error)
        || !repository.addOperationLog(actorId, QStringLiteral("RESOLVE_TICKET"),
                                       QStringLiteral("TICKET"), ticketId, now, &error)
        || !commitOrRollback(database_)) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    return {true, QStringLiteral("OK")};
}

// 关闭工单（终态，守卫最严格）：
// 1) 工单必须已 RESOLVED；2) 关联告警必须已 ACKNOWLEDGED；3) 处理说明非空；
// 4) 电桩不能仍是 FAULT/OFFLINE（DEVICE_NOT_RECOVERED）——设备恢复不等于自动关单。
// 通过后同一事务内：关工单 + 同步关告警（ACKNOWLEDGED→CLOSED）+ 写审计日志。
AlarmActionResult AlarmService::closeTicket(qint64 ticketId, const QString &actorId)
{
    if (ticketId <= 0 || !validIdentity(actorId)) {
        return failure(QStringLiteral("PARAM_INVALID"));
    }
    if (!database_.isOpen() || !beginImmediate(database_)) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    AlarmRepository repository(database_);
    QString error;
    const auto ticket = repository.findTicket(ticketId, &error);
    const QDateTime now = clock_().toUTC();
    if (!error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (!ticket || ticket->status != QStringLiteral("RESOLVED")
        || ticket->alarmStatus != QStringLiteral("ACKNOWLEDGED")
        || ticket->notes.trimmed().isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("STATE_CONFLICT"));
    }
    if (ticket->pileStatus == QStringLiteral("FAULT")
        || ticket->pileStatus == QStringLiteral("OFFLINE")) {
        database_.rollback();
        return failure(QStringLiteral("DEVICE_NOT_RECOVERED"));
    }
    if (!repository.closeTicket(ticketId, now, &error)
        || !repository.updateAlarmStatus(ticket->alarmId, QStringLiteral("ACKNOWLEDGED"),
                                         QStringLiteral("CLOSED"), now, &error)
        || !repository.addOperationLog(actorId, QStringLiteral("CLOSE_TICKET"),
                                       QStringLiteral("TICKET"), ticketId, now, &error)
        || !commitOrRollback(database_)) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    return {true, QStringLiteral("OK")};
}
