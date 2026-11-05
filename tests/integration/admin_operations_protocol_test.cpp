// 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
// 管理操作协议测试：验证管理列表、站点修改、坐标缺失拒绝、桩状态设置、模拟重启（按订单
// 恢复正确状态）及审计日志。

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QtTest>

#include "server/network/tcp_server.h"
#include "server/services/admin_service.h"
#include "shared/protocol/frame_codec.h"
#include "tests/helpers/test_database.h"

class AdminOperationsProtocolTest final : public QObject
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

    static bool ok(const QJsonObject &response)
    {
        return response.value(QStringLiteral("ok")).toBool();
    }

private slots:
    void supportsAuthenticatedOperationsAndWritesAuditLogs()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        QVERIFY(AdminService(testDb.database()).ensureDefaultAdmin().ok);
        const qint64 stationId = testDb.addStation(QStringLiteral("修改前站点"));
        const qint64 pileId = testDb.addPile(stationId, QStringLiteral("PILE-ADMIN-001"));
        QVERIFY(testDb.exec(QStringLiteral(
            "INSERT INTO alarm_event(pile_id,severity,alarm_type,content,status,occurred_at) "
            "VALUES(?,'WARNING','MANUAL_TEST','联动验收告警','OPEN',?)"),
            {pileId, QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}));
        const qint64 alarmId = testDb.scalar(
            QStringLiteral("SELECT last_insert_rowid()")).toLongLong();
        QVERIFY(testDb.exec(QStringLiteral(
            "INSERT INTO maintenance_ticket(alarm_id,status,created_at,updated_at) "
            "VALUES(?,'OPEN',?,?)"),
            {alarmId, QDateTime::currentDateTimeUtc().toString(Qt::ISODate),
             QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}));

        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));

        const auto login = send(socket, QStringLiteral("AO-LOGIN"),
                                QStringLiteral("ADMIN_LOGIN"),
                                {{QStringLiteral("username"), QStringLiteral("admin")},
                                 {QStringLiteral("password"), QStringLiteral("123456")}});
        QVERIFY(ok(login));
        const QString token = login.value(QStringLiteral("data")).toObject()
                                  .value(QStringLiteral("adminToken")).toString();
        QVERIFY(!token.isEmpty());

        const auto invalidStationUpdate = send(
            socket, QStringLiteral("AO-STATION-MISSING-COORD"),
            QStringLiteral("ADMIN_STATION_UPDATE"),
            {{QStringLiteral("adminToken"), token},
             {QStringLiteral("stationId"), stationId},
             {QStringLiteral("name"), QStringLiteral("不应写入")},
             {QStringLiteral("address"), QStringLiteral("无坐标")},
             {QStringLiteral("longitude"), 113.30},
             {QStringLiteral("priceYuanPerKwh"), QStringLiteral("1.47")},
             {QStringLiteral("status"), QStringLiteral("OPEN")}});
        QVERIFY(!ok(invalidStationUpdate));
        QCOMPARE(invalidStationUpdate.value(QStringLiteral("code")).toString(),
                 QStringLiteral("PARAM_INVALID"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT name FROM station_info WHERE id=?"),
                               {stationId}).toString(), QStringLiteral("修改前站点"));

        const auto denied = send(socket, QStringLiteral("AO-DENIED"),
                                 QStringLiteral("ADMIN_ALARMS"), {});
        QCOMPARE(denied.value(QStringLiteral("code")).toString(),
                 QStringLiteral("AUTH_INVALID"));

        const auto alarms = send(socket, QStringLiteral("AO-ALARMS"),
                                 QStringLiteral("ADMIN_ALARMS"),
                                 {{QStringLiteral("adminToken"), token},
                                  {QStringLiteral("status"), QStringLiteral("OPEN")},
                                  {QStringLiteral("limit"), 20}});
        QVERIFY(ok(alarms));
        QCOMPARE(alarms.value(QStringLiteral("data")).toObject()
                     .value(QStringLiteral("rows")).toArray().size(), 1);

        const auto tickets = send(socket, QStringLiteral("AO-TICKETS"),
                                  QStringLiteral("ADMIN_TICKETS"),
                                  {{QStringLiteral("adminToken"), token},
                                   {QStringLiteral("status"), QStringLiteral("OPEN")},
                                   {QStringLiteral("limit"), 20}});
        QVERIFY(ok(tickets));
        QCOMPARE(tickets.value(QStringLiteral("data")).toObject()
                     .value(QStringLiteral("rows")).toArray().size(), 1);

        const auto stationUpdate = send(socket, QStringLiteral("AO-STATION"),
                                        QStringLiteral("ADMIN_STATION_UPDATE"),
                                        {{QStringLiteral("adminToken"), token},
                                         {QStringLiteral("stationId"), stationId},
                                         {QStringLiteral("name"), QStringLiteral("修改后站点")},
                                         {QStringLiteral("address"), QStringLiteral("联动路9号")},
                                         {QStringLiteral("latitude"), 23.20},
                                         {QStringLiteral("longitude"), 113.30},
                                         {QStringLiteral("priceYuanPerKwh"), QStringLiteral("1.47")},
                                         {QStringLiteral("status"), QStringLiteral("OPEN")}});
        QVERIFY(ok(stationUpdate));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT price_yuan_per_kwh FROM station_info WHERE id=?"), {stationId}).toString(),
            QStringLiteral("1.47"));

        const auto stationCreate = send(socket, QStringLiteral("AO-STATION-CREATE"),
                                        QStringLiteral("ADMIN_STATION_CREATE"),
                                        {{QStringLiteral("adminToken"), token},
                                         {QStringLiteral("name"), QStringLiteral("新增联动站")},
                                         {QStringLiteral("address"), QStringLiteral("联动路10号")},
                                         {QStringLiteral("latitude"), 23.21},
                                         {QStringLiteral("longitude"), 113.31},
                                         {QStringLiteral("priceYuanPerKwh"), QStringLiteral("1.28")},
                                         {QStringLiteral("piles"), QJsonArray{
                                             QJsonObject{{QStringLiteral("serial"),
                                                          QStringLiteral("PILE-CREATED-001")},
                                                         {QStringLiteral("chargeType"),
                                                          QStringLiteral("FAST")},
                                                         {QStringLiteral("ratedPowerKw"), 60.0}}
                                         }}});
        QVERIFY(ok(stationCreate));

        const auto setFault = send(socket, QStringLiteral("AO-PILE-FAULT"),
                                   QStringLiteral("ADMIN_PILE_SET_STATUS"),
                                   {{QStringLiteral("adminToken"), token},
                                    {QStringLiteral("pileId"), pileId},
                                    {QStringLiteral("status"), QStringLiteral("FAULT")},
                                    {QStringLiteral("reason"), QStringLiteral("答辩模拟故障")}});
        QVERIFY(ok(setFault));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT status FROM pile_info WHERE id=?"), {pileId}).toString(),
            QStringLiteral("FAULT"));

        const auto restarted = send(socket, QStringLiteral("AO-PILE-RESTART"),
                                    QStringLiteral("ADMIN_PILE_RESTART"),
                                    {{QStringLiteral("adminToken"), token},
                                     {QStringLiteral("pileId"), pileId}});
        QVERIFY(ok(restarted));
        QVERIFY(!restarted.value(QStringLiteral("data")).toObject()
                     .value(QStringLiteral("commandId")).toString().isEmpty());
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT status FROM pile_info WHERE id=?"), {pileId}).toString(),
            QStringLiteral("IDLE"));

        const qint64 chargingUser = testDb.addUser(QStringLiteral("13800138301"));
        const qint64 chargingPile = testDb.addPile(
            stationId, QStringLiteral("PILE-ADMIN-CHARGING"), QStringLiteral("FAULT"));
        QVERIFY(testDb.exec(QStringLiteral(
            "INSERT INTO charge_order(id,user_id,pile_id,status,energy_kwh,amount_yuan,"
            "unit_price_yuan_per_kwh,created_at) "
            "VALUES('ORDER-ADMIN-CHARGING',?,?,'CHARGING','1.00','0.00','1.20',?)"),
            {chargingUser, chargingPile,
             QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}));
        const auto resumed = send(socket, QStringLiteral("AO-PILE-RESTART-CHARGING"),
                                  QStringLiteral("ADMIN_PILE_RESTART"),
                                  {{QStringLiteral("adminToken"), token},
                                   {QStringLiteral("pileId"), chargingPile}});
        QVERIFY(ok(resumed));
        QCOMPARE(resumed.value(QStringLiteral("data")).toObject()
                     .value(QStringLiteral("status")).toString(),
                 QStringLiteral("CHARGING"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT status FROM pile_info WHERE id=?"), {chargingPile}).toString(),
            QStringLiteral("CHARGING"));

        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM operation_log WHERE action IN "
            "('CREATE_STATION','UPDATE_STATION','SET_PILE_STATUS','RESTART_PILE')")).toInt(), 5);
    }
};

QTEST_GUILESS_MAIN(AdminOperationsProtocolTest)
#include "admin_operations_protocol_test.moc"
