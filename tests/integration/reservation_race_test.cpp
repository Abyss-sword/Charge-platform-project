// 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
// 并发抢桩集成测试：多连接并发预约同一电桩只能成功一个；另验证冻结用户、停运站、
// 预约期限、取消、计费与预约防重——服务端一致性的核心证据。

#include <QDateTime>
#include <QSqlDatabase>
#include <QtTest>

#include <condition_variable>
#include <future>
#include <mutex>

#include "server/services/order_service.h"
#include "tests/helpers/test_database.h"

class ReservationRaceTest final : public QObject
{
    Q_OBJECT

private:
    // 新契约夹具：预置新鲜设备读数+心跳（开始充电要求 ≤15 秒内有效读数）。
    static void seedFreshReading(TestDatabase &db, qint64 pileId,
                                const QString &energyKwh = QStringLiteral("1000.00"))
    {
        const QString now =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        QVERIFY2(db.exec(QStringLiteral(
            "INSERT INTO pile_telemetry(pile_id,recorded_at,temperature_c,voltage_v,"
            "current_a,power_w,energy_kwh) VALUES(?,?,25,220,16,3520,?)"),
            {pileId, now, energyKwh}), qPrintable(db.error()));
        QVERIFY2(db.exec(QStringLiteral(
            "UPDATE pile_info SET last_heartbeat_at=? WHERE id=?"),
            {now, pileId}), qPrintable(db.error()));
    }

private slots:
    void legacyChargingOrderRetainsSettlementRule()
    {
        TestDatabase db;
        QVERIFY2(db.isReady(), qPrintable(db.error()));
        const auto user = db.addUser(QStringLiteral("13800138996"));
        const auto station = db.addStation(QStringLiteral("旧站"), 23.1, 113.2, QStringLiteral("1.23"));
        const auto pile = db.addPile(station, QStringLiteral("LEGACY-CHARGING"), QStringLiteral("CHARGING"));
        // Existing pre-upgrade rows receive the migration's default flag (0).
        QVERIFY(db.exec(QStringLiteral("INSERT INTO charge_order(id,user_id,pile_id,status,energy_kwh) "
                                       "VALUES('LEGACY-CHARGING',?,?,'CHARGING','8.35')"), {user, pile}));
        const auto stopped = OrderService(db.database()).stop(QStringLiteral("LEGACY-CHARGING"));
        QVERIFY(stopped.ok);
        QCOMPARE(stopped.amountYuan.toString(), QStringLiteral("10.27"));
    }

    void failedStartRollsBackPriceSnapshot()
    {
        TestDatabase db;
        QVERIFY2(db.isReady(), qPrintable(db.error()));
        const auto user = db.addUser(QStringLiteral("13800138997"));
        const auto pile = db.addPile(db.addStation(), QStringLiteral("LOCK-ROLLBACK"));
        OrderService service(db.database());
        const auto order = service.reserve(user, pile, QStringLiteral("LOCK-ROLLBACK"));
        QVERIFY(order.ok);
        seedFreshReading(db, pile);
        QVERIFY(db.exec(QStringLiteral("CREATE TRIGGER reject_start BEFORE UPDATE OF status ON pile_info "
                                       "WHEN NEW.status='CHARGING' BEGIN SELECT RAISE(ABORT,'test failure'); END")));
        QCOMPARE(service.start(order.orderId).errorCode, QStringLiteral("DATABASE_ERROR"));
        QCOMPARE(db.scalar(QStringLiteral("SELECT price_locked FROM charge_order WHERE id=?"),
                           {order.orderId}).toInt(), 0);
        QCOMPARE(db.scalar(QStringLiteral("SELECT status FROM charge_order WHERE id=?"),
                           {order.orderId}).toString(), QStringLiteral("RESERVED"));
    }

    void chargingUsesStartPrice_data()
    {
        QTest::addColumn<QString>("price");
        QTest::addColumn<QString>("amount");
        QTest::newRow("decimal") << QStringLiteral("1.23") << QStringLiteral("10.27");
        QTest::newRow("free") << QStringLiteral("0.00") << QStringLiteral("0.00");
    }

