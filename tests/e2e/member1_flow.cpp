// 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
// 端到端业务闭环（答辩演示入口）：真实 TCP + 临时 SQLite 走完整订单流程，
// 成功输出应包含 ORDER=FINISHED、PILE=IDLE、PAYMENT_COUNT=1、
// REVENUE_BEFORE=0.00、REVENUE_AFTER=10.27——重复付款仍只有一笔支付。

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTcpSocket>
#include <QTextStream>

#include "server/network/tcp_server.h"
#include "server/services/admin_service.h"
#include "shared/protocol/frame_codec.h"
#include "tests/helpers/test_database.h"

namespace {
int fail(const QString &message)
{
    QTextStream(stderr) << "FLOW_ERROR=" << message << '\n';
    return 1;
}

QJsonObject send(QTcpSocket &socket, const QString &requestId, const QString &action,
                 const QJsonObject &payload)
{
    const QJsonObject request{
        {QStringLiteral("version"), QStringLiteral("1.0")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("action"), action},
        {QStringLiteral("payload"), payload}
    };
    socket.write(FrameCodec::encode(QJsonDocument(request).toJson(QJsonDocument::Compact)));
    socket.flush();
    if (!socket.waitForReadyRead(2000)) {
        return {};
    }
    FrameCodec codec;
    QList<QByteArray> frames = codec.feed(socket.readAll());
    while (frames.isEmpty() && socket.waitForReadyRead(200)) {
        frames.append(codec.feed(socket.readAll()));
    }
    return frames.isEmpty() ? QJsonObject{}
                            : QJsonDocument::fromJson(frames.first()).object();
}

QJsonObject send(TcpServer &server, const QString &requestId, const QString &action,
                 const QJsonObject &payload)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, server.port());
    if (!socket.waitForConnected(2000)) {
        return {};
    }
    return send(socket, requestId, action, payload);
}

