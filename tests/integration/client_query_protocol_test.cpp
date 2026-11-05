// 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
// 客户端查询协议测试：验证 Qt 端可调用的资料/昵称更新/余额/站点/活动订单/详情/历史 TCP 动作。

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QtTest>

#include "server/network/tcp_server.h"
#include "shared/protocol/frame_codec.h"
#include "tests/helpers/test_database.h"

class ClientQueryProtocolTest final : public QObject
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

private slots:
    void exposesUserStationAndOrderQueriesForQtClient()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800001234"),
                                             QStringLiteral("88.66"));
        const qint64 stationId = testDb.addStation(QStringLiteral("联动测试站"),
                                                   23.12, 113.26,
                                                   QStringLiteral("1.23"));
        const qint64 pileId = testDb.addPile(stationId,
                                             QStringLiteral("PILE-QUERY-001"),
                                             QStringLiteral("RESERVED"));
        QVERIFY(testDb.exec(QStringLiteral(
            "INSERT INTO charge_order(id,user_id,pile_id,status,energy_kwh,amount_yuan,"
            "unit_price_yuan_per_kwh,reserved_until,created_at) "
            "VALUES('ORD-ACTIVE',?,?, 'RESERVED','0.00','0.00','1.23',"
            "'2026-09-09T09:30:00Z','2026-09-09T09:00:00Z')"),
            {userId, pileId}));
        QVERIFY(testDb.exec(QStringLiteral(
            "INSERT INTO charge_order(id,user_id,pile_id,status,energy_kwh,amount_yuan,"
            "unit_price_yuan_per_kwh,started_at,ended_at,created_at) "
            "VALUES('ORD-FINISHED',?,?,'FINISHED','8.35','10.27','1.23',"
            "'2026-09-08T08:00:00Z','2026-09-08T09:00:00Z','2026-09-08T08:00:00Z')"),
            {userId, pileId}));

        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));

        // M1-02 夹具更新：登录拿 userToken，后续用户动作都携带令牌（保留原断言）。
        const auto login = send(socket, QStringLiteral("CQ-0"),
                                QStringLiteral("USER_LOGIN"),
                                {{QStringLiteral("phone"),
                                  QStringLiteral("13800001234")}});
        QVERIFY2(login.value(QStringLiteral("ok")).toBool(),
                 qPrintable(login.value(QStringLiteral("message")).toString()));
        const QString userToken =
            data(login).value(QStringLiteral("userToken")).toString();
        QVERIFY(!userToken.isEmpty());

        const auto profile = send(socket, QStringLiteral("CQ-1"),
                                  QStringLiteral("USER_PROFILE"),
                                  {{QStringLiteral("userToken"), userToken},
                                  {QStringLiteral("userId"), userId}});
        QVERIFY(profile.value(QStringLiteral("ok")).toBool());
        QCOMPARE(data(profile).value(QStringLiteral("phoneMasked")).toString(),
                 QStringLiteral("138****1234"));
        QCOMPARE(data(profile).value(QStringLiteral("balanceYuan")).toString(),
                 QStringLiteral("88.66"));

        const auto profileUpdate = send(
            socket, QStringLiteral("CQ-1-UPDATE"), QStringLiteral("USER_PROFILE_UPDATE"),
            {{QStringLiteral("userToken"), userToken},
             {QStringLiteral("userId"), userId},
             {QStringLiteral("nickname"), QStringLiteral("联动车主")},
             {QStringLiteral("avatarPath"), QString{}}});
        QVERIFY(profileUpdate.value(QStringLiteral("ok")).toBool());
        QCOMPARE(data(profileUpdate).value(QStringLiteral("nickname")).toString(),
                 QStringLiteral("联动车主"));
        const auto updatedProfile = send(socket, QStringLiteral("CQ-1-UPDATED"),
                                         QStringLiteral("USER_PROFILE"),
                                         {{QStringLiteral("userToken"), userToken},
                                  {QStringLiteral("userId"), userId}});
        QCOMPARE(data(updatedProfile).value(QStringLiteral("nickname")).toString(),
                 QStringLiteral("联动车主"));

        const auto balance = send(socket, QStringLiteral("CQ-2"),
                                  QStringLiteral("WALLET_BALANCE"),
                                  {{QStringLiteral("userToken"), userToken},
                                  {QStringLiteral("userId"), userId}});
        QVERIFY(balance.value(QStringLiteral("ok")).toBool());
        QCOMPARE(data(balance).value(QStringLiteral("balanceYuan")).toString(),
                 QStringLiteral("88.66"));

        const auto station = send(socket, QStringLiteral("CQ-3"),
                                  QStringLiteral("STATION_DETAIL"),
                                  {{QStringLiteral("stationId"), stationId}});
        QVERIFY(station.value(QStringLiteral("ok")).toBool());
        QCOMPARE(data(station).value(QStringLiteral("priceYuanPerKwh")).toString(),
                 QStringLiteral("1.23"));
        const auto piles = data(station).value(QStringLiteral("piles")).toArray();
        QCOMPARE(piles.size(), 1);
        QCOMPARE(piles.first().toObject().value(QStringLiteral("pileId")).toInteger(),
                 pileId);

        const auto active = send(socket, QStringLiteral("CQ-4"),
                                 QStringLiteral("ORDER_ACTIVE"),
                                 {{QStringLiteral("userToken"), userToken},
                                  {QStringLiteral("userId"), userId}});
        QVERIFY(active.value(QStringLiteral("ok")).toBool());
        QCOMPARE(data(active).value(QStringLiteral("order")).toObject()
                     .value(QStringLiteral("orderId")).toString(),
                 QStringLiteral("ORD-ACTIVE"));

        const auto detail = send(socket, QStringLiteral("CQ-5"),
                                 QStringLiteral("ORDER_DETAIL"),
                                 {{QStringLiteral("userToken"), userToken},
                                  {QStringLiteral("orderId"), QStringLiteral("ORD-FINISHED")}});
        QVERIFY(detail.value(QStringLiteral("ok")).toBool());
        QCOMPARE(data(detail).value(QStringLiteral("amountYuan")).toString(),
                 QStringLiteral("10.27"));
        QCOMPARE(data(detail).value(QStringLiteral("energyKwh")).toString(),
                 QStringLiteral("8.35"));

        const auto history = send(socket, QStringLiteral("CQ-6"),
                                  QStringLiteral("ORDER_HISTORY"),
                                  {{QStringLiteral("userToken"), userToken},
                                   {QStringLiteral("userId"), userId},
                                   {QStringLiteral("limit"), 20},
                                   {QStringLiteral("offset"), 0}});
        QVERIFY(history.value(QStringLiteral("ok")).toBool());
        const auto rows = data(history).value(QStringLiteral("rows")).toArray();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toObject().value(QStringLiteral("orderId")).toString(),
                 QStringLiteral("ORD-FINISHED"));

        const auto missing = send(socket, QStringLiteral("CQ-7"),
                                  QStringLiteral("ORDER_DETAIL"),
                                  {{QStringLiteral("userToken"), userToken},
                                   {QStringLiteral("orderId"), QStringLiteral("MISSING")}});
        QVERIFY(!missing.value(QStringLiteral("ok")).toBool());
        QCOMPARE(missing.value(QStringLiteral("code")).toString(),
                 QStringLiteral("NOT_FOUND"));
    }
};

QTEST_GUILESS_MAIN(ClientQueryProtocolTest)
#include "client_query_protocol_test.moc"
