#pragma once

#include <QDateTime>
#include <QSqlDatabase>
#include <QString>

#include <functional>

// 运维处置结果（失败时 errorCode：STATE_CONFLICT / DEVICE_NOT_RECOVERED 等）。
struct AlarmActionResult
{
    bool ok = false;
    QString errorCode;
};

// AlarmService：告警与工单的运维处置状态机（答辩高频）。
//   告警：OPEN → ACKNOWLEDGED → CLOSED
//   工单：OPEN → PROCESSING → RESOLVED → CLOSED
// 守卫规则：开始工单必须填负责人；解决工单必须填处理说明；关闭工单要求
// 告警已确认、说明非空、且电桩不再是 FAULT/OFFLINE；关闭工单会同步关闭告警并写审计日志。
// 注意口径：重复/跳步的处置请求返回状态冲突（非幂等）；设备恢复不会自动替管理员关单。
class AlarmService
{
public:
    using Clock = std::function<QDateTime()>;

    explicit AlarmService(QSqlDatabase database,
                          Clock clock = [] { return QDateTime::currentDateTimeUtc(); });

    // 告警确认：仅 OPEN 可确认，OPEN→ACKNOWLEDGED + 审计日志（同事务）。
    AlarmActionResult acknowledgeAlarm(qint64 alarmId, const QString &actorId);
    // 开始处理：工单 OPEN→PROCESSING，登记负责人。
    AlarmActionResult startTicket(qint64 ticketId, const QString &assignee,
                                  const QString &actorId);
    // 解决：PROCESSING→RESOLVED，必须填写处理说明。
    AlarmActionResult resolveTicket(qint64 ticketId, const QString &notes,
                                    const QString &actorId);
    // 关闭：RESOLVED→CLOSED，四重守卫校验后同步关闭关联告警 + 审计日志（同一事务）。
    AlarmActionResult closeTicket(qint64 ticketId, const QString &actorId);

private:
    QSqlDatabase database_;
    Clock clock_;
};
