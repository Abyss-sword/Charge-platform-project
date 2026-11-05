#pragma once

// GeoPoint：经纬度点（度）；haversineKm：两点半球面大圆距离（公里）——公共算法之二。
struct GeoPoint
{
    double latitude = 0.0;
    double longitude = 0.0;
};

// 球面距离而非导航里程：不涉及地图服务或路网路径规划（答辩口径）。
double haversineKm(const GeoPoint &from, const GeoPoint &to);
