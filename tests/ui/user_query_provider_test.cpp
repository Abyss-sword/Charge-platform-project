#include <QtTest>

#include "adapters/remote_station_provider.h"
#include "adapters/remote_user_account_provider.h"
#include "adapters/remote_user_order_provider.h"
#include "fake_tcp_server.h"
#include "pages/home_page.h"
#include "pages/charging_history_page.h"

#include <QJsonArray>
#include <QLabel>
#include <QSignalSpy>
#include <QTableWidget>
#include <QScrollBar>

namespace {
QJsonObject orderObject(const QString &orderId = QStringLiteral("ORD-Q-1"))
{
    return {{QStringLiteral("orderId"), orderId},
            {QStringLiteral("userId"), qint64(42)},
            {QStringLiteral("pileId"), qint64(9000000001LL)},
            {QStringLiteral("status"), QStringLiteral("CHARGING")},
            {QStringLiteral("energyKwh"), QStringLiteral("8.35")},
            {QStringLiteral("unitPriceYuanPerKwh"), QStringLiteral("1.23")},
            {QStringLiteral("amountYuan"), QStringLiteral("10.27")},
            {QStringLiteral("reservedUntil"), QJsonValue::Null},
            {QStringLiteral("startedAt"), QStringLiteral("2026-09-09T10:00:00Z")},
            {QStringLiteral("endedAt"), QJsonValue::Null},
            {QStringLiteral("createdAt"), QStringLiteral("2026-09-09T09:50:00Z")}};
}

QJsonObject failure(const QJsonObject &request, const QString &code,
                    const QString &message)
{
    return {{QStringLiteral("requestId"), request.value(QStringLiteral("requestId"))},
            {QStringLiteral("ok"), false}, {QStringLiteral("code"), code},
            {QStringLiteral("message"), message}, {QStringLiteral("data"), QJsonObject{}},
            {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code},
                                                   {QStringLiteral("message"), message}}}};
}

class HistoryUiProvider final : public IUserOrderProvider
{
public:
    using IUserOrderProvider::IUserOrderProvider;
    void reserve(qint64, qint64) override {}
    void start(const QString &) override {}
    void stop(const QString &) override {}
    void cancel(const QString &) override {}
    void pay(const QString &) override {}
    void recharge(qint64, const QString &) override {}
    void queryHistory(qint64, int, int) override { ++queryCount; }
    void complete(const UserOrderQueryResult &result) { emit queryFinished(result); }
    int queryCount = 0;
};
}

class UserQueryProviderTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qRegisterMetaType<UserProfileQueryResult>();
        qRegisterMetaType<WalletBalanceQueryResult>();
        qRegisterMetaType<StationDetailResult>();
        qRegisterMetaType<UserOrderQueryResult>();
    }

    void accountQueriesAndPreservesServerCode()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            const QString action = request.value(QStringLiteral("action")).toString();
            if (request.value(QStringLiteral("payload")).toObject()
                    .value(QStringLiteral("userId")).toInteger() == 99) {
                socket->write(FakeTcpServer::frame(failure(
                    request, QStringLiteral("NOT_FOUND"), QStringLiteral("用户不存在"))));
                return;
            }
            if (action == QStringLiteral("USER_PROFILE")) {
                socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                    request.value(QStringLiteral("requestId")).toString(),
                    {{QStringLiteral("userId"), qint64(42)},
                     {QStringLiteral("phoneMasked"), QStringLiteral("138****5678")},
                     {QStringLiteral("nickname"), QStringLiteral("Alice")},
                     {QStringLiteral("balanceYuan"), QStringLiteral("123.45")},
                     {QStringLiteral("status"), QStringLiteral("NORMAL")}})));
            } else if (action == QStringLiteral("WALLET_BALANCE")) {
                socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                    request.value(QStringLiteral("requestId")).toString(),
                    {{QStringLiteral("userId"), qint64(42)},
                     {QStringLiteral("balanceYuan"), QStringLiteral("223.45")}})));
            } else if (action == QStringLiteral("USER_PROFILE_UPDATE")) {
                socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                    request.value(QStringLiteral("requestId")).toString(),
                    {{QStringLiteral("userId"), qint64(42)},
                     {QStringLiteral("nickname"), QStringLiteral("新昵称")}})));
            } else {
                socket->write(FakeTcpServer::frame(failure(
                    request, QStringLiteral("PARAM_INVALID"), QStringLiteral("参数错误"))));
            }
        };
        RemoteUserAccountProvider provider(QStringLiteral("127.0.0.1"), server.port());
        provider.setUserToken(QStringLiteral("account-token"));
        QSignalSpy profiles(&provider, &IUserAccountProvider::profileFinished);
        QSignalSpy balances(&provider, &IUserAccountProvider::balanceFinished);
        QSignalSpy updates(&provider, &IUserAccountProvider::profileUpdateFinished);

        provider.queryProfile(42);
        QTRY_COMPARE(profiles.size(), 1);
        const auto profile = qvariant_cast<UserProfileQueryResult>(profiles.takeFirst().at(0));
        QCOMPARE(profile.kind, UserAccountResultKind::Success);
        QCOMPARE(profile.profile.userId, qint64(42));
        QCOMPARE(profile.profile.nickname, QStringLiteral("Alice"));
        QCOMPARE(profile.profile.balanceYuan, QStringLiteral("123.45"));
        QCOMPARE(server.requests.at(0).value(QStringLiteral("payload")).toObject()
                     .value(QStringLiteral("userId")).toInteger(), qint64(42));
        QCOMPARE(server.requests.at(0).value(QStringLiteral("payload")).toObject()
                     .value(QStringLiteral("userToken")).toString(),
                 QStringLiteral("account-token"));

        provider.queryBalance(42);
        QTRY_COMPARE(balances.size(), 1);
        const auto balance = qvariant_cast<WalletBalanceQueryResult>(balances.takeFirst().at(0));
        QCOMPARE(balance.kind, UserAccountResultKind::Success);
        QCOMPARE(balance.balanceYuan, QStringLiteral("223.45"));

        provider.updateProfile(42, QStringLiteral("新昵称"));
        QTRY_COMPARE(updates.size(), 1);
        const auto updated = qvariant_cast<UserProfileUpdateResult>(updates.takeFirst().at(0));
        QCOMPARE(updated.kind, UserAccountResultKind::Success);
        QCOMPARE(updated.nickname, QStringLiteral("新昵称"));

        provider.queryProfile(99);
        QTRY_COMPARE(profiles.size(), 1);
        const auto missing = qvariant_cast<UserProfileQueryResult>(profiles.takeFirst().at(0));
        QCOMPARE(missing.kind, UserAccountResultKind::Error);
        QCOMPARE(missing.errorCode, QStringLiteral("NOT_FOUND"));
        QCOMPARE(missing.message, QStringLiteral("用户不存在"));

        provider.queryBalance(0);
        QCOMPARE(balances.size(), 1);
        const auto invalid = qvariant_cast<WalletBalanceQueryResult>(balances.takeFirst().at(0));
        QCOMPARE(invalid.errorCode, QStringLiteral("PARAM_INVALID"));
    }

    void stationDetailMapsRealPileIds()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            QCOMPARE(request.value(QStringLiteral("action")).toString(),
                     QStringLiteral("STATION_DETAIL"));
            socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                request.value(QStringLiteral("requestId")).toString(),
                {{QStringLiteral("stationId"), qint64(7000000001LL)},
                 {QStringLiteral("name"), QStringLiteral("广州测试站")},
                 {QStringLiteral("address"), QStringLiteral("广州市天河区")},
                 {QStringLiteral("latitude"), 23.1351},
                 {QStringLiteral("longitude"), 113.3245},
                 {QStringLiteral("priceYuanPerKwh"), QStringLiteral("1.23")},
                 {QStringLiteral("status"), QStringLiteral("OPEN")},
                 {QStringLiteral("piles"), QJsonArray{
                     QJsonObject{{QStringLiteral("pileId"), qint64(9000000001LL)},
                                 {QStringLiteral("serial"), QStringLiteral("GZ-001")},
                                 {QStringLiteral("chargeType"), QStringLiteral("FAST")},
                                 {QStringLiteral("ratedPowerKw"), 120.0},
                                 {QStringLiteral("status"), QStringLiteral("IDLE")},
                                 {QStringLiteral("lastHeartbeatAt"),
                                  QStringLiteral("2026-09-09T10:00:00Z")}}}}})));
        };
        RemoteStationProvider provider(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy spy(&provider, &IStationProvider::detailFinished);
        provider.queryDetail(7000000001LL);
        QTRY_COMPARE(spy.size(), 1);
        const auto result = qvariant_cast<StationDetailResult>(spy.takeFirst().at(0));
        QCOMPARE(result.kind, StationQueryResultKind::Success);
        QCOMPARE(result.detail.stationId, qint64(7000000001LL));
        QCOMPARE(result.detail.piles.size(), 1);
        QCOMPARE(result.detail.piles.first().pileId, qint64(9000000001LL));
        QCOMPARE(result.detail.piles.first().status, QStringLiteral("IDLE"));
    }

    void orderActiveDetailAndHistory()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            const QString action = request.value(QStringLiteral("action")).toString();
            QJsonObject data;
            if (action == QStringLiteral("ORDER_ACTIVE"))
                data.insert(QStringLiteral("order"), orderObject());
            else if (action == QStringLiteral("ORDER_DETAIL"))
                data = orderObject();
            else
                data.insert(QStringLiteral("rows"), QJsonArray{orderObject(QStringLiteral("ORD-H-1"))});
            socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                request.value(QStringLiteral("requestId")).toString(), data)));
        };
        RemoteUserOrderProvider provider(QStringLiteral("127.0.0.1"), server.port());
        provider.setUserToken(QStringLiteral("order-query-token"));
        QSignalSpy spy(&provider, &IUserOrderProvider::queryFinished);

        provider.queryActive(42);
        QTRY_COMPARE(spy.size(), 1);
        auto result = qvariant_cast<UserOrderQueryResult>(spy.takeFirst().at(0));
        QCOMPARE(result.query, UserOrderQueryKind::Active);
        QVERIFY(result.hasOrder);
        QCOMPARE(result.order.pileId, qint64(9000000001LL));

        provider.queryDetail(QStringLiteral("ORD-Q-1"));
        QTRY_COMPARE(spy.size(), 1);
        result = qvariant_cast<UserOrderQueryResult>(spy.takeFirst().at(0));
        QCOMPARE(result.query, UserOrderQueryKind::Detail);
        QCOMPARE(result.order.energyKwh, QStringLiteral("8.35"));

        provider.queryHistory(42, 20, 0);
        QTRY_COMPARE(spy.size(), 1);
        result = qvariant_cast<UserOrderQueryResult>(spy.takeFirst().at(0));
        QCOMPARE(result.query, UserOrderQueryKind::History);
        QCOMPARE(result.orders.size(), 1);
        QCOMPARE(result.orders.first().orderId, QStringLiteral("ORD-H-1"));
        QCOMPARE(server.requests.at(2).value(QStringLiteral("payload")).toObject()
                     .value(QStringLiteral("limit")).toInt(), 20);
        for (const QJsonObject &request : server.requests) {
            QCOMPARE(request.value(QStringLiteral("payload")).toObject()
                         .value(QStringLiteral("userToken")).toString(),
                     QStringLiteral("order-query-token"));
        }
    }

    void staleHomeKeepsLastProfile()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                request.value(QStringLiteral("requestId")).toString(),
                {{QStringLiteral("userId"), qint64(42)},
                 {QStringLiteral("phoneMasked"), QStringLiteral("138****5678")},
                 {QStringLiteral("nickname"), QStringLiteral("Alice")},
                 {QStringLiteral("balanceYuan"), QStringLiteral("123.45")},
                 {QStringLiteral("status"), QStringLiteral("NORMAL")}})));
            socket->disconnectFromHost();
        };
        RemoteUserAccountProvider provider(QStringLiteral("127.0.0.1"), server.port());
        HomePage page({QStringLiteral("首页"), {}, {}, {}, Ui::PageState::Normal},
                      &provider, 42);
        page.refresh();
        QLabel *profile = nullptr;
        for (QLabel *label : page.findChildren<QLabel *>())
            if (label->text().contains(QStringLiteral("Alice"))) profile = label;
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
            for (QLabel *label : page.findChildren<QLabel *>())
                if (label->text().contains(QStringLiteral("Alice"))) return true;
            return false;
        })(), 1000);
        for (QLabel *label : page.findChildren<QLabel *>())
            if (label->text().contains(QStringLiteral("Alice"))) profile = label;
        QVERIFY(profile);
        const QString lastData = profile->text();
        QTRY_COMPARE(page.stateWidget()->state(), Ui::PageState::Offline);
        QCOMPARE(profile->text(), lastData);
    }

    void historyPageScrollsManyOrders()
    {
        HistoryUiProvider provider;
        ChargingHistoryPage page({QStringLiteral("记录"),{}, {}, {}, Ui::PageState::Normal},
                                 &provider,42);
        page.resize(420,500); page.show(); page.refresh(); QCOMPARE(provider.queryCount,1);
        UserOrderQueryResult result; result.kind=UserOrderResultKind::Success;
        result.query=UserOrderQueryKind::History;
        for(int i=0;i<60;++i){UserOrderSnapshot order;order.orderId=QStringLiteral("ORD-%1").arg(i);order.status=QStringLiteral("FINISHED");order.energyKwh=QStringLiteral("1.00");order.amountYuan=QStringLiteral("2.00");result.orders.append(order);}
        provider.complete(result); QCoreApplication::processEvents();
        auto *table=page.findChild<QTableWidget*>(QStringLiteral("historyOrdersTable")); QVERIFY(table);
        QCOMPARE(table->rowCount(),60); QVERIFY(table->verticalScrollBar()->maximum()>0);
    }
};

QTEST_MAIN(UserQueryProviderTest)
#include "user_query_provider_test.moc"
