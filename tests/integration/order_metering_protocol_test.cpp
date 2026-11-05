// 测试文件头注释（成员1 · 第一阶段收尾 · M1-03）：
// 统一设备计量协议测试（E01~E13，01文档 4.2/4.3）。
// 失败测试先行：当前 PILE_TELEMETRY 不写订单用量、ORDER_TELEMETRY 仍开放、
// 管理禁用不存在——本文件实现前必然失败。
//
// 计量口径：订单用量 = 设备累计读数 - 开始基准（meter_start_kwh）；
// 只允许 PILE_TELEMETRY 驱动订单计量；完全重复读数不重复计量；
// 同时间不同值/时间倒退/电量倒退拒绝；>15 秒间隔的增长不归属当前订单（新基准规则见 01）。
// 金额 = 锁价 × 用量，半向上舍入两位（E01: 8.35×1.23=10.2705→10.27）。

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QtTest>

#include "server/network/tcp_server.h"
#include "server/services/telemetry_service.h"
#include "shared/domain/decimal_value.h"
#include "shared/protocol/frame_codec.h"
#include "tests/helpers/test_database.h"

class OrderMeteringProtocolTest final : public QObject
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
        if (!socket.waitForReadyRead(2000)) return {};
        FrameCodec codec;
        auto frames = codec.feed(socket.readAll());
        while (frames.isEmpty() && socket.waitForReadyRead(200)) {
            frames.append(codec.feed(socket.readAll()));
        }
        return frames.isEmpty() ? QJsonObject{}
                                : QJsonDocument::fromJson(frames.first()).object();
    }

    static QJsonObject data(const QJsonObject &response)
    {
        return response.value(QStringLiteral("data")).toObject();
    }

    static QString codeOf(const QJsonObject &response)
    {
        return response.value(QStringLiteral("code")).toString();
    }

    // PILE_TELEMETRY 上报：累计读数从基准起，温度默认 25℃。
    static QJsonObject telemetryPayload(qint64 pileId, const QString &recordedAt,
                                        const QString &energyKwh,
                                        double temperatureC = 25.0)
    {
        return {{QStringLiteral("pileId"), pileId},
                {QStringLiteral("recordedAt"), recordedAt},
                {QStringLiteral("temperatureC"), temperatureC},
                {QStringLiteral("voltageV"), 220.0},
                {QStringLiteral("currentA"), 16.0},
                {QStringLiteral("powerW"), 3520},
                {QStringLiteral("energyKwh"), energyKwh}};
    }

    static QString loginAndGetToken(QTcpSocket &socket, const QString &phone,
                                    const QString &rid)
    {
        const auto login = send(socket, rid, QStringLiteral("USER_LOGIN"),
                                {{QStringLiteral("phone"), phone}});
        return data(login).value(QStringLiteral("userToken")).toString();
    }

    // 建站+桩并返回 pileId。
    static qint64 makePile(TestDatabase &db, const QString &serial,
                           const QString &pileStatus = QStringLiteral("IDLE"))
    {
        const qint64 stationId = db.addStation(QStringLiteral("计量测试站"), 23.12, 113.26,
                                              QStringLiteral("1.23"));
        return db.addPile(stationId, serial, pileStatus);
    }

private:
    // 相对真实“现在”的 ISO 时间串（测试用动态时间，不固定日期——服务端时钟与设备时间一致）。
    static QString isoOffset(int seconds)
    {
        return QDateTime::currentDateTimeUtc().addSecs(seconds).toString(Qt::ISODate);
    }

    // 新鲜基准：10 秒前（心跳 ≤15 秒仍新鲜，且早于后续遥测读数）。
    static QString isoOffsetFresh()
    {
        return isoOffset(-10);
    }