    void chargingUsesStartPrice()
    {
        QFETCH(QString, price);
        QFETCH(QString, amount);
        TestDatabase db;
        QVERIFY2(db.isReady(), qPrintable(db.error()));
        const auto user = db.addUser(QStringLiteral("13800138994"));
        const auto station = db.addStation();
        const auto pile = db.addPile(station, QStringLiteral("PRICE-LOCK"));
        OrderService service(db.database());
        const auto order = service.reserve(user, pile, QStringLiteral("PRICE-LOCK"));
        QVERIFY(order.ok);
        seedFreshReading(db, pile);
        QVERIFY(db.exec(QStringLiteral("UPDATE station_info SET price_yuan_per_kwh=? WHERE id=?"),
                        {price, station}));
        const auto started = service.start(order.orderId);
        QVERIFY(started.ok);
        QCOMPARE(started.unitPriceYuanPerKwh.toString(), price);
        QVERIFY(db.exec(QStringLiteral("UPDATE station_info SET price_yuan_per_kwh='9.99' WHERE id=?"),
                        {station}));
        // Recreate the service to prove the snapshot is persisted, not cached in memory.
        OrderService restarted(db.database());
        QVERIFY(restarted.recordTelemetry(order.orderId,
                *DecimalValue::parseCanonical(QStringLiteral("8.35"))).ok);
        const auto stopped = restarted.stop(order.orderId);
        QVERIFY2(stopped.ok, qPrintable(stopped.errorCode));
        QCOMPARE(db.scalar(QStringLiteral("SELECT amount_yuan FROM charge_order WHERE id=?"),
                           {order.orderId}).toString(), amount);
        QCOMPARE(db.scalar(QStringLiteral("SELECT unit_price_yuan_per_kwh FROM charge_order WHERE id=?"),
                           {order.orderId}).toString(), price);
    }

    void reservationRequestCannotReuseAnotherActionId()
    {
        TestDatabase db;
        QVERIFY2(db.isReady(), qPrintable(db.error()));
        const auto user = db.addUser(QStringLiteral("13800138995"));
        const auto pile = db.addPile(db.addStation(), QStringLiteral("COLLISION"));
        QVERIFY(db.exec(QStringLiteral("INSERT INTO request_dedup(request_id,action,result_json) "
                                       "VALUES('COLLISION','RECHARGE','{}')")));
        const auto result = OrderService(db.database()).reserve(user, pile, QStringLiteral("COLLISION"));
        QVERIFY(!result.ok);
        QCOMPARE(result.errorCode, QStringLiteral("DUPLICATE_REQUEST"));
        QCOMPARE(db.scalar(QStringLiteral("SELECT COUNT(*) FROM charge_order")).toInt(), 0);
        QCOMPARE(db.scalar(QStringLiteral("SELECT status FROM pile_info WHERE id=?"), {pile}).toString(),
                 QStringLiteral("IDLE"));
    }

    void expiredFaultedReservationIsReleasedOnStart()
    {
        TestDatabase db;
        QVERIFY2(db.isReady(), qPrintable(db.error()));
        const auto user = db.addUser(QStringLiteral("13800138993"));
        const auto pile = db.addPile(db.addStation(), QStringLiteral("EXPIRED-FAULT"));
        QDateTime now(QDate(2026, 9, 9), QTime(10, 0), Qt::UTC);
        OrderService service(db.database(), [&now] { return now; });
        const auto order = service.reserve(user, pile, QStringLiteral("EXPIRED-FAULT"));
        QVERIFY(order.ok);
        QVERIFY(db.exec(QStringLiteral("UPDATE pile_info SET status='FAULT' WHERE id=?"), {pile}));
        now = now.addSecs(901);
        const auto result = service.start(order.orderId);
        QVERIFY(!result.ok);
        QCOMPARE(result.errorCode, QStringLiteral("ORDER_STATE_CONFLICT"));
        QCOMPARE(db.scalar(QStringLiteral("SELECT status FROM charge_order WHERE id=?"),
                           {order.orderId}).toString(), QStringLiteral("EXPIRED"));
        QCOMPARE(db.scalar(QStringLiteral("SELECT status FROM pile_info WHERE id=?"),
                           {pile}).toString(), QStringLiteral("FAULT"));
    }

