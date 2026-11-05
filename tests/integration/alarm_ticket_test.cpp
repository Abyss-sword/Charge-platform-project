// 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
// 告警工单集成测试：验证告警/工单状态机守卫（不能跳过处理步骤直接关闭）与审计日志。

#include <QtTest>

#include "server/services/alarm_service.h"
#include "server/services/telemetry_service.h"
#include "tests/helpers/test_database.h"

class AlarmTicketTest final : public QObject
{
    Q_OBJECT

private:
    static DecimalValue decimal(const QString &text)
    {
        return *DecimalValue::parseCanonical(text);
    }

private slots:
    void enforcesSeparateAlarmAndTicketStateMachines()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 pileId = testDb.addPile(testDb.addStation(),
                                             QStringLiteral("PILE-TICKET-001"));
        QDateTime now(QDate(2026, 9, 6), QTime(11, 0), Qt::UTC);
        TelemetryService telemetry(testDb.database(), [&now] { return now; }, 70.0);
        QVERIFY(telemetry.record({pileId, now, 80.0, 380.0, 120.0, 45600,
                                  decimal(QStringLiteral("1.00"))}).ok);
        const qint64 alarmId = testDb.scalar(QStringLiteral(
            "SELECT id FROM alarm_event WHERE pile_id=?"), {pileId}).toLongLong();
        const qint64 ticketId = testDb.scalar(QStringLiteral(
            "SELECT id FROM maintenance_ticket WHERE alarm_id=?"), {alarmId}).toLongLong();
        AlarmService service(testDb.database(), [&now] { return now; });

        QVERIFY(!service.closeTicket(ticketId, QStringLiteral("admin-1")).ok);
        QVERIFY(service.acknowledgeAlarm(alarmId, QStringLiteral("admin-1")).ok);
        QVERIFY(!service.acknowledgeAlarm(alarmId, QStringLiteral("admin-1")).ok);
        QVERIFY(service.startTicket(ticketId, QStringLiteral("engineer-a"),
                                    QStringLiteral("admin-1")).ok);
        QVERIFY(!service.resolveTicket(ticketId, QString{}, QStringLiteral("admin-1")).ok);
        QVERIFY(service.resolveTicket(ticketId, QStringLiteral("已检查并恢复散热"),
                                      QStringLiteral("admin-1")).ok);
        QVERIFY(!service.closeTicket(ticketId, QStringLiteral("admin-1")).ok);

        now = now.addSecs(1);
        QVERIFY(telemetry.record({pileId, now, 60.0, 380.0, 120.0, 45600,
                                  decimal(QStringLiteral("1.01"))}).ok);
        QVERIFY(service.closeTicket(ticketId, QStringLiteral("admin-1")).ok);
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM alarm_event WHERE id=?"),
                               {alarmId}).toString(), QStringLiteral("CLOSED"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM maintenance_ticket WHERE id=?"),
                               {ticketId}).toString(), QStringLiteral("CLOSED"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM operation_log WHERE target_type IN ('ALARM','TICKET')"))
                     .toInt(), 4);
    }

    void rejectsOutOfOrderTicketTransitionsWithoutPartialWrites()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 pileId = testDb.addPile(testDb.addStation(),
                                             QStringLiteral("PILE-TICKET-002"));
        QVERIFY(testDb.exec(QStringLiteral(
            "INSERT INTO alarm_event(pile_id,severity,alarm_type,content,status,occurred_at) "
            "VALUES(?,'WARNING','MANUAL','测试告警','OPEN','2026-09-06T11:00:00Z')"),
            {pileId}));
        const qint64 alarmId = testDb.scalar(QStringLiteral("SELECT last_insert_rowid()"))
                                   .toLongLong();
        QVERIFY(testDb.exec(QStringLiteral(
            "INSERT INTO maintenance_ticket(alarm_id,status) VALUES(?,'OPEN')"), {alarmId}));
        const qint64 ticketId = testDb.scalar(QStringLiteral("SELECT last_insert_rowid()"))
                                    .toLongLong();
        const QDateTime now(QDate(2026, 9, 6), QTime(11, 0), Qt::UTC);
        AlarmService service(testDb.database(), [now] { return now; });

        QVERIFY(!service.resolveTicket(ticketId, QStringLiteral("跳过处理"),
                                       QStringLiteral("admin-1")).ok);
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM maintenance_ticket WHERE id=?"),
                               {ticketId}).toString(), QStringLiteral("OPEN"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT COUNT(*) FROM operation_log")).toInt(), 0);
    }
};

QTEST_GUILESS_MAIN(AlarmTicketTest)
#include "alarm_ticket_test.moc"
