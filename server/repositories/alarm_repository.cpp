#include "server/repositories/alarm_repository.h"

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

bool changedOne(QSqlQuery &query, QString *error)
{
    if (!query.exec() || query.numRowsAffected() != 1) {
        setError(error, query.lastError().text());
        return false;
    }
    clearError(error);
    return true;
}
}

AlarmRepository::AlarmRepository(QSqlDatabase database)
    : database_(std::move(database))
{
}

// 告警唯一性靠“先查后插 + 活动告警唯一索引 ux_alarm_active_pile_type”双保险：
// 查到同桩同类型活动告警即复用；没有则插 OPEN 告警并关联 OPEN 工单。
// 必须运行在调用方（遥测/扫描）的事务内，保证“告警+工单”同生共死。
bool AlarmRepository::ensureActiveAlarmAndTicket(
    qint64 pileId, const QString &orderId, const QString &severity,
    const QString &alarmType, const QString &content, const QDateTime &occurredAt,
    QString *error)
{
    QSqlQuery existing(database_);
    existing.prepare(QStringLiteral(
        "SELECT id FROM alarm_event WHERE pile_id=? AND alarm_type=? "
        "AND status IN ('OPEN','ACKNOWLEDGED') LIMIT 1"));
    existing.addBindValue(pileId);
    existing.addBindValue(alarmType);
    if (!existing.exec()) {
        setError(error, existing.lastError().text());
        return false;
    }
    if (existing.next()) {
        clearError(error);
        return true;
    }

    QSqlQuery alarm(database_);
    alarm.prepare(QStringLiteral(
        "INSERT INTO alarm_event(pile_id,order_id,severity,alarm_type,content,status,occurred_at) "
        "VALUES(?,?,?,?,?,'OPEN',?)"));
    alarm.addBindValue(pileId);
    orderId.isEmpty() ? alarm.addBindValue(QVariant{}) : alarm.addBindValue(orderId);
    alarm.addBindValue(severity);
    alarm.addBindValue(alarmType);
    alarm.addBindValue(content);
    alarm.addBindValue(sqlTime(occurredAt));
    if (!alarm.exec()) {
        setError(error, alarm.lastError().text());
        return false;
    }
    const qint64 alarmId = alarm.lastInsertId().toLongLong();
    QSqlQuery ticket(database_);
    ticket.prepare(QStringLiteral(
        "INSERT INTO maintenance_ticket(alarm_id,status,created_at,updated_at) "
        "VALUES(?,'OPEN',?,?)"));
    ticket.addBindValue(alarmId);
    ticket.addBindValue(sqlTime(occurredAt));
    ticket.addBindValue(sqlTime(occurredAt));
    if (!ticket.exec()) {
        setError(error, ticket.lastError().text());
        return false;
    }
    clearError(error);
    return true;
}

std::optional<AlarmRecord> AlarmRepository::findAlarm(qint64 alarmId,
                                                       QString *error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT id,pile_id,status FROM alarm_event WHERE id=?"));
    query.addBindValue(alarmId);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) {
        clearError(error);
        return std::nullopt;
    }
    clearError(error);
    return AlarmRecord{query.value(0).toLongLong(), query.value(1).toLongLong(),
                       query.value(2).toString()};
}

std::optional<TicketRecord> AlarmRepository::findTicket(qint64 ticketId,
                                                         QString *error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT t.id,t.alarm_id,a.pile_id,t.status,a.status,p.status,t.notes "
        "FROM maintenance_ticket t JOIN alarm_event a ON a.id=t.alarm_id "
        "JOIN pile_info p ON p.id=a.pile_id WHERE t.id=?"));
    query.addBindValue(ticketId);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) {
        clearError(error);
        return std::nullopt;
    }
    clearError(error);
    return TicketRecord{query.value(0).toLongLong(), query.value(1).toLongLong(),
                        query.value(2).toLongLong(), query.value(3).toString(),
                        query.value(4).toString(), query.value(5).toString(),
                        query.value(6).toString()};
}

// 告警状态条件流转：WHERE status=期望旧值；转 CLOSED 时额外写 closed_at 时间戳。
bool AlarmRepository::updateAlarmStatus(qint64 alarmId, const QString &expectedStatus,
                                        const QString &newStatus, const QDateTime &now,
                                        QString *error)
{
    QSqlQuery query(database_);
    if (newStatus == QStringLiteral("CLOSED")) {
        query.prepare(QStringLiteral(
            "UPDATE alarm_event SET status=?,closed_at=? WHERE id=? AND status=?"));
        query.addBindValue(newStatus);
        query.addBindValue(sqlTime(now));
    } else {
        query.prepare(QStringLiteral(
            "UPDATE alarm_event SET status=? WHERE id=? AND status=?"));
        query.addBindValue(newStatus);
    }
    query.addBindValue(alarmId);
    query.addBindValue(expectedStatus);
    return changedOne(query, error);
}

// 工单三段流转：开始（OPEN→PROCESSING，记负责人）、解决（→RESOLVED，记说明），
// 关闭（RESOLVED→CLOSED，记 closed_at）——每步 WHERE 都锁定期望旧状态。
bool AlarmRepository::startTicket(qint64 ticketId, const QString &assignee,
                                  const QDateTime &now, QString *error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE maintenance_ticket SET status='PROCESSING',assignee=?,updated_at=? "
        "WHERE id=? AND status='OPEN'"));
    query.addBindValue(assignee);
    query.addBindValue(sqlTime(now));
    query.addBindValue(ticketId);
    return changedOne(query, error);
}

bool AlarmRepository::resolveTicket(qint64 ticketId, const QString &notes,
                                    const QDateTime &now, QString *error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE maintenance_ticket SET status='RESOLVED',notes=?,resolved_at=?,updated_at=? "
        "WHERE id=? AND status='PROCESSING'"));
    query.addBindValue(notes);
    query.addBindValue(sqlTime(now));
    query.addBindValue(sqlTime(now));
    query.addBindValue(ticketId);
    return changedOne(query, error);
}

bool AlarmRepository::closeTicket(qint64 ticketId, const QDateTime &now, QString *error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE maintenance_ticket SET status='CLOSED',closed_at=?,updated_at=? "
        "WHERE id=? AND status='RESOLVED'"));
    query.addBindValue(sqlTime(now));
    query.addBindValue(sqlTime(now));
    query.addBindValue(ticketId);
    return changedOne(query, error);
}

// 运维处置审计日志（actor 固定记录为 ADMIN 类型）。
bool AlarmRepository::addOperationLog(const QString &actorId, const QString &action,
                                      const QString &targetType, qint64 targetId,
                                      const QDateTime &now, QString *error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO operation_log(actor_type,actor_id,action,target_type,target_id,result,"
        "created_at) VALUES('ADMIN',?,?,?,?,'SUCCESS',?)"));
    query.addBindValue(actorId);
    query.addBindValue(action);
    query.addBindValue(targetType);
    query.addBindValue(QString::number(targetId));
    query.addBindValue(sqlTime(now));
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    clearError(error);
    return true;
}