    void unavailableReservedPileCanBeCanceled_data()
    {
        QTest::addColumn<QString>("status");
        QTest::newRow("fault") << QStringLiteral("FAULT");
        QTest::newRow("offline") << QStringLiteral("OFFLINE");
    }

    void unavailableReservedPileCanBeCanceled()
    {
        QFETCH(QString, status);
        TestDatabase db;
        QVERIFY2(db.isReady(), qPrintable(db.error()));
        const auto user = db.addUser(QStringLiteral("13800138991"));
        const auto pile = db.addPile(db.addStation(), QStringLiteral("CANCEL-UNAVAILABLE"));
        OrderService service(db.database());
        const auto order = service.reserve(user, pile, QStringLiteral("CANCEL-UNAVAILABLE"));
        QVERIFY(order.ok);
        QVERIFY(db.exec(QStringLiteral("UPDATE pile_info SET status=? WHERE id=?"), {status, pile}));
        const auto result = service.cancel(order.orderId);
        QVERIFY2(result.ok, qPrintable(result.errorCode));
        QCOMPARE(db.scalar(QStringLiteral("SELECT status FROM charge_order WHERE id=?"),
                           {order.orderId}).toString(), QStringLiteral("CANCELED"));
        QCOMPARE(db.scalar(QStringLiteral("SELECT status FROM pile_info WHERE id=?"),
                           {pile}).toString(), status);
    }

    void stationClosedAfterReservationCannotStart()
    {
        TestDatabase db;
        QVERIFY2(db.isReady(), qPrintable(db.error()));
        const auto user = db.addUser(QStringLiteral("13800138992"));
        const auto station = db.addStation();
        const auto pile = db.addPile(station, QStringLiteral("START-CLOSED"));
        OrderService service(db.database());
        const auto order = service.reserve(user, pile, QStringLiteral("START-CLOSED"));
        QVERIFY(order.ok);
        QVERIFY(db.exec(QStringLiteral("UPDATE station_info SET status='CLOSED' WHERE id=?"), {station}));
        const auto result = service.start(order.orderId);
        QVERIFY(!result.ok);
        QCOMPARE(result.errorCode, QStringLiteral("PILE_UNAVAILABLE"));
        QCOMPARE(db.scalar(QStringLiteral("SELECT status FROM charge_order WHERE id=?"),
                           {order.orderId}).toString(), QStringLiteral("RESERVED"));
        QVERIFY(service.cancel(order.orderId).ok);
    }

    void twoConnectionsCanOnlyReserveTheSamePileOnce()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 firstUser = testDb.addUser(QStringLiteral("13800138001"));
        const qint64 secondUser = testDb.addUser(QStringLiteral("13800138002"));
        const qint64 stationId = testDb.addStation();
        const qint64 pileId = testDb.addPile(stationId, QStringLiteral("PILE-RACE"));
        QVERIFY(firstUser > 0 && secondUser > 0 && pileId > 0);

        const QDateTime now(QDate(2026, 9, 4), QTime(10, 0), Qt::UTC);
        std::mutex gateMutex;
        std::condition_variable gate;
        int ready = 0;
        bool go = false;
        const auto attempt = [&](qint64 userId, const QString &requestId) {
            auto db = testDb.openConnection(QStringLiteral("race"));
            const QString connectionName = db.connectionName();
            {
                std::unique_lock lock(gateMutex);
                ++ready;
                gate.notify_all();
                gate.wait(lock, [&] { return go; });
            }
            bool ok = false;
            {
                OrderService service(db, [now] { return now; });
                ok = service.reserve(userId, pileId, requestId).ok;
            }
            db.close();
            db = {};
            QSqlDatabase::removeDatabase(connectionName);
            return ok;
        };

        auto first = std::async(std::launch::async, attempt, firstUser,
                                QStringLiteral("REQ-RACE-1"));
        auto second = std::async(std::launch::async, attempt, secondUser,
                                 QStringLiteral("REQ-RACE-2"));
        {
            std::unique_lock lock(gateMutex);
            gate.wait(lock, [&] { return ready == 2; });
            go = true;
        }
        gate.notify_all();

