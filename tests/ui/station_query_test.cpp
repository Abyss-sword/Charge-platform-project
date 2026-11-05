#include <QtTest>

#include "adapters/remote_station_provider.h"
#include "adapters/station_provider.h"
#include "fake_tcp_server.h"
#include "pages/station_list_page.h"
#include "widgets/page_state_widget.h"

#include <QFrame>
#include <QJsonArray>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>

namespace {

QJsonObject stationJson()
{
    return {
        {QStringLiteral("stationId"), QJsonValue(qint64(9876543210123LL))},
        {QStringLiteral("name"), QStringLiteral("测试站")},
        {QStringLiteral("address"), QStringLiteral("测试路1号")},
        {QStringLiteral("latitude"), 23.1351},
        {QStringLiteral("longitude"), 113.3245},
        {QStringLiteral("priceYuanPerKwh"), QStringLiteral("1.23")},
        {QStringLiteral("totalPiles"), 4},
        {QStringLiteral("availablePiles"), 3},
        {QStringLiteral("distanceKm"), 0.6}
    };
}

StationQueryResult resultAt(const QSignalSpy &spy)
{
    return qvariant_cast<StationQueryResult>(spy.constFirst().at(0));
}

class FakeStationProvider final : public IStationProvider
{
public:
    using IStationProvider::IStationProvider;

    void queryNearby(const StationQuery &query) override
    {
        ++queryCount;
        lastQuery = query;
    }

    void complete(const StationQueryResult &result) { emit queryFinished(result); }
    void disconnectTransport() { emit connectivityLost(); }
    void reconnectTransport() { emit connectivityRestored(); }

    int queryCount = 0;
    StationQuery lastQuery;
};

PagePreview stationPreview()
{
    return {QStringLiteral("附近站点"),
            QStringLiteral("查询附近可用充电站"),
            {},
            {},
            Ui::PageState::Normal};
}

} // namespace

class StationQueryTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { qRegisterMetaType<StationQueryResult>(); }

    void requestEnvelopeAndSuccessMapping()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                request.value(QStringLiteral("requestId")).toString(),
                {{QStringLiteral("stations"), QJsonArray{stationJson()}}})));
        };
        RemoteStationProvider provider(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy results(&provider, &IStationProvider::queryFinished);
        provider.queryNearby({23.1351, 113.3245});
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(server.requests.size(), 1);
        const QJsonObject request = server.requests.first();
        QCOMPARE(request.value(QStringLiteral("action")).toString(),
                 QStringLiteral("STATION_NEARBY"));
        QVERIFY(!request.value(QStringLiteral("requestId")).toString().isEmpty());
        const QJsonObject payload = request.value(QStringLiteral("payload")).toObject();
        QCOMPARE(payload.size(), 2);
        QCOMPARE(payload.value(QStringLiteral("latitude")).toDouble(), 23.1351);
        QCOMPARE(payload.value(QStringLiteral("longitude")).toDouble(), 113.3245);

        const StationQueryResult result = resultAt(results);
        QCOMPARE(result.kind, StationQueryResultKind::Success);
        QCOMPARE(result.stations.size(), 1);
        QCOMPARE(result.stations.first().stationId, qint64(9876543210123LL));
        QCOMPARE(result.stations.first().priceYuanPerKwh, QStringLiteral("1.23"));
        QCOMPARE(result.stations.first().availablePiles, 3);
    }

    void emptyResponse()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                request.value(QStringLiteral("requestId")).toString(),
                {{QStringLiteral("stations"), QJsonArray{}}})));
        };
        RemoteStationProvider provider(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy results(&provider, &IStationProvider::queryFinished);
        provider.queryNearby({23.1351, 113.3245});
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(resultAt(results).kind, StationQueryResultKind::Success);
        QVERIFY(resultAt(results).stations.isEmpty());
    }

    void detailAllowsNullHeartbeat()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            const QJsonObject pile{{QStringLiteral("pileId"), 1},
                                   {QStringLiteral("serial"), QStringLiteral("P-001")},
                                   {QStringLiteral("chargeType"), QStringLiteral("FAST")},
                                   {QStringLiteral("ratedPowerKw"), 60.0},
                                   {QStringLiteral("status"), QStringLiteral("AVAILABLE")},
                                   {QStringLiteral("lastHeartbeatAt"), QJsonValue::Null}};
            const QJsonObject detail{{QStringLiteral("stationId"), 1},
                                     {QStringLiteral("name"), QStringLiteral("测试站")},
                                     {QStringLiteral("address"), QStringLiteral("测试路")},
                                     {QStringLiteral("latitude"), 23.1},
                                     {QStringLiteral("longitude"), 113.3},
                                     {QStringLiteral("priceYuanPerKwh"), QStringLiteral("1.23")},
                                     {QStringLiteral("status"), QStringLiteral("OPEN")},
                                     {QStringLiteral("piles"), QJsonArray{pile}}};
            socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                request.value(QStringLiteral("requestId")).toString(), detail)));
        };
        RemoteStationProvider provider(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy results(&provider, &IStationProvider::detailFinished);
        provider.queryDetail(1);
        QTRY_COMPARE(results.count(), 1);
        const auto result = qvariant_cast<StationDetailResult>(results.constFirst().at(0));
        QCOMPARE(result.kind, StationQueryResultKind::Success);
        QCOMPARE(result.detail.piles.size(), 1);
        QVERIFY(result.detail.piles.first().lastHeartbeatAt.isEmpty());
    }

    void serverError()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            socket->write(FakeTcpServer::frame({
                {QStringLiteral("requestId"), request.value(QStringLiteral("requestId"))},
                {QStringLiteral("ok"), false},
                {QStringLiteral("code"), QStringLiteral("DATABASE_ERROR")},
                {QStringLiteral("message"), QStringLiteral("outer")},
                {QStringLiteral("data"), QJsonObject{}},
                {QStringLiteral("error"), QJsonObject{
                    {QStringLiteral("code"), QStringLiteral("DATABASE_ERROR")},
                    {QStringLiteral("message"), QStringLiteral("inner")}
                }}
            }));
        };
        RemoteStationProvider provider(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy results(&provider, &IStationProvider::queryFinished);
        provider.queryNearby({23.1351, 113.3245});
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(resultAt(results).kind, StationQueryResultKind::Error);
        QCOMPARE(resultAt(results).message, QStringLiteral("inner"));
    }

    void malformedStationData()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            QJsonObject malformed = stationJson();
            malformed.insert(QStringLiteral("stationId"), QStringLiteral("1"));
            socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                request.value(QStringLiteral("requestId")).toString(),
                {{QStringLiteral("stations"), QJsonArray{malformed}}})));
        };
        RemoteStationProvider provider(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy results(&provider, &IStationProvider::queryFinished);
        provider.queryNearby({23.1351, 113.3245});
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(resultAt(results).kind, StationQueryResultKind::Error);
        QVERIFY(resultAt(results).stations.isEmpty());
    }

    void offline()
    {
        QTcpServer reservation;
        QVERIFY(reservation.listen(QHostAddress::LocalHost, 0));
        const quint16 port = reservation.serverPort();
        reservation.close();
        RemoteStationProvider provider(QStringLiteral("127.0.0.1"), port);
        QSignalSpy results(&provider, &IStationProvider::queryFinished);
        provider.queryNearby({23.1351, 113.3245});
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(resultAt(results).kind, StationQueryResultKind::Offline);
    }

    void timeout()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        ServiceClient client(QStringLiteral("127.0.0.1"), server.port());
        client.setRequestTimeout(150);
        RemoteStationProvider provider(&client);
        QSignalSpy results(&provider, &IStationProvider::queryFinished);
        provider.queryNearby({23.1351, 113.3245});
        QTRY_COMPARE(server.requests.size(), 1);
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(resultAt(results).kind, StationQueryResultKind::Error);
    }

    void pageStatesAndLayout()
    {
        FakeStationProvider provider;
        NearbyStationsPage page(stationPreview(), &provider);
        page.resize(360, 780);
        page.show();
        QTRY_COMPARE(provider.queryCount, 1);
        QCOMPARE(provider.lastQuery.latitude, 23.1351);
        QCOMPARE(provider.lastQuery.longitude, 113.3245);

        const StationSummary station{1, QStringLiteral("测试站"),
            QStringLiteral("测试路1号"), 23.1351, 113.3245,
            QStringLiteral("1.23"), 4, 3, 0.6};
        provider.complete({StationQueryResultKind::Success, {}, {station}, {}});
        QCOMPARE(page.stateWidget()->state(), Ui::PageState::Normal);
        QVERIFY(page.findChild<QLabel *>(QStringLiteral("stateMessage"))
                    ->text().contains(QStringLiteral("服务端站点数据")));
        QCOMPARE(page.findChildren<QFrame *>(QStringLiteral("stationCard")).size(), 1);
        QCOMPARE(page.findChild<QScrollArea *>(QStringLiteral("pageScroll"))
                     ->horizontalScrollBar()->maximum(), 0);

        page.refresh();
        provider.complete({StationQueryResultKind::Success, {}, {}, {}});
        QCOMPARE(page.stateWidget()->state(), Ui::PageState::Empty);
        page.refresh();
        provider.complete({StationQueryResultKind::Error, QStringLiteral("失败"), {}, {}});
        QCOMPARE(page.stateWidget()->state(), Ui::PageState::Error);
        page.refresh();
        provider.complete({StationQueryResultKind::Offline, QStringLiteral("离线"), {}, {}});
        QCOMPARE(page.stateWidget()->state(), Ui::PageState::Offline);
    }

    void staleDataSurvivesAndReconnectRefreshesVisiblePage()
    {
        FakeStationProvider provider;
        NearbyStationsPage page(stationPreview(), &provider);
        page.show();
        QTRY_COMPARE(provider.queryCount,1);
        provider.complete({StationQueryResultKind::Success,{},{{1,QStringLiteral("旧站点"),QStringLiteral("旧地址"),23.0,113.0,QStringLiteral("1.00"),2,1,0.2}}, {}});
        QCOMPARE(page.findChildren<QFrame *>(QStringLiteral("stationCard")).size(),1);
        provider.disconnectTransport();
        QCOMPARE(page.stateWidget()->state(),Ui::PageState::Offline);
        QCOMPARE(page.findChildren<QFrame *>(QStringLiteral("stationCard")).size(),1);
        provider.reconnectTransport();
        QCOMPARE(provider.queryCount,2);
        QCOMPARE(page.stateWidget()->state(),Ui::PageState::Loading);
    }
};

QTEST_MAIN(StationQueryTest)
#include "station_query_test.moc"
