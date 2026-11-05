#pragma once

#include <QDateTime>
#include <QSqlDatabase>
#include <QString>

#include <optional>

// 告警记录（处置状态机使用）。
struct AlarmRecord
{
    qint64 id = 0;
    qint64 pileId = 0;
    QString status;
};

// 工单三表联查快照：工单状态 + 关联告警状态 + 电桩状态 + 处理说明（关单守卫用）。
struct TicketRecord
{
    qint64 id = 0;
    qint64 alarmId = 0;
    qint64 pileId = 0;
    QString status;
    QString alarmStatus;
    QString pileStatus;
    QString notes;
};

// AlarmRepository：alarm_event / maintenance_ticket / operation_log 的数据访问。
// 写入全部是条件更新（WHERE status=期望旧值 + 受影响行数=1），
// 保证多管理员并发处置时状态机不被跳步/覆盖。
class AlarmRepository
{
public:
    explicit AlarmRepository(QSqlDatabase database);

    // 创建或复用活动告警并关联新工单：同桩同类型已有 OPEN/ACKNOWLEDGED 告警则
    // 直接复用（不重复建），否则插告警 + 插工单；设计为在调用方事务内执行。
    bool ensureActiveAlarmAndTicket(qint64 pileId, const QString &orderId,
                                    const QString &severity, const QString &alarmType,
                                    const QString &content, const QDateTime &occurredAt,
                                    QString *error = nullptr);
    std::optional<AlarmRecord> findAlarm(qint64 alarmId,
                                         QString *error = nullptr) const;
    std::optional<TicketRecord> findTicket(qint64 ticketId,
                                           QString *error = nullptr) const;
    bool updateAlarmStatus(qint64 alarmId, const QString &expectedStatus,
                           const QString &newStatus, const QDateTime &now,
                           QString *error = nullptr);
    bool startTicket(qint64 ticketId, const QString &assignee, const QDateTime &now,
                     QString *error = nullptr);
    bool resolveTicket(qint64 ticketId, const QString &notes, const QDateTime &now,
                       QString *error = nullptr);
    bool closeTicket(qint64 ticketId, const QDateTime &now,
                     QString *error = nullptr);
    bool addOperationLog(const QString &actorId, const QString &action,
                         const QString &targetType, qint64 targetId,
                         const QDateTime &now, QString *error = nullptr);

private:
    QSqlDatabase database_;
};