bool succeeded(const QJsonObject &response)
{
    return response.value(QStringLiteral("ok")).toBool();
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    TestDatabase testDb;
    if (!testDb.isReady()) {
        return fail(testDb.error());
    }

    const qint64 stationId = testDb.addStation(QStringLiteral("闭环测试站"),
                                               23.1291, 113.2644,
                                               QStringLiteral("1.23"));
    const qint64 pileId = testDb.addPile(stationId, QStringLiteral("PILE-FLOW-001"));
    if (stationId <= 0 || pileId <= 0) {
        return fail(testDb.error());
    }
    if (!AdminService(testDb.database()).ensureDefaultAdmin().ok) {
        return fail(QStringLiteral("ADMIN_BOOTSTRAP_FAILED"));
    }

    TcpServer server(testDb.path());
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        return fail(QStringLiteral("SERVER_START_FAILED"));
    }

    const QJsonObject invalidLocation = send(
        server, QStringLiteral("FLOW-BAD-GEO-1"), QStringLiteral("STATION_NEARBY"), {
            {QStringLiteral("latitude"), 91.0}, {QStringLiteral("longitude"), 113.0}
        });
    if (succeeded(invalidLocation)
        || invalidLocation.value(QStringLiteral("code")).toString()
            != QStringLiteral("PARAM_INVALID")
        || !succeeded(send(server, QStringLiteral("FLOW-PING-AFTER-BAD-GEO"),
                           QStringLiteral("PING"), {}))) {
        return fail(QStringLiteral("INVALID_COORDINATE_GUARD_FAILED"));
    }

    const QJsonObject login = send(server, QStringLiteral("FLOW-LOGIN-1"),
                                   QStringLiteral("USER_LOGIN"),
                                   {{QStringLiteral("phone"), QStringLiteral("13800138999")}});
    if (!succeeded(login)) {
        return fail(QStringLiteral("LOGIN_FAILED"));
    }
    const QJsonObject unauthorizedRevenue = send(
        server, QStringLiteral("FLOW-REVENUE-NO-AUTH"),
        QStringLiteral("ADMIN_REVENUE"), {});
    if (succeeded(unauthorizedRevenue)
        || unauthorizedRevenue.value(QStringLiteral("code")).toString()
            != QStringLiteral("AUTH_INVALID")) {
        return fail(QStringLiteral("ADMIN_AUTH_GUARD_FAILED"));
    }
    const QJsonObject adminLogin = send(server, QStringLiteral("FLOW-ADMIN-LOGIN-1"),
                                        QStringLiteral("ADMIN_LOGIN"), {
            {QStringLiteral("username"), QStringLiteral("admin")},
            {QStringLiteral("password"), QStringLiteral("123456")}
        });
    const QString adminToken = adminLogin.value(QStringLiteral("data")).toObject()
                                   .value(QStringLiteral("adminToken")).toString();
    if (!succeeded(adminLogin) || adminToken.isEmpty()) {
        return fail(QStringLiteral("ADMIN_LOGIN_FAILED"));
    }
    const qint64 userId = login.value(QStringLiteral("data")).toObject()
                              .value(QStringLiteral("userId")).toInteger();
    const QString userToken = login.value(QStringLiteral("data")).toObject()
                                  .value(QStringLiteral("userToken")).toString();
    if (userToken.isEmpty()) {
        return fail(QStringLiteral("USER_TOKEN_MISSING"));
    }

    QTcpSocket validationSocket;
    validationSocket.connectToHost(QHostAddress::LocalHost, server.port());
    if (!validationSocket.waitForConnected(2000)) {
        return fail(QStringLiteral("VALIDATION_CONNECTION_FAILED"));
    }
    // M1-03：网络 ORDER_TELEMETRY 已关闭；十进制校验经 PILE_TELEMETRY 的
    // energyKwh 参数验证（非法字符串/负值 → PARAM_INVALID）。
    const QList<QPair<QString, QJsonObject>> invalidDecimalRequests{
        {QStringLiteral("FLOW-BAD-NUMERIC"),
         {{QStringLiteral("pileId"), 1},
          {QStringLiteral("recordedAt"),
           QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
          {QStringLiteral("temperatureC"), 25.0},
          {QStringLiteral("voltageV"), 220.0},
          {QStringLiteral("currentA"), 16.0},
          {QStringLiteral("powerW"), 3520},
          {QStringLiteral("energyKwh"), QStringLiteral("invalid")}}},
        {QStringLiteral("FLOW-BAD-NEGATIVE"),
         {{QStringLiteral("pileId"), 1},
          {QStringLiteral("recordedAt"),
           QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
          {QStringLiteral("temperatureC"), 25.0},
          {QStringLiteral("voltageV"), 220.0},
          {QStringLiteral("currentA"), 16.0},
          {QStringLiteral("powerW"), 3520},
          {QStringLiteral("energyKwh"), QStringLiteral("-0.01")}}},
        {QStringLiteral("FLOW-BAD-NEGATIVE-ROUNDS-TO-ZERO"),
         {{QStringLiteral("pileId"), 1},
          {QStringLiteral("recordedAt"),
           QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
          {QStringLiteral("temperatureC"), 25.0},
          {QStringLiteral("voltageV"), 220.0},
          {QStringLiteral("currentA"), 16.0},
          {QStringLiteral("powerW"), 3520},
          {QStringLiteral("energyKwh"), QStringLiteral("-0.004")}}},
        {QStringLiteral("FLOW-BAD-NEGATIVE-ZERO"),
         {{QStringLiteral("pileId"), 1},
          {QStringLiteral("recordedAt"),
           QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
          {QStringLiteral("temperatureC"), 25.0},
          {QStringLiteral("voltageV"), 220.0},
          {QStringLiteral("currentA"), 16.0},
          {QStringLiteral("powerW"), 3520},
          {QStringLiteral("energyKwh"), QStringLiteral("-0.00")}}}
    };
    for (const auto &invalid : invalidDecimalRequests) {
        const auto response = send(validationSocket, invalid.first,
                                   QStringLiteral("PILE_TELEMETRY"), invalid.second);
        if (succeeded(response)
            || response.value(QStringLiteral("code")).toString()
                != QStringLiteral("PARAM_INVALID")) {
            return fail(QStringLiteral("DECIMAL_VALIDATION_FAILED"));
        }
    }
    // 已关闭的 ORDER_TELEMETRY 网络入口必须 AUTH_INVALID（无隐藏后门）。
    {
        const auto blocked = send(validationSocket,
                                  QStringLiteral("FLOW-ORDER-TEL-BLOCKED"),
                                  QStringLiteral("ORDER_TELEMETRY"),
                                  {{QStringLiteral("orderId"), QStringLiteral("x")},
                                   {QStringLiteral("energyKwh"),
                                    QStringLiteral("1.00")}});
        if (succeeded(blocked)
            || blocked.value(QStringLiteral("code")).toString()
                != QStringLiteral("AUTH_INVALID")) {
            return fail(QStringLiteral("ORDER_TELEMETRY_BACKDOOR"));
        }
    }
    const auto invalidRecharge = send(validationSocket, QStringLiteral("FLOW-BAD-AMOUNT"),
                                      QStringLiteral("WALLET_RECHARGE"), {
        {QStringLiteral("userToken"), userToken},
        {QStringLiteral("userId"), userId}, {QStringLiteral("amountYuan"), 100.0}
    });
    const auto invalidStationPrice = send(validationSocket, QStringLiteral("FLOW-BAD-PRICE"),
                                          QStringLiteral("ADMIN_STATION_CREATE"), {
        {QStringLiteral("adminToken"), adminToken},
        {QStringLiteral("name"), QStringLiteral("非法单价站")},
        {QStringLiteral("address"), QStringLiteral("测试路")},
        {QStringLiteral("latitude"), 23.0},
        {QStringLiteral("longitude"), 113.0},
        {QStringLiteral("priceYuanPerKwh"), QStringLiteral("-1.23")},
        {QStringLiteral("piles"), QJsonArray{
            QJsonObject{{QStringLiteral("serial"), QStringLiteral("PILE-BAD-PRICE")},
                        {QStringLiteral("chargeType"), QStringLiteral("FAST")},
                        {QStringLiteral("ratedPowerKw"), 120.0}}
        }}
    });
    const auto validStationPrice = send(validationSocket, QStringLiteral("FLOW-VALID-PRICE"),
                                        QStringLiteral("ADMIN_STATION_CREATE"), {
        {QStringLiteral("adminToken"), adminToken},
        {QStringLiteral("name"), QStringLiteral("协议测试站")},
        {QStringLiteral("address"), QStringLiteral("测试路")},
        {QStringLiteral("latitude"), 23.0},
        {QStringLiteral("longitude"), 113.0},
        {QStringLiteral("priceYuanPerKwh"), QStringLiteral("1.23")},
        {QStringLiteral("piles"), QJsonArray{
            QJsonObject{{QStringLiteral("serial"), QStringLiteral("PILE-V2-001")},
                        {QStringLiteral("chargeType"), QStringLiteral("FAST")},
                        {QStringLiteral("ratedPowerKw"), 120.0}}
        }}
    });
    if (succeeded(invalidRecharge) || succeeded(invalidStationPrice)
        || invalidRecharge.value(QStringLiteral("code")).toString()
            != QStringLiteral("PARAM_INVALID")
        || invalidStationPrice.value(QStringLiteral("code")).toString()
            != QStringLiteral("PARAM_INVALID")
        || !succeeded(validStationPrice)
        || !succeeded(send(validationSocket, QStringLiteral("FLOW-PING-AFTER-BAD-DECIMAL"),
                           QStringLiteral("PING"), {}))) {
        return fail(QStringLiteral("DECIMAL_CONNECTION_RECOVERY_FAILED"));
    }

    if (!succeeded(send(server, QStringLiteral("FLOW-RECHARGE-1"),
                        QStringLiteral("WALLET_RECHARGE"), {
            {QStringLiteral("userToken"), userToken},
            {QStringLiteral("userId"), userId},
            {QStringLiteral("amountYuan"), QStringLiteral("100.00")}
        }))) {
        return fail(QStringLiteral("RECHARGE_FAILED"));
    }
    const QJsonObject revenueBeforeResponse = send(
        server, QStringLiteral("FLOW-REVENUE-1"), QStringLiteral("ADMIN_REVENUE"),
        {{QStringLiteral("adminToken"), adminToken}});
    const QString revenueBefore = revenueBeforeResponse.value(QStringLiteral("data")).toObject()
                                      .value(QStringLiteral("totalRevenueYuan")).toString();

    const QJsonObject reserved = send(server, QStringLiteral("FLOW-RESERVE-1"),
                                      QStringLiteral("ORDER_RESERVE"), {
        {QStringLiteral("userToken"), userToken},
        {QStringLiteral("userId"), userId}, {QStringLiteral("pileId"), pileId}
    });
    const QString orderId = reserved.value(QStringLiteral("data")).toObject()
                                .value(QStringLiteral("orderId")).toString();
    // M1-03：预置设备新鲜累计读数基准 100.00（PILE 累计口径），START 前必需。
    {
        const QString seedAt =
            QDateTime::currentDateTimeUtc().addSecs(-8).toString(Qt::ISODate);
        QSqlQuery seed(testDb.database());
        seed.prepare(QStringLiteral(
            "INSERT INTO pile_telemetry(pile_id,recorded_at,temperature_c,voltage_v,"
            "current_a,power_w,energy_kwh) VALUES(?,?,25,220,16,3520,'100.00')"));
        seed.addBindValue(pileId);
        seed.addBindValue(seedAt);
        if (!seed.exec()) {
            return fail(QStringLiteral("METER_SEED_FAILED"));
        }
        QSqlQuery beat(testDb.database());
        beat.prepare(QStringLiteral(
            "UPDATE pile_info SET last_heartbeat_at=? WHERE id=?"));
        beat.addBindValue(seedAt);
        beat.addBindValue(pileId);
        if (!beat.exec()) {
            return fail(QStringLiteral("HEARTBEAT_SEED_FAILED"));
        }
    }
    if (!succeeded(reserved) || orderId.isEmpty()
        || !succeeded(send(server, QStringLiteral("FLOW-START-1"),
                           QStringLiteral("ORDER_START"),
                           {{QStringLiteral("userToken"), userToken},
                            {QStringLiteral("orderId"), orderId}}))
        || !succeeded(send(server, QStringLiteral("FLOW-TELEMETRY-1"),
                           QStringLiteral("PILE_TELEMETRY"), {
            {QStringLiteral("pileId"), pileId},
            {QStringLiteral("recordedAt"),
             QDateTime::currentDateTimeUtc().addSecs(-4).toString(Qt::ISODate)},
            {QStringLiteral("temperatureC"), 25.0},
            {QStringLiteral("voltageV"), 220.0},
            {QStringLiteral("currentA"), 16.0},
            {QStringLiteral("powerW"), 3520},
            {QStringLiteral("energyKwh"), QStringLiteral("108.35")}
        }))) {
        return fail(QStringLiteral("ORDER_FLOW_FAILED"));
    }
    const QJsonObject stopped = send(server, QStringLiteral("FLOW-STOP-1"),
                                     QStringLiteral("ORDER_STOP"),
                                     {{QStringLiteral("userToken"), userToken},
                                      {QStringLiteral("orderId"), orderId}});
    const QJsonObject settlement = stopped.value(QStringLiteral("data")).toObject();
    if (!succeeded(stopped)
        || settlement.value(QStringLiteral("energyKwh")).toString()
            != QStringLiteral("8.35")
        || settlement.value(QStringLiteral("unitPriceYuanPerKwh")).toString()
            != QStringLiteral("1.23")
        || settlement.value(QStringLiteral("amountYuan")).toString()
            != QStringLiteral("10.27")) {
        return fail(QStringLiteral("SETTLEMENT_FAILED"));
    }

    if (!succeeded(send(server, QStringLiteral("FLOW-PAY-1"),
                        QStringLiteral("WALLET_PAY"),
                        {{QStringLiteral("userToken"), userToken},
                         {QStringLiteral("orderId"), orderId}}))
        || !succeeded(send(server, QStringLiteral("FLOW-PAY-1"),
                           QStringLiteral("WALLET_PAY"),
                           {{QStringLiteral("userToken"), userToken},
                            {QStringLiteral("orderId"), orderId}}))) {
        return fail(QStringLiteral("PAYMENT_FAILED"));
    }

    const QString orderStatus = testDb.scalar(
        QStringLiteral("SELECT status FROM charge_order WHERE id=?"),
        {orderId}).toString();
    const QString pileStatus = testDb.scalar(
        QStringLiteral("SELECT status FROM pile_info WHERE id=?"), {pileId}).toString();
    const int paymentCount = testDb.scalar(QStringLiteral(
        "SELECT COUNT(*) FROM wallet_transaction WHERE request_id='FLOW-PAY-1' "
        "AND transaction_type='PAYMENT'" )).toInt();
    const QJsonObject revenueAfterResponse = send(
        server, QStringLiteral("FLOW-REVENUE-2"), QStringLiteral("ADMIN_REVENUE"),
        {{QStringLiteral("adminToken"), adminToken}});
    const QString revenueAfter = revenueAfterResponse.value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("totalRevenueYuan")).toString();

    if (orderStatus != QStringLiteral("FINISHED") || pileStatus != QStringLiteral("IDLE")
        || paymentCount != 1 || revenueBefore != QStringLiteral("0.00")
        || revenueAfter != QStringLiteral("10.27")) {
        return fail(QStringLiteral("FINAL_STATE_MISMATCH"));
    }

    QTextStream output(stdout);
    output << "ORDER=" << orderStatus << '\n'
           << "PILE=" << pileStatus << '\n'
           << "PAYMENT_COUNT=" << paymentCount << '\n'
           << "REVENUE_BEFORE=" << revenueBefore << '\n'
           << "REVENUE_AFTER=" << revenueAfter << '\n';
    return 0;
}
