// 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
// 站点服务单测：验证站点查询、距离排序、空闲桩数统计——证明附近站点不是写死排序。

#include <QtTest>

#include "server/services/station_service.h"
#include "tests/helpers/test_database.h"

class StationServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void equalDistancesAreOrderedByStationId()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 firstId = testDb.addStation(QStringLiteral("先建站"), 23.1291, 113.2644);
        const qint64 secondId = testDb.addStation(QStringLiteral("后建站"), 23.1291, 113.2644);
        QVERIFY(firstId > 0);
        QVERIFY(secondId > firstId);
        StationService service(testDb.database());

        const auto stations = service.nearbyStations({23.1200, 113.2500});

        QCOMPARE(stations.size(), 2);
        QCOMPARE(stations.at(0).stationId, firstId);
        QCOMPARE(stations.at(1).stationId, secondId);
        QCOMPARE(stations.at(0).distanceKm, stations.at(1).distanceKm);
    }

    void invalidOriginIsRejected()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        StationService service(testDb.database());
        QVERIFY_EXCEPTION_THROWN(service.nearbyStations({91.0, 0.0}), std::invalid_argument);
    }

    void excludesClosedStationsAndCountsIdlePiles()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 openId = testDb.addStation(QStringLiteral("开放站"));
        const qint64 closedId = testDb.addStation(QStringLiteral("关闭站"));
        QVERIFY(testDb.exec(QStringLiteral("UPDATE station_info SET status='CLOSED' WHERE id=?"),
                            {closedId}));
        QVERIFY(testDb.addPile(openId, QStringLiteral("PILE-IDLE"), QStringLiteral("IDLE")) > 0);
        QVERIFY(testDb.addPile(openId, QStringLiteral("PILE-FAULT"), QStringLiteral("FAULT")) > 0);
        QVERIFY(testDb.addPile(closedId, QStringLiteral("PILE-CLOSED"), QStringLiteral("IDLE")) > 0);
        StationService service(testDb.database());

        const auto stations = service.nearbyStations({23.12, 113.25});

        QCOMPARE(stations.size(), 1);
        QCOMPARE(stations.first().stationId, openId);
        QCOMPARE(stations.first().totalPiles, 2);
        QCOMPARE(stations.first().availablePiles, 1);
    }
};

QTEST_GUILESS_MAIN(StationServiceTest)
#include "station_service_test.moc"
