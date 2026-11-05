#include "shared/domain/geo.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
// 坐标合法性：必须是有限数，纬度 [-90,90]、经度 [-180,180]；非法直接抛异常拒绝计算。
void validate(const GeoPoint &point)
{
    if (!std::isfinite(point.latitude) || !std::isfinite(point.longitude)
        || point.latitude < -90.0 || point.latitude > 90.0
        || point.longitude < -180.0 || point.longitude > 180.0) {
        throw std::invalid_argument("coordinate outside earth bounds");
    }
}
}

// Haversine 球面距离（公共算法核心）：
// a = sin²(Δφ/2) + cosφ1·cosφ2·sin²(Δλ/2)，d = R·2·atan2(√a, √(1−a))。
// 地球半径取 6371.0088 km；a 夹在 [0,1] 防浮点误差导致 sqrt 负数。
// 复杂度：单次 O(1)；站点列表整体约 O(N) 计算 + O(N log N) 排序，未用空间索引。
double haversineKm(const GeoPoint &from, const GeoPoint &to)
{
    validate(from);
    validate(to);

    // IUGG 平均地球半径（公里）。
    constexpr double EarthRadiusKm = 6371.0088;
    constexpr double DegreesToRadians = 3.14159265358979323846 / 180.0;
    const double lat1 = from.latitude * DegreesToRadians;
    const double lat2 = to.latitude * DegreesToRadians;
    const double deltaLat = (to.latitude - from.latitude) * DegreesToRadians;
    const double deltaLon = (to.longitude - from.longitude) * DegreesToRadians;
    const double sinLat = std::sin(deltaLat / 2.0);
    const double sinLon = std::sin(deltaLon / 2.0);
    const double a = std::clamp(
        sinLat * sinLat + std::cos(lat1) * std::cos(lat2) * sinLon * sinLon,
        0.0, 1.0);
    // 结果即两点在大圆上的弧长（公里）。
    return EarthRadiusKm * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}