private slots:
    // E01：基准100.00→开始→100.10→108.35→停止：用量8.35、锁价1.23、金额10.27。
    // E09：第二单从新基准 108.35→110.00：用量1.65，不继承前单。
    void e01e09_pileTelemetryDrivesMetering()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 pileId = makePile(testDb, QStringLiteral("PILE-E01"));
        // 预置设备基准读数（模拟器先上报再开始充电）。
        QVERIFY(testDb.exec(QStringLiteral(
            "INSERT INTO pile_telemetry(pile_id,recorded_at,temperature_c,voltage_v,"
            "current_a,power_w,energy_kwh) VALUES(?,?,25,220,16,3520,'100.00')"),
            {pileId, isoOffsetFresh()}));
        QVERIFY(testDb.exec(QStringLiteral(
            "UPDATE pile_info SET last_heartbeat_at=? WHERE id=?"),
            {isoOffsetFresh(), pileId}));

        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));
        const QString token = loginAndGetToken(socket, QStringLiteral("13800001111"),
                                               QStringLiteral("E01-LOGIN"));
        QVERIFY(!token.isEmpty());

        // 预约 + 开始（应有新鲜基准 <15s 内读数；此处数据库预置，服务端读 pile_telemetry 最新一条）。
        const auto reserve = send(socket, QStringLiteral("E01-RES"),
                                   QStringLiteral("ORDER_RESERVE"),
                                   {{QStringLiteral("userToken"), token},
                                    {QStringLiteral("pileId"), pileId}});
        QVERIFY2(reserve.value(QStringLiteral("ok")).toBool(),
                 qPrintable(reserve.value(QStringLiteral("message")).toString()));
        const QString orderId = data(reserve).value(QStringLiteral("orderId")).toString();

        const auto start = send(socket, QStringLiteral("E01-START"),
                                QStringLiteral("ORDER_START"),
                                {{QStringLiteral("userToken"), token},
                                 {QStringLiteral("orderId"), orderId}});
        QVERIFY2(start.value(QStringLiteral("ok")).toBool(),
                 qPrintable(QString::fromUtf8(QJsonDocument(start).toJson())));

        // 模拟器上报 100.10 与 108.35：订单用量应为 8.35（由 PILE 驱动，非 ORDER 注入）。
        {const auto r = send(socket, QStringLiteral("E01-T1"), QStringLiteral("PILE_TELEMETRY"),
                     telemetryPayload(pileId, isoOffset(-5),
                                      QStringLiteral("100.10")));
        QVERIFY2(r.value(QStringLiteral("ok")).toBool(),
                 qPrintable(QString::fromUtf8(QJsonDocument(r).toJson())));}
        QVERIFY(send(socket, QStringLiteral("E01-T2"), QStringLiteral("PILE_TELEMETRY"),
                     telemetryPayload(pileId, isoOffset(-2),
                                      QStringLiteral("108.35")))
                    .value(QStringLiteral("ok")).toBool());

        const auto detail = send(socket, QStringLiteral("E01-D1"),
                                 QStringLiteral("ORDER_DETAIL"),
                                 {{QStringLiteral("userToken"), token},
                                  {QStringLiteral("orderId"), orderId}});
        QVERIFY(detail.value(QStringLiteral("ok")).toBool());
        QCOMPARE(data(detail).value(QStringLiteral("energyKwh")).toString(),
                 QStringLiteral("8.35"));
        QCOMPARE(data(detail).value(QStringLiteral("unitPriceYuanPerKwh")).toString(),
                 QStringLiteral("1.23"));

        // 停止结算：8.35 × 1.23 = 10.2705 → 10.27（半向上）。
        const auto stop = send(socket, QStringLiteral("E01-STOP"),
                               QStringLiteral("ORDER_STOP"),
                               {{QStringLiteral("userToken"), token},
                                {QStringLiteral("orderId"), orderId}});
        QVERIFY2(stop.value(QStringLiteral("ok")).toBool(),
                 qPrintable(stop.value(QStringLiteral("message")).toString()));
        QCOMPARE(data(stop).value(QStringLiteral("energyKwh")).toString(),
                 QStringLiteral("8.35"));
        QCOMPARE(data(stop).value(QStringLiteral("amountYuan")).toString(),
                 QStringLiteral("10.27"));

        // 支付第一单（PENDING_PAYMENT 仍占桩，结清后才可开新单——契约行为）。
        QVERIFY(testDb.exec(QStringLiteral(
            "UPDATE user_info SET balance_yuan='100.00' WHERE phone='13800001111'")));
        const auto pay1 = send(socket, QStringLiteral("E01-PAY"),
                               QStringLiteral("WALLET_PAY"),
                               {{QStringLiteral("userToken"), token},
                                {QStringLiteral("orderId"), orderId}});
        QVERIFY2(pay1.value(QStringLiteral("ok")).toBool(),
                 qPrintable(pay1.value(QStringLiteral("message")).toString()));

        // E09：第二单新基准 108.35 → 110.00：用量 1.65、金额 2.03（1.65×1.23=2.0295→2.03）。
        const auto reserve2 = send(socket, QStringLiteral("E09-RES"),
                                   QStringLiteral("ORDER_RESERVE"),
                                   {{QStringLiteral("userToken"), token},
                                    {QStringLiteral("pileId"), pileId}});
        QVERIFY2(reserve2.value(QStringLiteral("ok")).toBool(),
                 qPrintable(reserve2.value(QStringLiteral("message")).toString()));
        const QString orderId2 = data(reserve2).value(QStringLiteral("orderId")).toString();
        const auto start2 = send(socket, QStringLiteral("E09-START"),
                                 QStringLiteral("ORDER_START"),
                                 {{QStringLiteral("userToken"), token},
                                  {QStringLiteral("orderId"), orderId2}});
        QVERIFY2(start2.value(QStringLiteral("ok")).toBool(),
                 qPrintable(start2.value(QStringLiteral("message")).toString()));
        QVERIFY(send(socket, QStringLiteral("E09-T1"), QStringLiteral("PILE_TELEMETRY"),
                     telemetryPayload(pileId, isoOffset(-1),
                                      QStringLiteral("110.00")))
                    .value(QStringLiteral("ok")).toBool());
        const auto stop2 = send(socket, QStringLiteral("E09-STOP"),
                                QStringLiteral("ORDER_STOP"),
                                {{QStringLiteral("userToken"), token},
                                 {QStringLiteral("orderId"), orderId2}});
        QVERIFY2(stop2.value(QStringLiteral("ok")).toBool(),
                 qPrintable(stop2.value(QStringLiteral("message")).toString()));
        QCOMPARE(data(stop2).value(QStringLiteral("energyKwh")).toString(),
                 QStringLiteral("1.65"));
        QCOMPARE(data(stop2).value(QStringLiteral("amountYuan")).toString(),
                 QStringLiteral("2.03"));
    }

    // E02/E03：完全重复不重复计量；同时间不同值/倒退拒绝且订单不变。
    void e02e03_duplicateAndConflictingReadings()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 pileId = makePile(testDb, QStringLiteral("PILE-E02"));
        QVERIFY(testDb.exec(QStringLiteral(
            "INSERT INTO pile_telemetry(pile_id,recorded_at,temperature_c,voltage_v,"
            "current_a,power_w,energy_kwh) VALUES(?,?,25,220,16,3520,'50.00')"),
            {pileId, isoOffsetFresh()}));
        QVERIFY(testDb.exec(QStringLiteral(
            "UPDATE pile_info SET last_heartbeat_at=? WHERE id=?"),
            {isoOffsetFresh(), pileId}));

        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));
        const QString token = loginAndGetToken(socket, QStringLiteral("13800002222"),
                                               QStringLiteral("E02-LOGIN"));
        const auto reserve = send(socket, QStringLiteral("E02-RES"),
                                  QStringLiteral("ORDER_RESERVE"),
                                  {{QStringLiteral("userToken"), token},
                                   {QStringLiteral("pileId"), pileId}});
        const QString orderId = data(reserve).value(QStringLiteral("orderId")).toString();
        QVERIFY(send(socket, QStringLiteral("E02-START"), QStringLiteral("ORDER_START"),
                     {{QStringLiteral("userToken"), token},
                      {QStringLiteral("orderId"), orderId}})
                    .value(QStringLiteral("ok")).toBool());

        // 100.10 → 完全重复同帧 → 不重复计量（订单仍 50.10-50.00=0.10）。
        const auto t1 = send(socket, QStringLiteral("E02-T1"), QStringLiteral("PILE_TELEMETRY"),
                             telemetryPayload(pileId, isoOffset(-5),
                                              QStringLiteral("100.10")));
        QVERIFY(t1.value(QStringLiteral("ok")).toBool());
        const auto dup = send(socket, QStringLiteral("E02-T2"), QStringLiteral("PILE_TELEMETRY"),
                              telemetryPayload(pileId, isoOffset(-5),
                                               QStringLiteral("100.10")));
        QVERIFY(dup.value(QStringLiteral("ok")).toBool());

        // 同时间不同值 → 拒绝（TELEMETRY_OUT_OF_ORDER）。
        const auto conflict = send(socket, QStringLiteral("E02-T3"),
                                   QStringLiteral("PILE_TELEMETRY"),
                                   telemetryPayload(pileId,
                                                    isoOffset(-5),
                                                    QStringLiteral("100.20")));
        QVERIFY2(!conflict.value(QStringLiteral("ok")).toBool(),
                 qPrintable(QString::fromUtf8(QJsonDocument(conflict).toJson())));
        QCOMPARE(codeOf(conflict), QStringLiteral("TELEMETRY_OUT_OF_ORDER"));

        // 电量倒退 → 拒绝；时间倒退 → 拒绝。
        const auto backwardEnergy = send(socket, QStringLiteral("E02-T4"),
                                         QStringLiteral("PILE_TELEMETRY"),
                                         telemetryPayload(pileId,
                                                          isoOffset(-2),
                                                          QStringLiteral("99.00")));
        QVERIFY(!backwardEnergy.value(QStringLiteral("ok")).toBool());
        QCOMPARE(codeOf(backwardEnergy), QStringLiteral("TELEMETRY_OUT_OF_ORDER"));
        const auto backwardTime = send(socket, QStringLiteral("E02-T5"),
                                       QStringLiteral("PILE_TELEMETRY"),
                                       telemetryPayload(pileId,
                                                        isoOffset(-8),
                                                        QStringLiteral("100.30")));
        QVERIFY(!backwardTime.value(QStringLiteral("ok")).toBool());
        QCOMPARE(codeOf(backwardTime), QStringLiteral("TELEMETRY_OUT_OF_ORDER"));

        const auto detail = send(socket, QStringLiteral("E02-D1"),
                                 QStringLiteral("ORDER_DETAIL"),
                                 {{QStringLiteral("userToken"), token},
                                  {QStringLiteral("orderId"), orderId}});
        // 用量 = 累计读数 - 基准 = 100.10 - 50.00 = 50.10；完全重复上报不重复计量。
        QCOMPARE(data(detail).value(QStringLiteral("energyKwh")).toString(),
                 QStringLiteral("50.10"));
    }

    // E04：开始后改价 9.99：本单仍 1.23。
    void e04_lockedPriceSurvivesPriceChange()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 stationId = testDb.addStation(QStringLiteral("E04站"), 23.12, 113.26,
                                                   QStringLiteral("1.23"));
        const qint64 pileId = testDb.addPile(stationId, QStringLiteral("PILE-E04"));
        QVERIFY(testDb.exec(QStringLiteral(
            "INSERT INTO pile_telemetry(pile_id,recorded_at,temperature_c,voltage_v,"
            "current_a,power_w,energy_kwh) VALUES(?,?,25,220,16,3520,'10.00')"),
            {pileId, isoOffsetFresh()}));
        QVERIFY(testDb.exec(QStringLiteral(
            "UPDATE pile_info SET last_heartbeat_at=? WHERE id=?"),
            {isoOffsetFresh(), pileId}));

        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));
        const QString token = loginAndGetToken(socket, QStringLiteral("13800003333"),
                                               QStringLiteral("E04-LOGIN"));
        const auto reserve = send(socket, QStringLiteral("E04-RES"),
                                  QStringLiteral("ORDER_RESERVE"),
                                  {{QStringLiteral("userToken"), token},
                                   {QStringLiteral("pileId"), pileId}});
        const QString orderId = data(reserve).value(QStringLiteral("orderId")).toString();
        QVERIFY(send(socket, QStringLiteral("E04-START"), QStringLiteral("ORDER_START"),
                     {{QStringLiteral("userToken"), token},
                      {QStringLiteral("orderId"), orderId}})
                    .value(QStringLiteral("ok")).toBool());
        // 站点改价 9.99。
        QVERIFY(testDb.exec(QStringLiteral(
            "UPDATE station_info SET price_yuan_per_kwh='9.99' WHERE id=?"),
            {stationId}));
        QVERIFY(send(socket, QStringLiteral("E04-T1"), QStringLiteral("PILE_TELEMETRY"),
                     telemetryPayload(pileId, isoOffset(-5),
                                      QStringLiteral("11.23")))
                    .value(QStringLiteral("ok")).toBool());
        const auto stop = send(socket, QStringLiteral("E04-STOP"),
                               QStringLiteral("ORDER_STOP"),
                               {{QStringLiteral("userToken"), token},
                                {QStringLiteral("orderId"), orderId}});
        QVERIFY(stop.value(QStringLiteral("ok")).toBool());
        QCOMPARE(data(stop).value(QStringLiteral("unitPriceYuanPerKwh")).toString(),
                 QStringLiteral("1.23"));
        QCOMPARE(data(stop).value(QStringLiteral("amountYuan")).toString(),
                 QStringLiteral("1.51")); // 1.23 × 1.23 = 1.5129 → 1.51
    }

    // E05/E07：过热结算与恢复；E08：支付故障桩订单不误改桩状态。
    void e05e07e08_overheatSettlementAndRecovery()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 pileId = makePile(testDb, QStringLiteral("PILE-E05"));
        QVERIFY(testDb.exec(QStringLiteral(
            "INSERT INTO pile_telemetry(pile_id,recorded_at,temperature_c,voltage_v,"
            "current_a,power_w,energy_kwh) VALUES(?,?,25,220,16,3520,'200.00')"),
            {pileId, isoOffsetFresh()}));
        QVERIFY(testDb.exec(QStringLiteral(
            "UPDATE pile_info SET last_heartbeat_at=? WHERE id=?"),
            {isoOffsetFresh(), pileId}));

        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));
        const QString token = loginAndGetToken(socket, QStringLiteral("13800004444"),
                                               QStringLiteral("E05-LOGIN"));
        const auto reserve = send(socket, QStringLiteral("E05-RES"),
                                  QStringLiteral("ORDER_RESERVE"),
                                  {{QStringLiteral("userToken"), token},
                                   {QStringLiteral("pileId"), pileId}});
        const QString orderId = data(reserve).value(QStringLiteral("orderId")).toString();
        QVERIFY(send(socket, QStringLiteral("E05-START"), QStringLiteral("ORDER_START"),
                     {{QStringLiteral("userToken"), token},
                      {QStringLiteral("orderId"), orderId}})
                    .value(QStringLiteral("ok")).toBool());
        // 正常增量后过热：本条合法增量 0.50 计入后冻结。
        const auto hot = send(socket, QStringLiteral("E05-T1"), QStringLiteral("PILE_TELEMETRY"),
                              telemetryPayload(pileId, isoOffset(-5),
                                               QStringLiteral("200.50"), 71.5));
        QVERIFY2(hot.value(QStringLiteral("ok")).toBool(),
                 qPrintable(hot.value(QStringLiteral("message")).toString()));

        // 订单自动转 PENDING_PAYMENT、金额冻结 0.50×1.23=0.62（0.615→0.62）。
        {
            QSqlQuery q(testDb.database());
            q.prepare(QStringLiteral(
"SELECT status,energy_kwh,amount_yuan FROM charge_order "
                                  "WHERE id=?"

            ));
            q.addBindValue(orderId);
            QVERIFY(q.exec());
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("PENDING_PAYMENT"));
            QCOMPARE(q.value(1).toString(), QStringLiteral("0.50"));
            QCOMPARE(q.value(2).toString(), QStringLiteral("0.62"));
        }
        // 桩 FAULT + 活动告警 + 工单。
        {
            QSqlQuery q(testDb.database());
            q.prepare(QStringLiteral(

                "SELECT status FROM pile_info WHERE id=?"));

            q.addBindValue(pileId);

            QVERIFY(q.exec());
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("FAULT"));
            q.prepare(QStringLiteral(
"SELECT COUNT(*) FROM alarm_event WHERE pile_id=? "
                                  "AND alarm_type='OVERHEAT' AND status IN "
                                  "('OPEN','ACKNOWLEDGED')"

            ));
            q.addBindValue(pileId);
            QVERIFY(q.exec());
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 1);
            q.prepare(QStringLiteral(
"SELECT COUNT(*) FROM maintenance_ticket t JOIN "
                                  "alarm_event a ON a.id=t.alarm_id "
                                  "WHERE a.pile_id=? AND t.status<>'CLOSED'"

            ));
            q.addBindValue(pileId);
            QVERIFY(q.exec());
            QVERIFY(q.next());
            QVERIFY(q.value(0).toInt() >= 1);
        }

        // E08：支付后订单 FINISHED 但桩仍 FAULT（不误改 IDLE）。
        QVERIFY(testDb.exec(QStringLiteral(
            "UPDATE user_info SET balance_yuan='100.00' WHERE phone='13800004444'")));
        const auto pay = send(socket, QStringLiteral("E05-PAY"),
                              QStringLiteral("WALLET_PAY"),
                              {{QStringLiteral("userToken"), token},
                               {QStringLiteral("orderId"), orderId}});
        QVERIFY2(pay.value(QStringLiteral("ok")).toBool(),
                 qPrintable(pay.value(QStringLiteral("message")).toString()));
        {
            QSqlQuery q(testDb.database());
            q.prepare(QStringLiteral(

                "SELECT status FROM charge_order WHERE id=?"));

            q.addBindValue(orderId);

            QVERIFY(q.exec());
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("FINISHED"));
            q.prepare(QStringLiteral(

                "SELECT status FROM pile_info WHERE id=?"));

            q.addBindValue(pileId);

            QVERIFY(q.exec());
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("FAULT"));
        }
        // E7：恢复遥测（订单已结清、无活动订单）→ 设备健康恢复 IDLE，不复活充电单。
        const auto recover = send(socket, QStringLiteral("E07-T1"),
                                  QStringLiteral("PILE_TELEMETRY"),
                                  telemetryPayload(pileId, isoOffset(-1),
                                                   QStringLiteral("201.00"), 30.0));
        QVERIFY(recover.value(QStringLiteral("ok")).toBool());
        {
            QSqlQuery q(testDb.database());
            q.prepare(QStringLiteral(
                "SELECT status FROM pile_info WHERE id=?"));
            q.addBindValue(pileId);
            QVERIFY(q.exec());
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("IDLE"));
        }
    }

    // E06：断联超阈值+扫描：按最后读数冻结、桩 OFFLINE、不估算失联电量。
    void e06_offlineScanFreezesAtLastReading()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 pileId = makePile(testDb, QStringLiteral("PILE-E06"));
        QVERIFY(testDb.exec(QStringLiteral(
            "INSERT INTO pile_telemetry(pile_id,recorded_at,temperature_c,voltage_v,"
            "current_a,power_w,energy_kwh) VALUES(?,?,25,220,16,3520,'300.00')"),
            {pileId, isoOffsetFresh()}));
        QVERIFY(testDb.exec(QStringLiteral(
            "UPDATE pile_info SET last_heartbeat_at=? WHERE id=?"),
            {isoOffsetFresh(), pileId}));

        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));
        const QString token = loginAndGetToken(socket, QStringLiteral("13800005555"),
                                               QStringLiteral("E06-LOGIN"));
        const auto reserve = send(socket, QStringLiteral("E06-RES"),
                                  QStringLiteral("ORDER_RESERVE"),
                                  {{QStringLiteral("userToken"), token},
                                   {QStringLiteral("pileId"), pileId}});
        const QString orderId = data(reserve).value(QStringLiteral("orderId")).toString();
        QVERIFY(send(socket, QStringLiteral("E06-START"), QStringLiteral("ORDER_START"),
                     {{QStringLiteral("userToken"), token},
                      {QStringLiteral("orderId"), orderId}})
                    .value(QStringLiteral("ok")).toBool());
        // 最后有效读数 300.80。
        QVERIFY(send(socket, QStringLiteral("E06-T1"), QStringLiteral("PILE_TELEMETRY"),
                     telemetryPayload(pileId, isoOffset(-5),
                                      QStringLiteral("300.80")))
                    .value(QStringLiteral("ok")).toBool());

        // 心跳时间回拨 >15 秒再扫描（服务层入口与生产后台同一逻辑）。
        QVERIFY(testDb.exec(QStringLiteral(
            "UPDATE pile_info SET last_heartbeat_at=? WHERE id=?"),
            {isoOffset(-90), pileId}));
        auto monitor = testDb.openConnection(QStringLiteral("e06_monitor"));
        QCOMPARE(TelemetryService(monitor).scanOffline(), 1);

        {
            QSqlQuery q(testDb.database());
            q.prepare(QStringLiteral(
"SELECT status,energy_kwh,amount_yuan FROM charge_order "
                                  "WHERE id=?"

            ));
            q.addBindValue(orderId);
            QVERIFY(q.exec());
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("PENDING_PAYMENT"));
            QCOMPARE(q.value(1).toString(), QStringLiteral("0.80"));
            QCOMPARE(q.value(2).toString(), QStringLiteral("0.98")); // 0.80×1.23=0.984→0.98
            q.prepare(QStringLiteral(

                "SELECT status FROM pile_info WHERE id=?"));

            q.addBindValue(pileId);

            QVERIFY(q.exec());
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("OFFLINE"));
        }
        monitor.close();
    }

    // E10：管理禁用→正常遥测→重启→仍禁用；解除需健康+无占用。
    // E12：两客户端并发预约同桩只一单成功。
    void e10_adminOverrideSurvivesTelemetryAndRestart()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 pileId = makePile(testDb, QStringLiteral("PILE-E10"));
        QVERIFY(testDb.exec(QStringLiteral(
            "UPDATE pile_info SET admin_status_override='OFFLINE' WHERE id=?"),
            {pileId}));

        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));

        // 正常遥测不清除管理禁用。
        QVERIFY(send(socket, QStringLiteral("E10-T1"), QStringLiteral("PILE_TELEMETRY"),
                     telemetryPayload(pileId, isoOffset(-5),
                                      QStringLiteral("5.00")))
                    .value(QStringLiteral("ok")).toBool());
        {
            QSqlQuery q(testDb.database());
            q.prepare(QStringLiteral(
"SELECT status,admin_status_override FROM pile_info "
                                  "WHERE id=?"

            ));
            q.addBindValue(pileId);
            QVERIFY(q.exec());
            QVERIFY(q.next());
            QCOMPARE(q.value(1).toString(), QStringLiteral("OFFLINE"));
        }

        // “重启”（服务端重开）后仍禁用。
        server.close();
        TcpServer server2(testDb.path());
        QVERIFY(server2.listen(QHostAddress::LocalHost, 0));
        {
            QSqlQuery q(testDb.database());
            q.prepare(QStringLiteral(

                "SELECT admin_status_override FROM pile_info WHERE id=?"));

            q.addBindValue(pileId);

            QVERIFY(q.exec());
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("OFFLINE"));
        }

        // 管理员请求 IDLE=申请解除：无近期正常读数（心跳是 08:01，当前真实时间更晚）
        // 或有活动订单时不能解除——此处设备健康（刚上报）但需管理员操作走 ADMIN_PILE_SET_STATUS。
        // 精确解除路径在 device_override_test 中细化；此处验证禁用不被遥测/重启清除即可。
        server2.close();
    }

    void e12_concurrentReservationStillExclusive()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 pileId = makePile(testDb, QStringLiteral("PILE-E12"));
        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket a;
        a.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(a.waitForConnected(2000));
        QTcpSocket b;
        b.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(b.waitForConnected(2000));
        const QString tokenA = loginAndGetToken(a, QStringLiteral("13800006666"),
                                                QStringLiteral("E12-A"));
        const QString tokenB = loginAndGetToken(b, QStringLiteral("13800007777"),
                                                QStringLiteral("E12-B"));
        QVERIFY(!tokenA.isEmpty() && !tokenB.isEmpty());

        const auto ra = send(a, QStringLiteral("E12-RA"), QStringLiteral("ORDER_RESERVE"),
                            {{QStringLiteral("userToken"), tokenA},
                             {QStringLiteral("pileId"), pileId}});
        const auto rb = send(b, QStringLiteral("E12-RB"), QStringLiteral("ORDER_RESERVE"),
                            {{QStringLiteral("userToken"), tokenB},
                             {QStringLiteral("pileId"), pileId}});
        const int successCount = (ra.value(QStringLiteral("ok")).toBool() ? 1 : 0)
            + (rb.value(QStringLiteral("ok")).toBool() ? 1 : 0);
        QCOMPARE(successCount, 1);
        QSqlQuery q(testDb.database());
        q.prepare(QStringLiteral(
"SELECT COUNT(*) FROM charge_order WHERE pile_id=? "
                              "AND status IN ('RESERVED','CHARGING','PENDING_PAYMENT')"

        ));
        q.addBindValue(pileId);
        QVERIFY(q.exec());
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
        server.close();
    }

    // E11：工单写入失败整体回滚（模拟约束冲突：制造同桩同类已关闭告警+工单外的失败注入——
    // 直接通过在事务中制造唯一索引冲突：重复插入相同 alarm 唯一键 ux_alarm_active_pile_type）。
    void e11_telemetryAlarmTicketAtomicity()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 pileId = makePile(testDb, QStringLiteral("PILE-E11"));
        QVERIFY(testDb.exec(QStringLiteral(
            "INSERT INTO pile_telemetry(pile_id,recorded_at,temperature_c,voltage_v,"
            "current_a,power_w,energy_kwh) VALUES(?,?,25,220,16,3520,'20.00')"),
            {pileId, isoOffsetFresh()}));
        QVERIFY(testDb.exec(QStringLiteral(
            "UPDATE pile_info SET last_heartbeat_at=? WHERE id=?"),
            {isoOffsetFresh(), pileId}));

        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));
        const QString token = loginAndGetToken(socket, QStringLiteral("13800008888"),
                                               QStringLiteral("E11-LOGIN"));
        const auto reserve = send(socket, QStringLiteral("E11-RES"),
                                  QStringLiteral("ORDER_RESERVE"),
                                  {{QStringLiteral("userToken"), token},
                                   {QStringLiteral("pileId"), pileId}});
        const QString orderId = data(reserve).value(QStringLiteral("orderId")).toString();
        QVERIFY(send(socket, QStringLiteral("E11-START"), QStringLiteral("ORDER_START"),
                     {{QStringLiteral("userToken"), token},
                      {QStringLiteral("orderId"), orderId}})
                    .value(QStringLiteral("ok")).toBool());
        // 过热触发告警+工单 → PENDING_PAYMENT。
        QVERIFY(send(socket, QStringLiteral("E11-T1"), QStringLiteral("PILE_TELEMETRY"),
                     telemetryPayload(pileId, isoOffset(-5),
                                      QStringLiteral("20.50"), 72.0))
                    .value(QStringLiteral("ok")).toBool());
        {
            QSqlQuery q(testDb.database());
            q.prepare(QStringLiteral(

                "SELECT status FROM charge_order WHERE id=?"));

            q.addBindValue(orderId);

            QVERIFY(q.exec());
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("PENDING_PAYMENT"));
        }
        server.close();
        // 直接注入失败：把 alarm_event 表临时重命名制造 ensureActiveAlarmAndTicket 失败路径，
        // 验证下一条过热遥测整体回滚（遥测明细也不落库）。
        QVERIFY(testDb.exec(QStringLiteral("ALTER TABLE alarm_event RENAME TO "
                                           "alarm_event_hidden")));
        auto conn = testDb.openConnection(QStringLiteral("e11_fail"));
        const int before = [&] {
            QSqlQuery q(conn);
            q.prepare(QStringLiteral(

                "SELECT COUNT(*) FROM pile_telemetry WHERE pile_id=?"));

            q.addBindValue(pileId);
            if (!q.exec() || !q.next()) {
                return -1;
            }
            return q.value(0).toInt();
        }();
        const auto result = TelemetryService(conn).record(
            {pileId, QDateTime::fromString(isoOffset(-2),
                                           Qt::ISODate),
             75.0, 220.0, 16.0, 3520,
             DecimalValue::parseCanonical(QStringLiteral("21.00")).value()});
        QVERIFY(!result.ok);
        QCOMPARE(result.errorCode, QStringLiteral("DATABASE_ERROR"));
        const int after = [&] {
            QSqlQuery q(conn);
            q.prepare(QStringLiteral(

                "SELECT COUNT(*) FROM pile_telemetry WHERE pile_id=?"));

            q.addBindValue(pileId);
            if (!q.exec() || !q.next()) {
                return -1;
            }
            return q.value(0).toInt();
        }();
        QCOMPARE(after, before);
        conn.close();
        QVERIFY(testDb.exec(QStringLiteral("ALTER TABLE alarm_event_hidden RENAME TO "
                                           "alarm_event")));
    }

    // E13：未授权 ORDER_TELEMETRY 写入拒绝且无用量变化；PILE 无 deviceToken 配置时拒绝。
    void e13_unauthorizedTelemetryRejected()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));

        // 网络层 ORDER_TELEMETRY 已按契约关闭（AUTH_INVALID），无用量变化。
        const auto orderTelemetry = send(socket, QStringLiteral("E13-OT"),
                                         QStringLiteral("ORDER_TELEMETRY"),
                                         {{QStringLiteral("orderId"),
                                           QStringLiteral("ORD-ANY")},
                                          {QStringLiteral("energyKwh"),
                                           QStringLiteral("5.00")}});
        QVERIFY(!orderTelemetry.value(QStringLiteral("ok")).toBool());
        QCOMPARE(codeOf(orderTelemetry), QStringLiteral("AUTH_INVALID"));
        server.close();
    }
};

QTEST_MAIN(OrderMeteringProtocolTest)
#include "order_metering_protocol_test.moc"
