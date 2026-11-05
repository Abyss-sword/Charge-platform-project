// 测试文件头注释（成员1 · 第一阶段收尾 · M1-02）：
// 用户会话、归属与请求状态协议测试（U01~U09，01文档 4.1/4.5）。
// 失败测试先行：当前 USER_LOGIN 不返回 userToken、用户动作不带授权，
// 本文件在实现前必然编译失败或断言失败。

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QtTest>

#include "server/network/tcp_server.h"
#include "server/services/admin_service.h"
#include "shared/protocol/frame_codec.h"
#include "tests/helpers/test_database.h"

class UserAuthorizationProtocolTest final : public QObject
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

private slots:
    // U01：登录返回 userToken + expiresAt；U02：无/乱填 token 一律 AUTH_INVALID。
    void u01u02_loginIssuesTokenAndRejectsInvalid()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userIdA = testDb.addUser(QStringLiteral("13800001234"),
                                              QStringLiteral("88.66"));
        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));

        const auto login = send(socket, QStringLiteral("U01-1"),
                                 QStringLiteral("USER_LOGIN"),
                                 {{QStringLiteral("phone"), QStringLiteral("13800001234")}});
        QVERIFY(login.value(QStringLiteral("ok")).toBool());
        QCOMPARE(data(login).value(QStringLiteral("userId")).toInteger(), userIdA);
        const QString token = data(login).value(QStringLiteral("userToken")).toString();
        QVERIFY2(!token.isEmpty(), "USER_LOGIN must return non-empty userToken");
        QVERIFY(!data(login).value(QStringLiteral("expiresAt")).toString().isEmpty());

        // 带合法 token 查询资料成功。
        const auto profile = send(socket, QStringLiteral("U01-2"),
                                  QStringLiteral("USER_PROFILE"),
                                  {{QStringLiteral("userToken"), token}});
        QVERIFY(profile.value(QStringLiteral("ok")).toBool());
        QCOMPARE(data(profile).value(QStringLiteral("userId")).toInteger(), userIdA);

        // U02：不带 token → AUTH_INVALID，无数据。
        const auto noToken = send(socket, QStringLiteral("U02-1"),
                                  QStringLiteral("USER_PROFILE"), {});
        QVERIFY(!noToken.value(QStringLiteral("ok")).toBool());
        QCOMPARE(codeOf(noToken), QStringLiteral("AUTH_INVALID"));

        // 乱填 token → AUTH_INVALID。
        const auto badToken = send(socket, QStringLiteral("U02-2"),
                                   QStringLiteral("USER_PROFILE"),
                                   {{QStringLiteral("userToken"),
                                     QStringLiteral("garbage-token")}});
        QVERIFY(!badToken.value(QStringLiteral("ok")).toBool());
        QCOMPARE(codeOf(badToken), QStringLiteral("AUTH_INVALID"));
    }

    // U03：A 的 token 携带 B 的 userId → 拒绝，B 数据不变。
    void u03_tokenCannotActAsOtherUser()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userIdA = testDb.addUser(QStringLiteral("13800001234"),
                                              QStringLiteral("10.00"));
        const qint64 userIdB = testDb.addUser(QStringLiteral("13900005678"),
                                              QStringLiteral("77.77"));
        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));

        const auto loginA = send(socket, QStringLiteral("U03-1"),
                                  QStringLiteral("USER_LOGIN"),
                                  {{QStringLiteral("phone"), QStringLiteral("13800001234")}});
        const QString tokenA = data(loginA).value(QStringLiteral("userToken")).toString();
        QVERIFY(!tokenA.isEmpty());

        // A 的 token + B 的 userId → 拒绝（AUTH_INVALID），不返回 B 的资料。
        const auto asB = send(socket, QStringLiteral("U03-2"),
                               QStringLiteral("USER_PROFILE"),
                               {{QStringLiteral("userToken"), tokenA},
                                {QStringLiteral("userId"), userIdB}});
        QVERIFY(!asB.value(QStringLiteral("ok")).toBool());
        QCOMPARE(codeOf(asB), QStringLiteral("AUTH_INVALID"));

        // B 的资料仍可由 B 自己查到（数据未变）。
        const auto loginB = send(socket, QStringLiteral("U03-3"),
                                 QStringLiteral("USER_LOGIN"),
                                 {{QStringLiteral("phone"), QStringLiteral("13900005678")}});
        const QString tokenB = data(loginB).value(QStringLiteral("userToken")).toString();
        const auto profileB = send(socket, QStringLiteral("U03-4"),
                                   QStringLiteral("USER_PROFILE"),
                                   {{QStringLiteral("userToken"), tokenB}});
        QVERIFY(profileB.value(QStringLiteral("ok")).toBool());
        QCOMPARE(data(profileB).value(QStringLiteral("userId")).toInteger(), userIdB);
        QCOMPARE(data(profileB).value(QStringLiteral("balanceYuan")).toString(),
                 QStringLiteral("77.77"));
    }

    // U04：A 的 token 读/停/付 B 的订单 → 拒绝，不泄露详情。
    void u04_orderOwnershipEnforced()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userIdA = testDb.addUser(QStringLiteral("13800001234"),
                                              QStringLiteral("50.00"));
        const qint64 userIdB = testDb.addUser(QStringLiteral("13900005678"),
                                              QStringLiteral("50.00"));
        const qint64 stationId = testDb.addStation(QStringLiteral("U04站"), 23.12, 113.26,
                                                   QStringLiteral("1.23"));
        const qint64 pileId = testDb.addPile(stationId, QStringLiteral("PILE-U04-1"),
                                             QStringLiteral("CHARGING"));
        QVERIFY2(testDb.exec(QStringLiteral(
            "INSERT INTO charge_order(id,user_id,pile_id,status,energy_kwh,amount_yuan,"
            "unit_price_yuan_per_kwh,started_at,created_at) "
            "VALUES('ORD-U04-B',?,?,'CHARGING','3.00','0.00','1.23',"
            "'2026-09-10T08:00:00Z','2026-09-10T08:00:00Z')"),
            {userIdB, pileId}), qPrintable(testDb.error()));

        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));

        const auto loginA = send(socket, QStringLiteral("U04-1"),
                                 QStringLiteral("USER_LOGIN"),
                                 {{QStringLiteral("phone"), QStringLiteral("13800001234")}});
        const QString tokenA = data(loginA).value(QStringLiteral("userToken")).toString();
        QVERIFY(!tokenA.isEmpty());

        for (const QString &action : {QStringLiteral("ORDER_DETAIL"),
                                      QStringLiteral("ORDER_STOP"),
                                      QStringLiteral("WALLET_PAY")}) {
            QJsonObject payload{{QStringLiteral("userToken"), tokenA},
                                {QStringLiteral("orderId"), QStringLiteral("ORD-U04-B")}};
            const auto resp = send(socket, QStringLiteral("U04-X-") + action, action,
                                   payload);
            QVERIFY2(!resp.value(QStringLiteral("ok")).toBool(),
                     qPrintable(action + QStringLiteral(" must reject")));
            QCOMPARE(codeOf(resp), QStringLiteral("AUTH_INVALID"));
            // 不泄露 B 订单内容：data 为空或无 status 字段。
            QVERIFY(data(resp).value(QStringLiteral("status")).toString().isEmpty());
        }

        // B 的订单状态未被改动。
        QSqlQuery check(testDb.database());
        check.exec(QStringLiteral("SELECT status FROM charge_order "
                                  "WHERE id='ORD-U04-B'"));
        QVERIFY(check.next());
        QCOMPARE(check.value(0).toString(), QStringLiteral("CHARGING"));
    }

    // U05：userToken 与 adminToken 不可跨角色使用。
    void u05_tokensCannotCrossRoles()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        // 迁移只建表不建管理员；正式入口 main() 会 ensureDefaultAdmin，此处同样调用。
        {
            QSqlDatabase bootstrap = testDb.openConnection(QStringLiteral("u05_admin"));
            QVERIFY(AdminService(bootstrap).ensureDefaultAdmin().ok);
            bootstrap.close();
        }
        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));

        const auto login = send(socket, QStringLiteral("U05-1"),
                                 QStringLiteral("USER_LOGIN"),
                                 {{QStringLiteral("phone"), QStringLiteral("13800001234")}});
        const QString userToken = data(login).value(QStringLiteral("userToken")).toString();
        QVERIFY(!userToken.isEmpty());

        // userToken 访问管理动作 → AUTH_INVALID。
        const auto adminWithUserToken = send(socket, QStringLiteral("U05-2"),
                                             QStringLiteral("ADMIN_USERS"),
                                             {{QStringLiteral("adminToken"), userToken}});
        QVERIFY(!adminWithUserToken.value(QStringLiteral("ok")).toBool());
        QCOMPARE(codeOf(adminWithUserToken), QStringLiteral("AUTH_INVALID"));

        // adminToken 替代 userToken 访问用户资料 → AUTH_INVALID。
        const auto adminLogin = send(socket, QStringLiteral("U05-3"),
                                     QStringLiteral("ADMIN_LOGIN"),
                                     {{QStringLiteral("username"),
                                       QStringLiteral("admin")},
                                      {QStringLiteral("password"),
                                       QStringLiteral("123456")}});
        const QString adminToken =
            data(adminLogin).value(QStringLiteral("adminToken")).toString();
        QVERIFY(!adminToken.isEmpty());
        const auto userWithAdminToken = send(socket, QStringLiteral("U05-4"),
                                             QStringLiteral("USER_PROFILE"),
                                             {{QStringLiteral("userToken"), adminToken}});
        QVERIFY(!userWithAdminToken.value(QStringLiteral("ok")).toBool());
        QCOMPARE(codeOf(userWithAdminToken), QStringLiteral("AUTH_INVALID"));
    }

    // U06：冻结用户可登录/查询/停止/结清既有订单，但新预约被拒。
    void u06_frozenUserRestrictions()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800001234"),
                                              QStringLiteral("20.00"));
        QVERIFY(testDb.exec(QStringLiteral(
            "UPDATE user_info SET status='FROZEN' WHERE id=%1").arg(userId)));
        const qint64 stationId = testDb.addStation(QStringLiteral("U06站"), 23.12, 113.26,
                                                   QStringLiteral("1.23"));
        const qint64 pileId1 = testDb.addPile(stationId, QStringLiteral("PILE-U06-1"),
                                              QStringLiteral("IDLE"));
        const qint64 pileId2 = testDb.addPile(stationId, QStringLiteral("PILE-U06-2"),
                                              QStringLiteral("CHARGING"));
        QVERIFY(testDb.exec(QStringLiteral(
            "INSERT INTO charge_order(id,user_id,pile_id,status,energy_kwh,amount_yuan,"
            "unit_price_yuan_per_kwh,started_at,created_at) "
            "VALUES('ORD-U06',?,?,'CHARGING','5.00','0.00','1.23',"
            "'2026-09-10T08:00:00Z','2026-09-10T08:00:00Z')"),
            {userId, pileId2}));

        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));

        const auto login = send(socket, QStringLiteral("U06-1"),
                                QStringLiteral("USER_LOGIN"),
                                {{QStringLiteral("phone"), QStringLiteral("13800001234")}});
        QVERIFY(login.value(QStringLiteral("ok")).toBool());
        const QString token = data(login).value(QStringLiteral("userToken")).toString();
        QVERIFY(!token.isEmpty());
        QCOMPARE(data(login).value(QStringLiteral("restricted")).toBool(), true);

        // 查询资料与活动订单可用。
        const auto profile = send(socket, QStringLiteral("U06-2"),
                                   QStringLiteral("USER_PROFILE"),
                                   {{QStringLiteral("userToken"), token}});
        QVERIFY(profile.value(QStringLiteral("ok")).toBool());

        const auto active = send(socket, QStringLiteral("U06-3"),
                                  QStringLiteral("ORDER_ACTIVE"),
                                  {{QStringLiteral("userToken"), token}});
        QVERIFY2(active.value(QStringLiteral("ok")).toBool(),
                 qPrintable(QString::fromUtf8(QJsonDocument(active).toJson())));

        // 停止既有订单并结清 → 成功。
        const auto stop = send(socket, QStringLiteral("U06-4"),
                               QStringLiteral("ORDER_STOP"),
                               {{QStringLiteral("userToken"), token},
                                {QStringLiteral("orderId"), QStringLiteral("ORD-U06")}});
        QVERIFY2(stop.value(QStringLiteral("ok")).toBool(),
                 qPrintable(stop.value(QStringLiteral("message")).toString()));
        QCOMPARE(data(stop).value(QStringLiteral("amountYuan")).toString(),
                 QStringLiteral("6.15"));

        const auto pay = send(socket, QStringLiteral("U06-5"),
                              QStringLiteral("WALLET_PAY"),
                              {{QStringLiteral("userToken"), token},
                               {QStringLiteral("orderId"), QStringLiteral("ORD-U06")}});
        QVERIFY2(pay.value(QStringLiteral("ok")).toBool(),
                 qPrintable(pay.value(QStringLiteral("message")).toString()));

        // 新预约 → USER_FROZEN。
        const auto reserve = send(socket, QStringLiteral("U06-6"),
                                  QStringLiteral("ORDER_RESERVE"),
                                  {{QStringLiteral("userToken"), token},
                                   {QStringLiteral("pileId"), pileId1}});
        QVERIFY(!reserve.value(QStringLiteral("ok")).toBool());
        QCOMPARE(codeOf(reserve), QStringLiteral("USER_FROZEN"));

        // 充值仍允许（冻结用户补足余额结清债务）。
        const auto recharge = send(socket, QStringLiteral("U06-7"),
                                   QStringLiteral("WALLET_RECHARGE"),
                                   {{QStringLiteral("userToken"), token},
                                    {QStringLiteral("amountYuan"),
                                     QStringLiteral("5.00")}});
        QVERIFY2(recharge.value(QStringLiteral("ok")).toBool(),
                 qPrintable(recharge.value(QStringLiteral("message")).toString()));
    }

    // U07/U08：REQUEST_STATUS 查本人充值；重登录换 token 后复用原 requestId
    // 重试 → 仅一笔充值、无重复赠送初始余额。
    void u07u08_requestStatusAndIdempotentRecharge()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800001234"),
                                              QStringLiteral("88.66"));
        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));

        const auto login1 = send(socket, QStringLiteral("U08-1"),
                                 QStringLiteral("USER_LOGIN"),
                                 {{QStringLiteral("phone"), QStringLiteral("13800001234")}});
        const QString token1 = data(login1).value(QStringLiteral("userToken")).toString();
        QVERIFY(!token1.isEmpty());

        // 充值 10.00 成功。
        const auto recharge = send(socket, QStringLiteral("U08-2"),
                                   QStringLiteral("WALLET_RECHARGE"),
                                   {{QStringLiteral("userToken"), token1},
                                    {QStringLiteral("amountYuan"),
                                     QStringLiteral("10.00")}});
        QVERIFY2(recharge.value(QStringLiteral("ok")).toBool(),
                 qPrintable(recharge.value(QStringLiteral("message")).toString()));

        // U07：REQUEST_STATUS 用本人 token 查到 COMMITTED。
        const auto status = send(socket, QStringLiteral("U08-3"),
                                 QStringLiteral("REQUEST_STATUS"),
                                 {{QStringLiteral("userToken"), token1},
                                  {QStringLiteral("targetRequestId"),
                                   QStringLiteral("U08-2")}});
        QVERIFY2(status.value(QStringLiteral("ok")).toBool(),
                 qPrintable(status.value(QStringLiteral("message")).toString()));
        QCOMPARE(data(status).value(QStringLiteral("state")).toString(),
                 QStringLiteral("COMMITTED"));

        // 其他用户查（带自己的合法 token）：目标请求不在其名下 → 不存在（NOT_FOUND），
        // 不泄露他人请求是否存在（授权请求本身合法，仅结果受限）。
        const qint64 userIdB = testDb.addUser(QStringLiteral("13900005678"),
                                              QStringLiteral("1.00"));
        Q_UNUSED(userIdB);
        const auto loginB = send(socket, QStringLiteral("U07-2"),
                                 QStringLiteral("USER_LOGIN"),
                                 {{QStringLiteral("phone"), QStringLiteral("13900005678")}});
        const QString tokenB = data(loginB).value(QStringLiteral("userToken")).toString();
        const auto statusB = send(socket, QStringLiteral("U07-3"),
                                  QStringLiteral("REQUEST_STATUS"),
                                  {{QStringLiteral("userToken"), tokenB},
                                   {QStringLiteral("targetRequestId"),
                                    QStringLiteral("U08-2")}});
        QVERIFY(statusB.value(QStringLiteral("ok")).toBool());
        QCOMPARE(data(statusB).value(QStringLiteral("state")).toString(),
                 QStringLiteral("NOT_FOUND"));

        // U08：退出（新连接）重新登录同一手机号 → 新 token；复用原 requestId 同参数重试
        // → 仍仅一笔充值（幂等回放）。
        QTcpSocket socket2;
        socket2.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket2.waitForConnected(2000));
        const auto login2 = send(socket2, QStringLiteral("U08-4"),
                                  QStringLiteral("USER_LOGIN"),
                                  {{QStringLiteral("phone"), QStringLiteral("13800001234")}});
        const QString token2 = data(login2).value(QStringLiteral("userToken")).toString();
        QVERIFY(!token2.isEmpty());
        QVERIFY(token2 != token1);

        const auto replay = send(socket2, QStringLiteral("U08-2"),
                                 QStringLiteral("WALLET_RECHARGE"),
                                 {{QStringLiteral("userToken"), token2},
                                  {QStringLiteral("amountYuan"),
                                   QStringLiteral("10.00")}});
        QVERIFY2(replay.value(QStringLiteral("ok")).toBool(),
                 qPrintable(replay.value(QStringLiteral("message")).toString()));

        // 余额 = 88.66 + 10.00 = 98.66（只加一次），无重复赠送。
        const auto balance = send(socket2, QStringLiteral("U08-5"),
                                  QStringLiteral("WALLET_BALANCE"),
                                  {{QStringLiteral("userToken"), token2}});
        QCOMPARE(data(balance).value(QStringLiteral("balanceYuan")).toString(),
                 QStringLiteral("98.66"));

        // RECHARGE 流水仅一条。
        QSqlQuery count(testDb.database());
        count.exec(QStringLiteral(
            "SELECT COUNT(*) FROM wallet_transaction "
            "WHERE user_id=%1 AND transaction_type='RECHARGE'").arg(userId));
        QVERIFY(count.next());
        QCOMPARE(count.value(0).toInt(), 1);
    }

    // U09：同 requestId 但参数不同 → DUPLICATE_REQUEST，无部分变更。
    void u09_duplicateRequestWithDifferentParams()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        TcpServer server(testDb.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));

        const auto login = send(socket, QStringLiteral("U09-1"),
                                QStringLiteral("USER_LOGIN"),
                                {{QStringLiteral("phone"), QStringLiteral("13800001234")}});
        const QString token = data(login).value(QStringLiteral("userToken")).toString();
        QVERIFY(!token.isEmpty());

        const auto first = send(socket, QStringLiteral("U09-2"),
                                QStringLiteral("WALLET_RECHARGE"),
                                {{QStringLiteral("userToken"), token},
                                 {QStringLiteral("amountYuan"),
                                  QStringLiteral("10.00")}});
        QVERIFY2(first.value(QStringLiteral("ok")).toBool(),
                 qPrintable(first.value(QStringLiteral("message")).toString()));

        // 同 requestId，金额改成 20.00 → DUPLICATE_REQUEST。
        const auto conflict = send(socket, QStringLiteral("U09-2"),
                                   QStringLiteral("WALLET_RECHARGE"),
                                   {{QStringLiteral("userToken"), token},
                                    {QStringLiteral("amountYuan"),
                                     QStringLiteral("20.00")}});
        QVERIFY(!conflict.value(QStringLiteral("ok")).toBool());
        QCOMPARE(codeOf(conflict), QStringLiteral("DUPLICATE_REQUEST"));

        // 余额只反映第一笔：100.00 + 10.00 = 110.00。
        const auto balance = send(socket, QStringLiteral("U09-3"),
                                  QStringLiteral("WALLET_BALANCE"),
                                  {{QStringLiteral("userToken"), token}});
        QCOMPARE(data(balance).value(QStringLiteral("balanceYuan")).toString(),
                 QStringLiteral("110.00"));
    }
};

QTEST_MAIN(UserAuthorizationProtocolTest)
#include "user_authorization_protocol_test.moc"
