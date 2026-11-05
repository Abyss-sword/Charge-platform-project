#include <QtTest>

#include "admin_provider.h"
#include "fake_tcp_server.h"
#include "remote_admin_provider.h"
#include "admin_pages.h"

#include <QJsonArray>
#include <QSignalSpy>
#include <QTableWidget>
#include <QComboBox>
#include <QLabel>
#ifdef ADMIN_HAS_CHARTS
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QDateTimeAxis>
#endif

namespace {

AdminLoginResult loginAt(const QSignalSpy &spy)
{
    return qvariant_cast<AdminLoginResult>(spy.constFirst().at(0));
}

AdminOrderQueryResult ordersAt(const QSignalSpy &spy)
{
    return qvariant_cast<AdminOrderQueryResult>(spy.constFirst().at(0));
}

QJsonObject orderRow()
{
    return {{QStringLiteral("orderId"), QStringLiteral("ORD-SHARED-1")},
            {QStringLiteral("userId"), QJsonValue(qint64(9876543210123LL))},
            {QStringLiteral("pileId"), QJsonValue(qint64(1234567890123LL))},
            {QStringLiteral("status"), QStringLiteral("FINISHED")},
            {QStringLiteral("energyKwh"), QStringLiteral("8.35")},
            {QStringLiteral("amountYuan"), QStringLiteral("10.27")},
            {QStringLiteral("createdAt"), QStringLiteral("2026-09-08T10:00:00Z")},
            {QStringLiteral("endedAt"), QJsonValue::Null}};
}

QJsonObject logRow()
{
    return {{QStringLiteral("logId"), QJsonValue(qint64(1234567890123LL))},
            {QStringLiteral("actorType"), QStringLiteral("ADMIN")},
            {QStringLiteral("actorId"), QStringLiteral("admin")},
            {QStringLiteral("action"), QStringLiteral("PAYMENT")},
            {QStringLiteral("targetType"), QStringLiteral("ORDER")},
            {QStringLiteral("targetId"), QStringLiteral("ORD-SHARED-1")},
            {QStringLiteral("result"), QStringLiteral("OK")},
            {QStringLiteral("createdAt"), QStringLiteral("2026-09-08T10:00:00Z")}};
}

class FakeAdminProvider final : public IAdminProvider
{
public:
    using IAdminProvider::IAdminProvider;
    void login(const QString &, const QString &) override {}
    void queryOrders(const QString &token, const QString &, int limit, int offset) override
    { lastToken=token; lastLimit=limit; lastOffset=offset; ++queryCount; }
    void queryLogs(const QString &token, const QString &, int limit, int offset) override
    { lastToken=token; lastLimit=limit; lastOffset=offset; ++queryCount; }
    void queryRevenue(const QString &) override { ++revenueQueries; }
    void queryOverview(const QString &) override { ++overviewQueries; }
    void queryRevenueTrend(const QString &, int days) override { trendDays=days; ++trendQueries; }
    void complete(const AdminOrderQueryResult &result) { emit ordersFinished(result); }
    void completePiles(const AdminPileQueryResult &result) { emit pilesFinished(result); }
    void completeRevenue(const AdminRevenueResult &result) { emit revenueFinished(result); }
    void completeOverview(const AdminOverviewResult &result) { emit overviewFinished(result); }
    void completeTrend(const AdminRevenueTrendResult &result) { emit revenueTrendFinished(result); }
    void completeLogs(const AdminLogQueryResult &result) { emit logsFinished(result); }
    void disconnectTransport() { emit connectivityLost(); }
    void reconnectTransport() { emit connectivityRestored(); }
    QString lastToken;
    int lastLimit=0;
    int lastOffset=0;
    int queryCount=0;
    int revenueQueries=0, overviewQueries=0, trendQueries=0, trendDays=0;
};

} // namespace

class RemoteAdminProviderTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qRegisterMetaType<AdminLoginResult>();
        qRegisterMetaType<AdminOrderQueryResult>();
        qRegisterMetaType<AdminLogQueryResult>();
        qRegisterMetaType<AdminOverviewResult>();
        qRegisterMetaType<AdminRevenueResult>();
        qRegisterMetaType<AdminRevenueTrendResult>();
        qRegisterMetaType<AdminStationQueryResult>();
        qRegisterMetaType<AdminPileQueryResult>();
        qRegisterMetaType<AdminUserQueryResult>();
        qRegisterMetaType<AdminActionResult>();
    }

    void loginThenQueryOrders()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            const QString action = request.value(QStringLiteral("action")).toString();
            QJsonObject data;
            if (action == QStringLiteral("ADMIN_LOGIN"))
                data = {{QStringLiteral("adminToken"), QStringLiteral("TOKEN-1")}};
            else if (action == QStringLiteral("ADMIN_ORDERS"))
                data = {{QStringLiteral("rows"), QJsonArray{orderRow()}}};
            else data = {{QStringLiteral("rows"), QJsonArray{logRow()}}};
            socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                request.value(QStringLiteral("requestId")).toString(), data)));
        };
        RemoteAdminProvider provider(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy logins(&provider, &IAdminProvider::loginFinished);
        QSignalSpy orders(&provider, &IAdminProvider::ordersFinished);
        QSignalSpy logs(&provider, &IAdminProvider::logsFinished);
        provider.login(QStringLiteral("admin"), QStringLiteral("123456"));
        QTRY_COMPARE(logins.count(), 1);
        QCOMPARE(loginAt(logins).kind, AdminResultKind::Success);
        QCOMPARE(loginAt(logins).adminToken, QStringLiteral("TOKEN-1"));
        QCOMPARE(server.requests.first().value(QStringLiteral("action")).toString(),
                 QStringLiteral("ADMIN_LOGIN"));
        const QJsonObject credentials = server.requests.first()
                                            .value(QStringLiteral("payload")).toObject();
        QCOMPARE(credentials.size(), 2);
        QCOMPARE(credentials.value(QStringLiteral("username")).toString(),
                 QStringLiteral("admin"));

        provider.queryOrders(QStringLiteral("TOKEN-1"));
        QTRY_COMPARE(orders.count(), 1);
        QCOMPARE(server.requests.size(), 2);
        QCOMPARE(server.requests.last().value(QStringLiteral("action")).toString(),
                 QStringLiteral("ADMIN_ORDERS"));
        const QJsonObject payload = server.requests.last()
                                        .value(QStringLiteral("payload")).toObject();
        QCOMPARE(payload.value(QStringLiteral("adminToken")).toString(),
                 QStringLiteral("TOKEN-1"));
        QCOMPARE(payload.value(QStringLiteral("limit")).toInt(), 100);
        QCOMPARE(payload.value(QStringLiteral("offset")).toInt(), 0);
        const AdminOrderQueryResult result = ordersAt(orders);
        QCOMPARE(result.kind, AdminResultKind::Success);
        QCOMPARE(result.orders.size(), 1);
        QCOMPARE(result.orders.first().orderId, QStringLiteral("ORD-SHARED-1"));
        QCOMPARE(result.orders.first().userId, qint64(9876543210123LL));
        QCOMPARE(result.orders.first().pileId, qint64(1234567890123LL));
        QCOMPARE(result.orders.first().amountYuan, QStringLiteral("10.27"));
        QVERIFY(result.orders.first().endedAt.isEmpty());

        provider.queryLogs(QStringLiteral("TOKEN-1"), QStringLiteral("PAYMENT"));
        QTRY_COMPARE(logs.count(), 1);
        QCOMPARE(server.requests.size(), 3);
        QCOMPARE(server.requests.last().value(QStringLiteral("action")).toString(),
                 QStringLiteral("ADMIN_LOGS"));
        const QJsonObject logPayload = server.requests.last()
                                           .value(QStringLiteral("payload")).toObject();
        QCOMPARE(logPayload.value(QStringLiteral("adminToken")).toString(),
                 QStringLiteral("TOKEN-1"));
        QCOMPARE(logPayload.value(QStringLiteral("action")).toString(),
                 QStringLiteral("PAYMENT"));
        const AdminLogQueryResult logResult =
            qvariant_cast<AdminLogQueryResult>(logs.constFirst().at(0));
        QCOMPARE(logResult.kind, AdminResultKind::Success);
        QCOMPARE(logResult.logs.size(), 1);
        QCOMPARE(logResult.logs.first().logId, qint64(1234567890123LL));
        QCOMPARE(logResult.logs.first().targetId, QStringLiteral("ORD-SHARED-1"));
    }

    void serverErrorAndMalformedRows()
    {
        FakeTcpServer failureServer;
        QVERIFY(failureServer.listen());
        failureServer.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            socket->write(FakeTcpServer::frame({
                {QStringLiteral("requestId"), request.value(QStringLiteral("requestId"))},
                {QStringLiteral("ok"), false},
                {QStringLiteral("code"), QStringLiteral("AUTH_INVALID")},
                {QStringLiteral("message"), QStringLiteral("outer")},
                {QStringLiteral("data"), QJsonObject{}},
                {QStringLiteral("error"), QJsonObject{
                    {QStringLiteral("code"), QStringLiteral("AUTH_INVALID")},
                    {QStringLiteral("message"), QStringLiteral("令牌无效")}}}
            }));
        };
        RemoteAdminProvider failure(QStringLiteral("127.0.0.1"), failureServer.port());
        QSignalSpy failures(&failure, &IAdminProvider::ordersFinished);
        failure.queryOrders(QStringLiteral("bad"));
        QTRY_COMPARE(failures.count(), 1);
        QCOMPARE(ordersAt(failures).kind, AdminResultKind::Error);
        QCOMPARE(ordersAt(failures).message, QStringLiteral("令牌无效"));

        FakeTcpServer malformedServer;
        QVERIFY(malformedServer.listen());
        malformedServer.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            QJsonObject malformed = orderRow();
            malformed.insert(QStringLiteral("amountYuan"), 10.27);
            socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                request.value(QStringLiteral("requestId")).toString(),
                {{QStringLiteral("rows"), QJsonArray{malformed}}})));
        };
        RemoteAdminProvider malformed(QStringLiteral("127.0.0.1"), malformedServer.port());
        QSignalSpy malformedResults(&malformed, &IAdminProvider::ordersFinished);
        malformed.queryOrders(QStringLiteral("token"));
        QTRY_COMPARE(malformedResults.count(), 1);
        QCOMPARE(ordersAt(malformedResults).kind, AdminResultKind::Error);
        QVERIFY(ordersAt(malformedResults).orders.isEmpty());
    }

    void formalQueriesAndWrites()
    {
        FakeTcpServer server; QVERIFY(server.listen());
        server.handler=[](QTcpSocket*socket,const QJsonObject&request){const QString a=request.value(QStringLiteral("action")).toString();QJsonObject d;
            if(a==QStringLiteral("ADMIN_OVERVIEW"))d={{"todayRevenueYuan","12.30"},{"todayServiceCount",2},{"utilizationRate",20.0},{"onlineRate",90.0},{"pendingAlarmCount",1}};
            else if(a==QStringLiteral("ADMIN_REVENUE"))d={{"totalRevenueYuan","99.00"}};
            else if(a==QStringLiteral("ADMIN_STATIONS"))d={{"rows",QJsonArray{QJsonObject{{"stationId",QJsonValue(qint64(7))},{"name","站点"},{"address","地址"},{"status","ACTIVE"},{"priceYuanPerKwh","1.23"},{"totalPiles",2},{"availablePiles",1}}}}};
            else if(a==QStringLiteral("ADMIN_PILES"))d={{"rows",QJsonArray{QJsonObject{{"pileId",QJsonValue(qint64(8))},{"stationId",QJsonValue(qint64(7))},{"serial","P-1"},{"chargeType","FAST"},{"ratedPowerKw",120.0},{"status","IDLE"},{"lastHeartbeatAt",QJsonValue::Null}}}}};
            else if(a==QStringLiteral("ADMIN_USERS"))d={{"rows",QJsonArray{QJsonObject{{"userId",QJsonValue(qint64(9))},{"phone","138****5678"},{"nickname","Alice"},{"balanceYuan","88.00"},{"status","NORMAL"},{"createdAt","2026-09-09T00:00:00Z"}}}}};
            else if(a==QStringLiteral("ADMIN_STATION_CREATE"))d={{"stationId",QJsonValue(qint64(10))}};
            socket->write(FakeTcpServer::frame(FakeTcpServer::response(request.value(QStringLiteral("requestId")).toString(),d)));};
        RemoteAdminProvider provider(QStringLiteral("127.0.0.1"),server.port());
        QSignalSpy overview(&provider,&IAdminProvider::overviewFinished), revenue(&provider,&IAdminProvider::revenueFinished), stations(&provider,&IAdminProvider::stationsFinished), piles(&provider,&IAdminProvider::pilesFinished), users(&provider,&IAdminProvider::usersFinished), creates(&provider,&IAdminProvider::stationCreated), freezes(&provider,&IAdminProvider::userFreezeFinished);
        provider.queryOverview("TOKEN");QTRY_COMPARE(overview.count(),1);provider.queryRevenue("TOKEN");QTRY_COMPARE(revenue.count(),1);provider.queryStations("TOKEN");QTRY_COMPARE(stations.count(),1);provider.queryPiles("TOKEN");QTRY_COMPARE(piles.count(),1);provider.queryUsers("TOKEN");QTRY_COMPARE(users.count(),1);
        AdminNewStation station{QStringLiteral("新站"),QStringLiteral("新地址"),23.0,113.0,QStringLiteral("1.23"),{{QStringLiteral("PX"),QStringLiteral("FAST"),120.0}}};provider.createStation("TOKEN",station);QTRY_COMPARE(creates.count(),1);QCOMPARE(qvariant_cast<AdminActionResult>(creates.at(0).at(0)).stationId,qint64(10));provider.setUserFrozen("TOKEN",qint64(9),true,QStringLiteral("审核"));QTRY_COMPARE(freezes.count(),1);
        QCOMPARE(server.requests.at(5).value("action").toString(),QStringLiteral("ADMIN_STATION_CREATE"));QCOMPARE(server.requests.at(5).value("payload").toObject().value("piles").toArray().size(),1);const auto freeze=server.requests.at(6).value("payload").toObject();QCOMPARE(freeze.value("userId").toInteger(),qint64(9));QCOMPARE(freeze.value("frozen").toBool(),true);QCOMPARE(freeze.value("reason").toString(),QStringLiteral("审核"));
    }

    void ordersAndLogsKeepStaleRowsAndRefreshOnReconnect()
    {
        FakeAdminProvider provider; AdminPreview preview{QStringLiteral("页"),{},{},{},Ui::PageState::Normal};
        OrdersPage orders(preview,&provider,QStringLiteral("TOKEN"));orders.show();orders.refresh();provider.complete({AdminResultKind::Success,{},{{QStringLiteral("ORD-1"),1,2,QStringLiteral("NEW"),QStringLiteral("0.00"),QStringLiteral("0.00"),{}, {}}}});QCOMPARE(orders.findChild<QTableWidget*>("adminTable")->rowCount(),1);provider.disconnectTransport();QCOMPARE(orders.stateWidget()->state(),Ui::PageState::Offline);QCOMPARE(orders.findChild<QTableWidget*>("adminTable")->rowCount(),1);const int before=provider.queryCount;provider.reconnectTransport();QCOMPARE(provider.queryCount,before+1);
        orders.hide(); LogsPage logs(preview,&provider,QStringLiteral("TOKEN"));logs.show();logs.refresh();provider.completeLogs({AdminResultKind::Success,{},{{1,QStringLiteral("ADMIN"),QStringLiteral("a"),QStringLiteral("X"),QStringLiteral("ORDER"),QStringLiteral("1"),QStringLiteral("OK"),QStringLiteral("now")}}});QCOMPARE(logs.findChild<QTableWidget*>("adminLogsTable")->rowCount(),1);provider.disconnectTransport();QCOMPARE(logs.stateWidget()->state(),Ui::PageState::Offline);const int logBefore=provider.queryCount;provider.reconnectTransport();QCOMPARE(provider.queryCount,logBefore+1);
    }

    void managementActionsSendContractPayloads()
    {
        FakeTcpServer server; QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                request.value("requestId").toString(),
                {{"stationId", qint64(7)}, {"pileId", qint64(8)}})));
        };
        RemoteAdminProvider provider(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy station(&provider, &IAdminProvider::stationUpdated);
        QSignalSpy pile(&provider, &IAdminProvider::pileStatusFinished);
        QSignalSpy restart(&provider, &IAdminProvider::pileRestartFinished);
        QSignalSpy ack(&provider, &IAdminProvider::alarmAcknowledged);
        QSignalSpy ticket(&provider, &IAdminProvider::ticketActionFinished);
        provider.updateStation("TOKEN", 7, {"新站", "广州", 23.1, 113.3, "1.23", {}}, "OPEN"); QTRY_COMPARE(station.count(), 1);
        provider.setPileStatus("TOKEN", 8, "OFFLINE", "维护"); QTRY_COMPARE(pile.count(), 1);
        provider.restartPile("TOKEN", 8); QTRY_COMPARE(restart.count(), 1);
        provider.acknowledgeAlarm("TOKEN", 9); QTRY_COMPARE(ack.count(), 1);
        provider.startTicket("TOKEN", 10, "operator"); QTRY_COMPARE(ticket.count(), 1);
        provider.resolveTicket("TOKEN", 10, "done"); QTRY_COMPARE(ticket.count(), 2);
        provider.closeTicket("TOKEN", 10); QTRY_COMPARE(ticket.count(), 3);
        QCOMPARE(server.requests.at(0).value("action").toString(), QStringLiteral("ADMIN_STATION_UPDATE"));
        QCOMPARE(server.requests.at(1).value("payload").toObject().value("status").toString(), QStringLiteral("OFFLINE"));
        QCOMPARE(server.requests.at(3).value("payload").toObject().value("alarmId").toInteger(), qint64(9));
        QCOMPARE(server.requests.at(4).value("payload").toObject().value("ticketId").toInteger(), qint64(10));
    }

    void offlineAndTimeout()
    {
        QTcpServer reservation;
        QVERIFY(reservation.listen(QHostAddress::LocalHost, 0));
        const quint16 port = reservation.serverPort();
        reservation.close();
        RemoteAdminProvider offline(QStringLiteral("127.0.0.1"), port);
        QSignalSpy offlineResults(&offline, &IAdminProvider::loginFinished);
        offline.login(QStringLiteral("admin"), QStringLiteral("123456"));
        QTRY_COMPARE(offlineResults.count(), 1);
        QCOMPARE(loginAt(offlineResults).kind, AdminResultKind::Offline);

        FakeTcpServer server;
        QVERIFY(server.listen());
        ServiceClient client(QStringLiteral("127.0.0.1"), server.port());
        client.setRequestTimeout(150);
        RemoteAdminProvider timeout(&client);
        QSignalSpy timeoutResults(&timeout, &IAdminProvider::ordersFinished);
        timeout.queryOrders(QStringLiteral("token"));
        QTRY_COMPARE(timeoutResults.count(), 1);
        QCOMPARE(ordersAt(timeoutResults).kind, AdminResultKind::Error);
    }

    void ordersPageDisplaysServerOrderId()
    {
        FakeAdminProvider provider;
        AdminPreview preview{QStringLiteral("充电订单"), {}, {}, {},
                             Ui::PageState::Normal};
        OrdersPage page(preview, &provider, QStringLiteral("TOKEN-UI"));
        page.refresh();
        QTRY_COMPARE(provider.queryCount, 1);
        QCOMPARE(provider.lastToken, QStringLiteral("TOKEN-UI"));
        provider.complete({AdminResultKind::Success, {}, {
            {QStringLiteral("ORD-SHARED-1"), qint64(42), qint64(7),
             QStringLiteral("FINISHED"), QStringLiteral("8.35"),
             QStringLiteral("10.27"), QStringLiteral("2026-09-08T10:00:00Z"), {}}
        }});
        auto *table = page.findChild<QTableWidget *>(QStringLiteral("adminTable"));
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("ORD-SHARED-1"));
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("42"));
        QCOMPARE(table->item(0, 2)->text(), QStringLiteral("7"));
        QCOMPARE(table->item(0, 4)->text(), QStringLiteral("10.27"));
    }

    void revenueTrendContractAndPageSwitch()
    {
        FakeTcpServer server; QVERIFY(server.listen());
        server.handler=[](QTcpSocket *socket,const QJsonObject &request){
            socket->write(FakeTcpServer::frame(FakeTcpServer::response(request.value("requestId").toString(),
                {{"points",QJsonArray{QJsonObject{{"date","2026-09-01"},{"revenueYuan","1.20"}}}}}))); };
        RemoteAdminProvider provider(QStringLiteral("127.0.0.1"),server.port());
        QSignalSpy trend(&provider,&IAdminProvider::revenueTrendFinished);
        provider.queryRevenueTrend("TOKEN",7); QTRY_COMPARE(trend.count(),1);
        QCOMPARE(server.requests.first().value("action").toString(),QStringLiteral("ADMIN_REVENUE_TREND"));
        QCOMPARE(server.requests.first().value("payload").toObject().value("days").toInt(),7);
        QCOMPARE(qvariant_cast<AdminRevenueTrendResult>(trend.first().at(0)).points.size(),1);
        FakeAdminProvider fake;
        RevenuePage page({QStringLiteral("营收"),{}, {}, {}, Ui::PageState::Normal},&fake,"TOKEN");
        page.refresh(); QCOMPARE(fake.revenueQueries,1);
        fake.completeRevenue({AdminResultKind::Success,{},QStringLiteral("100.00")}); QCOMPARE(fake.overviewQueries,1);
        // S1-B：默认区间跟随候选栏首项（近7日=7），不再固定 30 日。
        fake.completeOverview({AdminResultKind::Success,{},QStringLiteral("5.00"),0,0.0,0.0,0}); QCOMPARE(fake.trendDays,7);
        QList<AdminRevenuePoint> month;
        for(int i=0;i<30;++i)month.append({QStringLiteral("2026-09-%1").arg(i+1,2,10,QChar('0')),QStringLiteral("1.00")});
        fake.completeTrend({AdminResultKind::Success,{},month}); QCOMPARE(fake.trendDays,7);
        fake.completeTrend({AdminResultKind::Success,{},month.mid(23)});
        auto *range=page.findChild<QComboBox*>(QStringLiteral("revenueTrendRange")); QVERIFY(range); QVERIFY(range->isEnabled());
#ifdef ADMIN_HAS_CHARTS
        auto *view=page.findChild<QChartView*>(QStringLiteral("revenueTrendChart")); QVERIFY(view);
        const auto axes=view->chart()->axes(Qt::Horizontal); QCOMPARE(axes.size(),1);
        auto *dateAxis=qobject_cast<QDateTimeAxis*>(axes.first()); QVERIFY(dateAxis);
        QCOMPARE(dateAxis->format(),QStringLiteral("MM-dd")); QCOMPARE(dateAxis->tickCount(),7);
        QCOMPARE(view->chart()->title(),QStringLiteral("近7日营收趋势"));
#endif
        range->setCurrentIndex(1); QCOMPARE(fake.trendDays,30);
    }

    // S1-A 联动契约：查询响应带 snapshotVersion，旧快照（版本号更小）到达时不得覆盖新数据。
    void overviewDropsStaleSnapshot()
    {
        FakeAdminProvider provider;
        AdminPreview preview{QStringLiteral("总览"), {}, {}, {}, Ui::PageState::Normal};
        OverviewPage page(preview, &provider, QStringLiteral("TOKEN"));
        page.show();
        page.refresh();
        QCOMPARE(provider.overviewQueries, 1);

        AdminOverviewResult fresh;
        fresh.kind = AdminResultKind::Success;
        fresh.todayRevenueYuan = QStringLiteral("100.00");
        fresh.todayServiceCount = 10;
        fresh.snapshotVersion = 10;
        fresh.serverTimeUtc = QStringLiteral("2026-09-14T10:00:00Z");
        provider.completeOverview(fresh);
        auto *todayCard = page.findChild<Ui::MetricCard *>(QStringLiteral("overviewTodayRevenue"));
        QVERIFY(todayCard);
        QCOMPARE(todayCard->value(), QStringLiteral("100.00"));

        // 旧快照（版本 5 < 10）：丢弃并进入 Stale，指标卡保持版本 10 的值。
        AdminOverviewResult stale;
        stale.kind = AdminResultKind::Success;
        stale.todayRevenueYuan = QStringLiteral("999.99");
        stale.todayServiceCount = 999;
        stale.snapshotVersion = 5;
        stale.serverTimeUtc = QStringLiteral("2026-09-14T09:00:00Z");
        provider.completeOverview(stale);
        QCOMPARE(page.stateWidget()->state(), Ui::PageState::Stale);
        QCOMPARE(todayCard->value(), QStringLiteral("100.00"));

        // 更新的快照（版本 11）：正常接受并覆盖。
        AdminOverviewResult newer = fresh;
        newer.todayRevenueYuan = QStringLiteral("123.45");
        newer.snapshotVersion = 11;
        provider.completeOverview(newer);
        QCOMPARE(todayCard->value(), QStringLiteral("123.45"));
        QCOMPARE(page.stateWidget()->state(), Ui::PageState::Normal);
    }

    void pileStatusAggregation()
    {
        FakeAdminProvider provider;
        PileDevicesPage page({QStringLiteral("电桩"),{}, {}, {}, Ui::PageState::Normal},&provider,"TOKEN");
        provider.completePiles({AdminResultKind::Success,{},{{1,1,"a","FAST",60.0,"IDLE",{}},{2,1,"b","FAST",60.0,"CHARGING",{}},{3,1,"c","FAST",60.0,"RESERVED",{}},{4,1,"d","FAST",60.0,"FAULT",{}},{5,1,"e","FAST",60.0,"OFFLINE",{}}}});
        QLabel *summary=nullptr;
        for (auto *label : page.findChildren<QLabel*>()) if (label->text().contains(QStringLiteral("状态统计"))) summary=label;
        QVERIFY(summary);
        QVERIFY(summary->text().contains(QStringLiteral("在用 2 (40.0%)")));
        QVERIFY(summary->text().contains(QStringLiteral("离线 1 (20.0%)")));
    }
};

QTEST_MAIN(RemoteAdminProviderTest)
#include "remote_admin_provider_test.moc"
