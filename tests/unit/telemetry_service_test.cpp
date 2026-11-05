// 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
// 遥测服务单测：验证物理量格式、时序/电量单调、过热联动、事务回滚、离线恢复与演示心跳——
// 异常设备数据有明确处理。

#include <QtTest>

#include <cmath>
#include <limits>

#include "server/services/telemetry_service.h"
#include "server/services/order_service.h"
#include "tests/helpers/test_database.h"

class TelemetryServiceTest final : public QObject
{
    Q_OBJECT

private:
    static DecimalValue decimal(const QString &text)
    {
        const auto value = DecimalValue::parseCanonical(text);
        Q_ASSERT(value);
        return *value;
    }

    static TelemetryInput input(qint64 pileId, const QDateTime &recordedAt,
                                const QString &energyKwh = QStringLiteral("1.00"),
                                double temperatureC = 35.0)
    {
        return {pileId, recordedAt, temperatureC, 380.0, 120.0, 45600,
                decimal(energyKwh)};
    }

private slots:
    void demoSeedDoesNotImmediatelyCreateOfflineAlarms()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        auto database = testDb.database();
        QString seedError;
        QVERIFY2(Database::executeSqlFile(
                     database,
                     QStringLiteral(CHARGE_PLATFORM_SOURCE_DIR "/database/seeds/demo.sql"),
                     &seedError),
                 qPrintable(seedError));
        const QDateTime now(QDate(2026, 9, 8), QTime(10, 0), Qt::UTC);

