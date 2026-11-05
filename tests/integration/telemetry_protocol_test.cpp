// 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
// 遥测协议集成测试：通过真实 TCP 上报遥测，并验证后台离线扫描与预约回收定时任务实际运行——
// 定时任务不只是类存在，运行中的服务会实际执行。

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QtTest>

#include "server/network/tcp_server.h"
#include "server/services/admin_service.h"
#include "server/services/telemetry_service.h"
#include "shared/protocol/frame_codec.h"
#include "tests/helpers/test_database.h"

class TelemetryProtocolTest final : public QObject
{
    Q_OBJECT

private:
    static QJsonObject send(QTcpSocket &socket, const QString &requestId,
                            const QString &action, const QJsonObject &payload)
    {
        const QJsonObject request{{QStringLiteral("version"), QStringLiteral("1.0")},
                                  {QStringLiteral("requestId"), requestId},
                                  {QStringLiteral("action"), action},
                                  {QStringLiteral("payload"), payload}};
        socket.write(FrameCodec::encode(
            QJsonDocument(request).toJson(QJsonDocument::Compact)));
        socket.flush();
        if (!socket.waitForReadyRead(2000)) {
            return {};
        }
        FrameCodec codec;
        auto frames = codec.feed(socket.readAll());
        while (frames.isEmpty() && socket.waitForReadyRead(200)) {
            frames.append(codec.feed(socket.readAll()));
        }
        return frames.isEmpty() ? QJsonObject{}
                                : QJsonDocument::fromJson(frames.first()).object();
    }

private slots:
    void acceptsDecimalTelemetryAndKeepsConnectionAfterInvalidDecimal()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 pileId = testDb.addPile(testDb.addStation(),
                                             QStringLiteral("PILE-PROTOCOL-001"));
        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));

        const QString validRecordedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        QJsonObject payload{{QStringLiteral("pileId"), pileId},
                            {QStringLiteral("recordedAt"), validRecordedAt},
                            {QStringLiteral("temperatureC"), 35.0},
                            {QStringLiteral("voltageV"), 380.0},
                            {QStringLiteral("currentA"), 120.0},
                            {QStringLiteral("powerW"), 45600},
                            {QStringLiteral("energyKwh"), 1.0}};
        const auto invalid = send(socket, QStringLiteral("TEL-BAD-1"),
                                  QStringLiteral("PILE_TELEMETRY"), payload);
        QVERIFY(!invalid.value(QStringLiteral("ok")).toBool());
        QCOMPARE(invalid.value(QStringLiteral("code")).toString(),
                 QStringLiteral("PARAM_INVALID"));

        payload.insert(QStringLiteral("energyKwh"), QStringLiteral("1.00"));
        payload.insert(QStringLiteral("recordedAt"),
                       QDateTime::currentDateTimeUtc().toString(
                           QStringLiteral("yyyy-MM-dd'T'HH:mm:ss")));
        const auto missingTimezone = send(socket, QStringLiteral("TEL-BAD-2"),
                                          QStringLiteral("PILE_TELEMETRY"), payload);
        QVERIFY(!missingTimezone.value(QStringLiteral("ok")).toBool());
        QCOMPARE(missingTimezone.value(QStringLiteral("code")).toString(),
                 QStringLiteral("PARAM_INVALID"));

        payload.insert(QStringLiteral("recordedAt"), validRecordedAt);
        const auto accepted = send(socket, QStringLiteral("TEL-OK-1"),
                                   QStringLiteral("PILE_TELEMETRY"), payload);
        QVERIFY(accepted.value(QStringLiteral("ok")).toBool());
        QCOMPARE(accepted.value(QStringLiteral("data")).toObject()
                     .value(QStringLiteral("energyKwh")).toString(),
                 QStringLiteral("1.00"));
        QTRY_COMPARE_WITH_TIMEOUT(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM pile_telemetry WHERE pile_id=?"), {pileId}).toInt(), 1,
            2000);
    }

    void pileLifetimeMeterDrivesTheCompleteOrderFlowTwice()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));

        for (int run = 1; run <= 2; ++run) {
            const qint64 userId = testDb.addUser(
                QStringLiteral("1380013830%1").arg(run));
            const qint64 pileId = testDb.addPile(
                testDb.addStation(QStringLiteral("累计表联调站%1").arg(run)),
                QStringLiteral("PILE-E2E-METER-%1").arg(run));
            // 2026-09-13（第二阶段 T08 修复）：订单动作自 M1-02 起要求用户会话，
            // 先 USER_LOGIN 取 userToken，再携带令牌执行预约/开始/停止/支付。
            const auto login = send(socket, QStringLiteral("LOGIN-%1").arg(run),
                                    QStringLiteral("USER_LOGIN"),
                                    {{QStringLiteral("phone"),
                                      QStringLiteral("1380013830%1").arg(run)}});
            QVERIFY(login.value(QStringLiteral("ok")).toBool());
            const QString userToken = login.value(QStringLiteral("data")).toObject()
                                          .value(QStringLiteral("userToken")).toString();
            QVERIFY(!userToken.isEmpty());
            const QDateTime baselineAt = QDateTime::currentDateTimeUtc().addSecs(run * 20);
            const auto telemetryPayload = [pileId](const QDateTime &recordedAt,
                                                   const QString &energy) {
                return QJsonObject{
                    {QStringLiteral("pileId"), pileId},
                    {QStringLiteral("recordedAt"), recordedAt.toString(Qt::ISODate)},
                    {QStringLiteral("temperatureC"), 35.0},
                    {QStringLiteral("voltageV"), 230.0},
                    {QStringLiteral("currentA"), 30.0},
                    {QStringLiteral("powerW"), 6900},
                    {QStringLiteral("energyKwh"), energy},
                    {QStringLiteral("dataMode"), QStringLiteral("SIMULATED")}
                };
            };

            QVERIFY(send(socket, QStringLiteral("BASE-%1").arg(run),
                         QStringLiteral("PILE_TELEMETRY"),
                         telemetryPayload(baselineAt, QStringLiteral("100.00")))
                        .value(QStringLiteral("ok")).toBool());
            const auto reserved = send(socket, QStringLiteral("RESERVE-%1").arg(run),
                                       QStringLiteral("ORDER_RESERVE"), {
                {QStringLiteral("userId"), userId},
                {QStringLiteral("pileId"), pileId},
                {QStringLiteral("userToken"), userToken}
            });
            QVERIFY(reserved.value(QStringLiteral("ok")).toBool());
            const QString orderId = reserved.value(QStringLiteral("data")).toObject()
                                        .value(QStringLiteral("orderId")).toString();
            QVERIFY(send(socket, QStringLiteral("START-%1").arg(run),
                         QStringLiteral("ORDER_START"),
                         {{QStringLiteral("orderId"), orderId},
                          {QStringLiteral("userToken"), userToken}})
                        .value(QStringLiteral("ok")).toBool());

            const QDateTime chargingAt = baselineAt.addSecs(5);
            const auto accepted = send(
                socket, QStringLiteral("PILE-CHARGING-%1").arg(run),
                QStringLiteral("PILE_TELEMETRY"),
                telemetryPayload(chargingAt, QStringLiteral("100.10")));
            QVERIFY(accepted.value(QStringLiteral("ok")).toBool());
            QCOMPARE(accepted.value(QStringLiteral("data")).toObject()
                         .value(QStringLiteral("orderEnergyKwh")).toString(),
                     QStringLiteral("0.10"));
            const auto duplicate = send(
                socket, QStringLiteral("PILE-DUPLICATE-%1").arg(run),
                QStringLiteral("PILE_TELEMETRY"),
                telemetryPayload(chargingAt, QStringLiteral("100.10")));
            QVERIFY(duplicate.value(QStringLiteral("ok")).toBool());
            QVERIFY(duplicate.value(QStringLiteral("data")).toObject()
                        .value(QStringLiteral("duplicate")).toBool());
            QCOMPARE(testDb.scalar(QStringLiteral(
                "SELECT COUNT(*) FROM pile_telemetry WHERE pile_id=?"), {pileId}).toInt(), 2);

            const auto stopped = send(socket, QStringLiteral("STOP-%1").arg(run),
                                      QStringLiteral("ORDER_STOP"),
                                      {{QStringLiteral("orderId"), orderId},
                                       {QStringLiteral("userToken"), userToken}});
            QVERIFY(stopped.value(QStringLiteral("ok")).toBool());
            QCOMPARE(stopped.value(QStringLiteral("data")).toObject()
                         .value(QStringLiteral("energyKwh")).toString(),
                     QStringLiteral("0.10"));
            QCOMPARE(stopped.value(QStringLiteral("data")).toObject()
                         .value(QStringLiteral("amountYuan")).toString(),
                     QStringLiteral("0.12"));
            QVERIFY(send(socket, QStringLiteral("PAY-%1").arg(run),
                         QStringLiteral("WALLET_PAY"),
                         {{QStringLiteral("orderId"), orderId},
                          {QStringLiteral("userToken"), userToken}})
                        .value(QStringLiteral("ok")).toBool());
            QCOMPARE(testDb.scalar(QStringLiteral(
                "SELECT status FROM charge_order WHERE id=?"), {orderId}).toString(),
                     QStringLiteral("FINISHED"));
        }
    }

    void backgroundMonitorMarksStalePileOffline()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 pileId = testDb.addPile(testDb.addStation(),
                                             QStringLiteral("PILE-MONITOR-001"));
        const QString staleAt = QDateTime::currentDateTimeUtc().addSecs(-20)
                                    .toString(Qt::ISODate);
        QVERIFY(testDb.exec(QStringLiteral(
            "UPDATE pile_info SET last_heartbeat_at=? WHERE id=?"), {staleAt, pileId}));
        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));

        QTRY_COMPARE_WITH_TIMEOUT(testDb.scalar(QStringLiteral(
            "SELECT status FROM pile_info WHERE id=?"), {pileId}).toString(),
            QStringLiteral("OFFLINE"), 7000);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM alarm_event WHERE pile_id=? AND alarm_type='OFFLINE'"),
            {pileId}).toInt(), 1);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM maintenance_ticket t JOIN alarm_event a ON a.id=t.alarm_id "
            "WHERE a.pile_id=? AND a.alarm_type='OFFLINE'"), {pileId}).toInt(), 1);
    }

    void backgroundTasksExpireOverdueReservations()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800138201"));
        const qint64 pileId = testDb.addPile(testDb.addStation(),
                                             QStringLiteral("PILE-AUTO-EXPIRE"),
                                             QStringLiteral("RESERVED"));
        QVERIFY(testDb.exec(QStringLiteral(
            "INSERT INTO charge_order(id,user_id,pile_id,status,reserved_until,created_at) "
            "VALUES('ORDER-AUTO-EXPIRE',?,?,'RESERVED',?,?)"),
            {userId, pileId,
             QDateTime::currentDateTimeUtc().addSecs(-60).toString(Qt::ISODate),
             QDateTime::currentDateTimeUtc().addSecs(-120).toString(Qt::ISODate)}));
        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));

        QTRY_COMPARE_WITH_TIMEOUT(testDb.scalar(QStringLiteral(
            "SELECT status FROM charge_order WHERE id='ORDER-AUTO-EXPIRE'" )).toString(),
            QStringLiteral("EXPIRED"), 7000);
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM pile_info WHERE id=?"),
                               {pileId}).toString(), QStringLiteral("IDLE"));
    }

    void adminRoutesEnforceAuthenticationAndCompleteTicketFlow()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        AdminService admin(testDb.database());
        QVERIFY(admin.ensureDefaultAdmin().ok);
        const qint64 pileId = testDb.addPile(testDb.addStation(),
                                             QStringLiteral("PILE-ADMIN-FLOW"));
        const QDateTime now = QDateTime::currentDateTimeUtc();
        TelemetryService telemetry(testDb.database());
        const auto energy = DecimalValue::parseCanonical(QStringLiteral("1.00"));
        QVERIFY(energy);
        QVERIFY(telemetry.record({pileId, now, 75.0, 380.0, 120.0, 45600,
                                  *energy}).ok);
        const qint64 alarmId = testDb.scalar(QStringLiteral(
            "SELECT id FROM alarm_event WHERE pile_id=? AND alarm_type='OVERHEAT'"),
            {pileId}).toLongLong();
        const qint64 ticketId = testDb.scalar(QStringLiteral(
            "SELECT id FROM maintenance_ticket WHERE alarm_id=?"), {alarmId}).toLongLong();

        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));

        const auto login = send(socket, QStringLiteral("ADMIN-LOGIN-1"),
                                QStringLiteral("ADMIN_LOGIN"), {
            {QStringLiteral("username"), QStringLiteral("admin")},
            {QStringLiteral("password"), QStringLiteral("123456")}
        });
        QVERIFY(login.value(QStringLiteral("ok")).toBool());
        const QString token = login.value(QStringLiteral("data")).toObject()
                                  .value(QStringLiteral("adminToken")).toString();
        QVERIFY(!token.isEmpty());

        const auto unauthorized = send(socket, QStringLiteral("ALARM-ACK-BAD"),
                                       QStringLiteral("ADMIN_ALARM_ACK"), {
            {QStringLiteral("alarmId"), alarmId}
        });
        QCOMPARE(unauthorized.value(QStringLiteral("code")).toString(),
                 QStringLiteral("AUTH_INVALID"));
        QVERIFY(send(socket, QStringLiteral("ALARM-ACK-1"),
                     QStringLiteral("ADMIN_ALARM_ACK"), {
            {QStringLiteral("adminToken"), token},
            {QStringLiteral("alarmId"), alarmId}
        }).value(QStringLiteral("ok")).toBool());
        QVERIFY(send(socket, QStringLiteral("TICKET-START-1"),
                     QStringLiteral("ADMIN_TICKET_START"), {
            {QStringLiteral("adminToken"), token},
            {QStringLiteral("ticketId"), ticketId},
            {QStringLiteral("assignee"), QStringLiteral("engineer-a")}
        }).value(QStringLiteral("ok")).toBool());
        QVERIFY(send(socket, QStringLiteral("TICKET-RESOLVE-1"),
                     QStringLiteral("ADMIN_TICKET_RESOLVE"), {
            {QStringLiteral("adminToken"), token},
            {QStringLiteral("ticketId"), ticketId},
            {QStringLiteral("notes"), QStringLiteral("散热模块已恢复")}
        }).value(QStringLiteral("ok")).toBool());
        const auto notRecovered = send(socket, QStringLiteral("TICKET-CLOSE-BAD"),
                                       QStringLiteral("ADMIN_TICKET_CLOSE"), {
            {QStringLiteral("adminToken"), token},
            {QStringLiteral("ticketId"), ticketId}
        });
        QCOMPARE(notRecovered.value(QStringLiteral("code")).toString(),
                 QStringLiteral("DEVICE_NOT_RECOVERED"));

        QVERIFY(testDb.exec(QStringLiteral("UPDATE pile_info SET status='IDLE' WHERE id=?"),
                            {pileId}));
        QVERIFY(send(socket, QStringLiteral("TICKET-CLOSE-1"),
                     QStringLiteral("ADMIN_TICKET_CLOSE"), {
            {QStringLiteral("adminToken"), token},
            {QStringLiteral("ticketId"), ticketId}
        }).value(QStringLiteral("ok")).toBool());
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT status FROM maintenance_ticket WHERE id=?"), {ticketId}).toString(),
            QStringLiteral("CLOSED"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT status FROM alarm_event WHERE id=?"), {alarmId}).toString(),
            QStringLiteral("CLOSED"));
    }

    void overheatAlarmTicketAndRecoveryFlowRunsTwice()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        AdminService admin(testDb.database());
        QVERIFY(admin.ensureDefaultAdmin().ok);
        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));
        const auto login = send(socket, QStringLiteral("ADMIN-LOGIN-REPEAT"),
                                QStringLiteral("ADMIN_LOGIN"), {
            {QStringLiteral("username"), QStringLiteral("admin")},
            {QStringLiteral("password"), QStringLiteral("123456")}
        });
        const QString token = login.value(QStringLiteral("data")).toObject()
                                  .value(QStringLiteral("adminToken")).toString();
        QVERIFY(!token.isEmpty());

        for (int run = 1; run <= 2; ++run) {
            const qint64 pileId = testDb.addPile(
                testDb.addStation(QStringLiteral("故障联调站%1").arg(run)),
                QStringLiteral("PILE-E2E-HOT-%1").arg(run));
            const QDateTime occurredAt = QDateTime::currentDateTimeUtc().addSecs(run * 20);
            const auto pilePayload = [pileId](const QDateTime &recordedAt,
                                              double temperature,
                                              const QString &energy) {
                return QJsonObject{
                    {QStringLiteral("pileId"), pileId},
                    {QStringLiteral("recordedAt"), recordedAt.toString(Qt::ISODate)},
                    {QStringLiteral("temperatureC"), temperature},
                    {QStringLiteral("voltageV"), 380.0},
                    {QStringLiteral("currentA"), temperature >= 70.0 ? 0.0 : 30.0},
                    {QStringLiteral("powerW"), temperature >= 70.0 ? 0 : 11400},
                    {QStringLiteral("energyKwh"), energy},
                    {QStringLiteral("dataMode"), QStringLiteral("SIMULATED")}
                };
            };
            QVERIFY(send(socket, QStringLiteral("HOT-%1").arg(run),
                         QStringLiteral("PILE_TELEMETRY"),
                         pilePayload(occurredAt, 71.0, QStringLiteral("200.00")))
                        .value(QStringLiteral("ok")).toBool());
            const qint64 alarmId = testDb.scalar(QStringLiteral(
                "SELECT id FROM alarm_event WHERE pile_id=? AND alarm_type='OVERHEAT'"),
                {pileId}).toLongLong();
            const qint64 ticketId = testDb.scalar(QStringLiteral(
                "SELECT id FROM maintenance_ticket WHERE alarm_id=?"), {alarmId}).toLongLong();
            QVERIFY(alarmId > 0 && ticketId > 0);
            QVERIFY(send(socket, QStringLiteral("ACK-%1").arg(run),
                         QStringLiteral("ADMIN_ALARM_ACK"), {
                {QStringLiteral("adminToken"), token},
                {QStringLiteral("alarmId"), alarmId}
            }).value(QStringLiteral("ok")).toBool());
            QVERIFY(send(socket, QStringLiteral("TICKET-START-%1").arg(run),
                         QStringLiteral("ADMIN_TICKET_START"), {
                {QStringLiteral("adminToken"), token},
                {QStringLiteral("ticketId"), ticketId},
                {QStringLiteral("assignee"), QStringLiteral("member3-e2e")}
            }).value(QStringLiteral("ok")).toBool());
            QVERIFY(send(socket, QStringLiteral("RECOVER-%1").arg(run),
                         QStringLiteral("PILE_TELEMETRY"),
                         pilePayload(occurredAt.addSecs(5), 35.0, QStringLiteral("200.00")))
                        .value(QStringLiteral("ok")).toBool());
            QCOMPARE(testDb.scalar(QStringLiteral(
                "SELECT status FROM pile_info WHERE id=?"), {pileId}).toString(),
                     QStringLiteral("IDLE"));
            QVERIFY(send(socket, QStringLiteral("TICKET-RESOLVE-%1").arg(run),
                         QStringLiteral("ADMIN_TICKET_RESOLVE"), {
                {QStringLiteral("adminToken"), token},
                {QStringLiteral("ticketId"), ticketId},
                {QStringLiteral("notes"), QStringLiteral("自动联调恢复")}
            }).value(QStringLiteral("ok")).toBool());
            QVERIFY(send(socket, QStringLiteral("TICKET-CLOSE-%1").arg(run),
                         QStringLiteral("ADMIN_TICKET_CLOSE"), {
                {QStringLiteral("adminToken"), token},
                {QStringLiteral("ticketId"), ticketId}
            }).value(QStringLiteral("ok")).toBool());
            QCOMPARE(testDb.scalar(QStringLiteral(
                "SELECT status FROM maintenance_ticket WHERE id=?"), {ticketId}).toString(),
                     QStringLiteral("CLOSED"));
        }
    }
};

QTEST_GUILESS_MAIN(TelemetryProtocolTest)
#include "telemetry_protocol_test.moc"
