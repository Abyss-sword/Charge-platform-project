// 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
// 地理距离单测：验证 Haversine 计算结果与非法坐标拒绝——附近站点排序的算法基础。

#include <cmath>
#include <stdexcept>
#include <QtTest>

#include "shared/domain/geo.h"

class GeoTest final : public QObject
{
    Q_OBJECT

private slots:
    void computesKnownDistance()
    {
        const double km = haversineKm({23.1291, 113.2644}, {23.1300, 113.2700});
        QVERIFY(std::abs(km - 0.58) < 0.08);
    }

    void rejectsCoordinatesOutsideEarthBounds()
    {
        QVERIFY_EXCEPTION_THROWN(haversineKm({90.0001, 0.0}, {0.0, 0.0}),
                                 std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(haversineKm({0.0, -180.0001}, {0.0, 0.0}),
                                 std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(haversineKm({0.0, 0.0}, {-90.0001, 0.0}),
                                 std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(haversineKm({0.0, 0.0}, {0.0, 180.0001}),
                                 std::invalid_argument);
    }

    void acceptsBoundaryCoordinates()
    {
        QVERIFY(std::isfinite(haversineKm({90.0, 180.0}, {-90.0, -180.0})));
    }

    void nearAntipodalCoordinatesRemainFinite()
    {
        const double km = haversineKm({57.152844391518073, 0.0},
                                      {-57.152844391518073, 180.0});
        QVERIFY(std::isfinite(km));
        QVERIFY(km > 20000.0 && km < 20016.0);
    }
};

QTEST_GUILESS_MAIN(GeoTest)
#include "geo_test.moc"