        QCOMPARE(static_cast<int>(first.get()) + static_cast<int>(second.get()), 1);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM charge_order WHERE pile_id=? AND status='RESERVED'"),
            {pileId}).toInt(), 1);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT status FROM pile_info WHERE id=?"), {pileId}).toString(),
            QStringLiteral("RESERVED"));
    }

    void frozenUserCannotReserveOrOccupyPile()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800138013"));
        const qint64 pileId = testDb.addPile(testDb.addStation(), QStringLiteral("PILE-FROZEN"));
        QVERIFY(userId > 0 && pileId > 0);
        QVERIFY(testDb.exec(QStringLiteral("UPDATE user_info SET status='FROZEN' WHERE id=?"),
                            {userId}));
        OrderService service(testDb.database());

        const auto result = service.reserve(userId, pileId, QStringLiteral("REQ-FROZEN"));

        QVERIFY(!result.ok);
        QCOMPARE(result.errorCode, QStringLiteral("USER_FROZEN"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM charge_order WHERE user_id=?"), {userId}).toInt(), 0);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT status FROM pile_info WHERE id=?"), {pileId}).toString(),
            QStringLiteral("IDLE"));
    }

    void closedStationPileCannotBeReserved()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800138014"));
        const qint64 stationId = testDb.addStation(QStringLiteral("停运站"));
        const qint64 pileId = testDb.addPile(stationId, QStringLiteral("PILE-CLOSED"));
        QVERIFY(testDb.exec(QStringLiteral("UPDATE station_info SET status='CLOSED' WHERE id=?"),
                            {stationId}));

        const auto result = OrderService(testDb.database()).reserve(
            userId, pileId, QStringLiteral("REQ-CLOSED"));

        QVERIFY(!result.ok);
        QCOMPARE(result.errorCode, QStringLiteral("PILE_UNAVAILABLE"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM charge_order WHERE pile_id=?"), {pileId}).toInt(), 0);
    }

    void userFrozenAfterReservationCannotStartCharging()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800138015"));
        const qint64 pileId = testDb.addPile(testDb.addStation(),
                                             QStringLiteral("PILE-FROZEN-START"));
        OrderService service(testDb.database());
        const auto reservation = service.reserve(
            userId, pileId, QStringLiteral("REQ-FROZEN-START"));
        QVERIFY(reservation.ok);
        QVERIFY(testDb.exec(QStringLiteral("UPDATE user_info SET status='FROZEN' WHERE id=?"),
                            {userId}));

        const auto result = service.start(reservation.orderId);

        QVERIFY(!result.ok);
        QCOMPARE(result.errorCode, QStringLiteral("USER_FROZEN"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM charge_order WHERE id=?"),
                               {reservation.orderId}).toString(), QStringLiteral("RESERVED"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM pile_info WHERE id=?"),
                               {pileId}).toString(), QStringLiteral("RESERVED"));
    }

    void expiredReservationReleasesPileUsingInjectedClock()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800138003"));
        const qint64 pileId = testDb.addPile(testDb.addStation(), QStringLiteral("PILE-EXP"));
        QDateTime now(QDate(2026, 9, 4), QTime(10, 0), Qt::UTC);
        OrderService service(testDb.database(), [&now] { return now; });
        const auto reservation = service.reserve(userId, pileId, QStringLiteral("REQ-EXP"));
        QVERIFY(reservation.ok);

        now = now.addSecs(16 * 60);
        QCOMPARE(service.expireReservations(), 1);
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM charge_order WHERE id=?"),
                               {reservation.orderId}).toString(), QStringLiteral("EXPIRED"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM pile_info WHERE id=?"),
                               {pileId}).toString(), QStringLiteral("IDLE"));
    }

    void telemetryCannotGoBackwardsAndStopFreezesDecimalCharge()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800138004"));
        const qint64 stationId = testDb.addStation(
            QStringLiteral("计费站"), 23.1, 113.2, QStringLiteral("1.23"));
        const qint64 pileId = testDb.addPile(stationId, QStringLiteral("PILE-METER"));
        const QDateTime now(QDate(2026, 9, 4), QTime(10, 0), Qt::UTC);
        OrderService service(testDb.database(), [now] { return now; });
        const auto reservation = service.reserve(userId, pileId, QStringLiteral("REQ-METER"));
        QVERIFY(reservation.ok);
        // 新契约：预置与注入时钟对齐的新鲜读数+心跳（设备累计基准 100.00）。
        QVERIFY2(testDb.exec(QStringLiteral(
            "INSERT INTO pile_telemetry(pile_id,recorded_at,temperature_c,voltage_v,"
            "current_a,power_w,energy_kwh) VALUES(?,?,25,220,16,3520,'100.00')"),
            {pileId, now.addSecs(-10).toString(Qt::ISODate)}),
            qPrintable(testDb.error()));
        QVERIFY2(testDb.exec(QStringLiteral(
            "UPDATE pile_info SET last_heartbeat_at=? WHERE id=?"),
            {now.addSecs(-10).toString(Qt::ISODate), pileId}), qPrintable(testDb.error()));
        QVERIFY(service.start(reservation.orderId).ok);
        QVERIFY(service.recordTelemetry(
            reservation.orderId,
            *DecimalValue::parseCanonical(QStringLiteral("8.35"))).ok);

        const auto backwards = service.recordTelemetry(
            reservation.orderId,
            *DecimalValue::parseCanonical(QStringLiteral("8.34")));
        QVERIFY(!backwards.ok);
        QCOMPARE(backwards.errorCode, QStringLiteral("PARAM_INVALID"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT energy_kwh FROM charge_order WHERE id=?"),
                               {reservation.orderId}).toString(), QStringLiteral("8.35"));

        const auto stopped = service.stop(reservation.orderId);
        QVERIFY(stopped.ok);
        QCOMPARE(stopped.energyKwh.toString(), QStringLiteral("8.35"));
        QCOMPARE(stopped.unitPriceYuanPerKwh.toString(), QStringLiteral("1.23"));
        QCOMPARE(stopped.amountYuan.toString(), QStringLiteral("10.27"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM charge_order WHERE id=?"),
                               {reservation.orderId}).toString(), QStringLiteral("PENDING_PAYMENT"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT unit_price_yuan_per_kwh FROM charge_order WHERE id=?"),
            {reservation.orderId}).toString(), QStringLiteral("1.23"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT amount_yuan FROM charge_order WHERE id=?"),
                               {reservation.orderId}).toString(), QStringLiteral("10.27"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM pile_info WHERE id=?"),
                               {pileId}).toString(), QStringLiteral("CHARGING"));
    }

    void cancellationReleasesReservedPile()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800138005"));
        const qint64 pileId = testDb.addPile(testDb.addStation(), QStringLiteral("PILE-CANCEL"));
        const QDateTime now(QDate(2026, 9, 4), QTime(10, 0), Qt::UTC);
        OrderService service(testDb.database(), [now] { return now; });
        const auto reservation = service.reserve(userId, pileId, QStringLiteral("REQ-CANCEL"));
        QVERIFY(reservation.ok);

        QVERIFY(service.cancel(reservation.orderId).ok);
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM charge_order WHERE id=?"),
                               {reservation.orderId}).toString(), QStringLiteral("CANCELED"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM pile_info WHERE id=?"),
                               {pileId}).toString(), QStringLiteral("IDLE"));
    }

    void activeReservationBlocksSecondOrderEvenIfPileStatusWasCorrupted()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 firstUser = testDb.addUser(QStringLiteral("13800138006"));
        const qint64 secondUser = testDb.addUser(QStringLiteral("13800138007"));
        const qint64 pileId = testDb.addPile(testDb.addStation(), QStringLiteral("PILE-GUARD"));
        const QDateTime now(QDate(2026, 9, 4), QTime(10, 0), Qt::UTC);
        OrderService service(testDb.database(), [now] { return now; });
        QVERIFY(service.reserve(firstUser, pileId, QStringLiteral("REQ-GUARD-1")).ok);
        QVERIFY(testDb.exec(QStringLiteral("UPDATE pile_info SET status='IDLE' WHERE id=?"),
                            {pileId}));

        const auto second = service.reserve(secondUser, pileId, QStringLiteral("REQ-GUARD-2"));
        QVERIFY(!second.ok);
        QCOMPARE(second.errorCode, QStringLiteral("PILE_UNAVAILABLE"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM charge_order WHERE pile_id=? AND status='RESERVED'"),
            {pileId}).toInt(), 1);
    }

    void schedulerExpiresReservationsAtDeadline()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800138008"));
        const qint64 pileId = testDb.addPile(testDb.addStation(), QStringLiteral("PILE-TIMER"));
        QDateTime now(QDate(2026, 9, 4), QTime(10, 0), Qt::UTC);
        OrderService service(testDb.database(), [&now] { return now; });
        const auto reservation = service.reserve(userId, pileId, QStringLiteral("REQ-TIMER"));
        QVERIFY(reservation.ok);
        now = now.addSecs(15 * 60);

        ReservationExpiryScheduler scheduler(service, 10);
        QTRY_COMPARE_WITH_TIMEOUT(testDb.scalar(QStringLiteral(
            "SELECT status FROM charge_order WHERE id=?"), {reservation.orderId}).toString(),
            QStringLiteral("EXPIRED"), 1000);
        QCOMPARE(scheduler.intervalMs(), 10);
    }

    void reservationRequestIsIdempotentAndParameterBound()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 firstUser = testDb.addUser(QStringLiteral("13800138009"));
        const qint64 secondUser = testDb.addUser(QStringLiteral("13800138010"));
        const qint64 stationId = testDb.addStation();
        const qint64 firstPile = testDb.addPile(stationId, QStringLiteral("PILE-IDEMP-1"));
        const qint64 secondPile = testDb.addPile(stationId, QStringLiteral("PILE-IDEMP-2"));
        const QDateTime now(QDate(2026, 9, 4), QTime(10, 0), Qt::UTC);
        OrderService service(testDb.database(), [now] { return now; });

        const auto first = service.reserve(firstUser, firstPile, QStringLiteral("REQ-IDEMP"));
        const auto replay = service.reserve(firstUser, firstPile, QStringLiteral("REQ-IDEMP"));
        const auto conflict = service.reserve(secondUser, secondPile, QStringLiteral("REQ-IDEMP"));

        QVERIFY(first.ok);
        QVERIFY(replay.ok);
        QCOMPARE(replay.orderId, first.orderId);
        QVERIFY(!conflict.ok);
        QCOMPARE(conflict.errorCode, QStringLiteral("DUPLICATE_REQUEST"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM charge_order WHERE status='RESERVED'" )).toInt(), 1);
    }

    void corruptedPileStateDoesNotBlockOtherExpirations()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 firstUser = testDb.addUser(QStringLiteral("13800138011"));
        const qint64 secondUser = testDb.addUser(QStringLiteral("13800138012"));
        const qint64 stationId = testDb.addStation();
        const qint64 firstPile = testDb.addPile(stationId, QStringLiteral("PILE-EXP-BAD"));
        const qint64 secondPile = testDb.addPile(stationId, QStringLiteral("PILE-EXP-GOOD"));
        QDateTime now(QDate(2026, 9, 4), QTime(10, 0), Qt::UTC);
        OrderService service(testDb.database(), [&now] { return now; });
        const auto first = service.reserve(firstUser, firstPile, QStringLiteral("REQ-EXP-BAD"));
        const auto second = service.reserve(secondUser, secondPile, QStringLiteral("REQ-EXP-GOOD"));
        QVERIFY(first.ok && second.ok);
        QVERIFY(testDb.exec(QStringLiteral("UPDATE pile_info SET status='FAULT' WHERE id=?"),
                            {firstPile}));
        now = now.addSecs(16 * 60);

        QCOMPARE(service.expireReservations(), 2);
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM charge_order WHERE id=?"),
                               {first.orderId}).toString(), QStringLiteral("EXPIRED"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM charge_order WHERE id=?"),
                               {second.orderId}).toString(), QStringLiteral("EXPIRED"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM pile_info WHERE id=?"),
                               {firstPile}).toString(), QStringLiteral("FAULT"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM pile_info WHERE id=?"),
                               {secondPile}).toString(), QStringLiteral("IDLE"));
    }

};

QTEST_GUILESS_MAIN(ReservationRaceTest)
#include "reservation_race_test.moc"