        QCOMPARE(TelemetryService(database, [now] { return now; }).scanOffline(), 0);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM alarm_event WHERE alarm_type='OFFLINE'" )).toInt(), 0);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM pile_info WHERE status='IDLE'" )).toInt(), 12);
    }

    void acceptsCanonicalTelemetryAndRefreshesHeartbeat()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 stationId = testDb.addStation();
        const qint64 pileId = testDb.addPile(stationId, QStringLiteral("PILE-TEL-001"));
        const QDateTime now(QDate(2026, 9, 6), QTime(10, 0), Qt::UTC);
        TelemetryService service(testDb.database(), [now] { return now; });
        const QDateTime firstRecordedAt = now.addSecs(-60);

        const auto result = service.record(input(pileId, firstRecordedAt));

        QVERIFY(result.ok);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT energy_kwh FROM pile_telemetry WHERE pile_id=?"), {pileId}).toString(),
            QStringLiteral("1.00"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT last_heartbeat_at FROM pile_info WHERE id=?"), {pileId}).toString(),
            QStringLiteral("2026-09-06T10:00:00Z"));
        QVERIFY(service.record(input(pileId, now.addSecs(-30),
                                     QStringLiteral("1.01"))).ok);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM pile_telemetry WHERE pile_id=?"), {pileId}).toInt(), 2);
    }

    void rejectsUnknownPileInvalidNumbersAndBackwardsData()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 pileId = testDb.addPile(testDb.addStation(),
                                             QStringLiteral("PILE-TEL-002"));
        const QDateTime now(QDate(2026, 9, 6), QTime(10, 0), Qt::UTC);
        TelemetryService service(testDb.database(), [now] { return now; });
        QVERIFY(service.record(input(pileId, now, QStringLiteral("2.00"))).ok);

        auto unknown = input(999999, now.addSecs(1));
        QVERIFY(!service.record(unknown).ok);
        auto invalid = input(pileId, now.addSecs(1));
        invalid.temperatureC = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!service.record(invalid).ok);
        QVERIFY(!service.record(input(pileId, now.addSecs(-1),
                                      QStringLiteral("2.10"))).ok);
        QVERIFY(!service.record(input(pileId, now.addSecs(1),
                                      QStringLiteral("1.99"))).ok);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM pile_telemetry WHERE pile_id=?"), {pileId}).toInt(), 1);
    }

    void createsOneHighTemperatureAlarmAndTicket()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 pileId = testDb.addPile(testDb.addStation(),
                                             QStringLiteral("PILE-TEL-HOT"));
        QDateTime now(QDate(2026, 9, 6), QTime(10, 0), Qt::UTC);
        TelemetryService service(testDb.database(), [&now] { return now; }, 70.0);

        QVERIFY(service.record(input(pileId, now, QStringLiteral("1.00"), 75.0)).ok);
        now = now.addSecs(1);
        QVERIFY(service.record(input(pileId, now, QStringLiteral("1.01"), 76.0)).ok);

        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM alarm_event WHERE pile_id=? AND alarm_type='OVERHEAT'"),
            {pileId}).toInt(), 1);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM maintenance_ticket t JOIN alarm_event a ON a.id=t.alarm_id "
            "WHERE a.pile_id=? AND a.alarm_type='OVERHEAT'"), {pileId}).toInt(), 1);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT status FROM pile_info WHERE id=?"), {pileId}).toString(),
            QStringLiteral("FAULT"));
    }

    void rollsBackTelemetryWhenAlarmTicketCreationFails()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 pileId = testDb.addPile(testDb.addStation(),
                                             QStringLiteral("PILE-TEL-ROLLBACK"));
        QVERIFY(testDb.exec(QStringLiteral(
            "CREATE TRIGGER reject_test_ticket BEFORE INSERT ON maintenance_ticket "
            "BEGIN SELECT RAISE(ABORT,'forced ticket failure'); END")));
        const QDateTime now(QDate(2026, 9, 6), QTime(10, 0), Qt::UTC);
        TelemetryService service(testDb.database(), [now] { return now; }, 70.0);

        const auto result = service.record(
            input(pileId, now, QStringLiteral("1.00"), 75.0));

        QVERIFY(!result.ok);
        QCOMPARE(result.errorCode, QStringLiteral("DATABASE_ERROR"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM pile_telemetry WHERE pile_id=?"), {pileId}).toInt(), 0);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM alarm_event WHERE pile_id=?"), {pileId}).toInt(), 0);
        QVERIFY(testDb.scalar(QStringLiteral(
            "SELECT last_heartbeat_at FROM pile_info WHERE id=?"), {pileId}).isNull());
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT status FROM pile_info WHERE id=?"), {pileId}).toString(),
            QStringLiteral("IDLE"));
    }

    void marksStalePileOfflineAndRestoresChargingState()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800138101"));
        const qint64 pileId = testDb.addPile(testDb.addStation(),
                                             QStringLiteral("PILE-TEL-OFFLINE"),
                                             QStringLiteral("CHARGING"));
        QVERIFY(testDb.exec(QStringLiteral(
            "INSERT INTO charge_order(id,user_id,pile_id,status,energy_kwh,amount_yuan,"
            "unit_price_yuan_per_kwh,created_at) VALUES('ORD-TEL-1',?,?,'CHARGING',"
            "'0.00','0.00','1.20','2026-09-06T09:00:00Z')"), {userId, pileId}));
        QVERIFY(testDb.exec(QStringLiteral(
            "UPDATE pile_info SET last_heartbeat_at='2026-09-06T09:59:44Z' WHERE id=?"),
            {pileId}));
        QDateTime now(QDate(2026, 9, 6), QTime(10, 0), Qt::UTC);
        TelemetryService service(testDb.database(), [&now] { return now; });

        QCOMPARE(service.scanOffline(), 1);
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM pile_info WHERE id=?"),
                               {pileId}).toString(), QStringLiteral("OFFLINE"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM alarm_event WHERE pile_id=? AND alarm_type='OFFLINE'"),
            {pileId}).toInt(), 1);

        now = now.addSecs(1);
        QVERIFY(service.record(input(pileId, now, QStringLiteral("0.01"))).ok);
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM pile_info WHERE id=?"),
                               {pileId}).toString(), QStringLiteral("CHARGING"));
    }

    void lifetimeMeterIsIdempotentAndUpdatesTheActiveOrderByDelta()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800138102"));
        const qint64 pileId = testDb.addPile(testDb.addStation(),
                                             QStringLiteral("PILE-LIFETIME-METER"));
        QDateTime now(QDate(2026, 9, 9), QTime(10, 0), Qt::UTC);
        TelemetryService telemetry(testDb.database(), [&now] { return now; });

        auto baseline = input(pileId, now, QStringLiteral("100.00"));
        baseline.sourceMode = QStringLiteral("SIMULATED");
        QVERIFY(telemetry.record(baseline).ok);

        OrderService orders(testDb.database(), [&now] { return now; });
        const auto reservation = orders.reserve(
            userId, pileId, QStringLiteral("REQ-LIFETIME-METER"));
        QVERIFY(reservation.ok);
        QVERIFY(orders.start(reservation.orderId).ok);

        now = now.addSecs(5);
        auto charging = input(pileId, now, QStringLiteral("100.05"));
        charging.sourceMode = QStringLiteral("SIMULATED");
        const auto accepted = telemetry.record(charging);
        QVERIFY(accepted.ok);
        QVERIFY(!accepted.duplicate);
        QCOMPARE(accepted.orderId, reservation.orderId);
        QCOMPARE(accepted.orderEnergyKwh.toString(), QStringLiteral("0.05"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT energy_kwh FROM charge_order WHERE id=?"),
            {reservation.orderId}).toString(), QStringLiteral("0.05"));

        const auto duplicate = telemetry.record(charging);
        QVERIFY(duplicate.ok);
        QVERIFY(duplicate.duplicate);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM pile_telemetry WHERE pile_id=?"), {pileId}).toInt(), 2);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT energy_kwh FROM charge_order WHERE id=?"),
            {reservation.orderId}).toString(), QStringLiteral("0.05"));

        QVERIFY(orders.stop(reservation.orderId).ok);
        now = now.addSecs(5);
        QVERIFY(telemetry.record(input(pileId, now, QStringLiteral("100.07"))).ok);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT energy_kwh FROM charge_order WHERE id=?"),
            {reservation.orderId}).toString(), QStringLiteral("0.05"));
    }
};

QTEST_GUILESS_MAIN(TelemetryServiceTest)
#include "telemetry_service_test.moc"
