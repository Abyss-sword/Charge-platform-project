#include "server/services/station_service.h"

#include <QSqlError>
#include <QSqlQuery>

#include <algorithm>
#include <stdexcept>

StationService::StationService(QSqlDatabase database)
    : database_(std::move(database))
{
}

// 附近站点查询（公共算法 Haversine 的业务落点）：
// 1) 先校验入参坐标（即使库里没有站点也要先抛出非法坐标，保证行为一致）；
// 2) 一条 SQL 只查营业中（OPEN）站点并聚合总桩数/空闲（IDLE）桩数；
// 3) 逐站计算 Haversine 距离；4) 按距离升序、同距按站点 ID 排序。
QVector<StationSummary> StationService::nearbyStations(const GeoPoint &origin) const
{
    // Validate even when the database contains no station.
    (void)haversineKm(origin, origin);

    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT s.id,s.name,s.address,s.latitude,s.longitude,s.price_yuan_per_kwh,"
            "COUNT(p.id),COALESCE(SUM(CASE WHEN p.status='IDLE' THEN 1 ELSE 0 END),0) "
            "FROM station_info s LEFT JOIN pile_info p ON p.station_id=s.id "
            "WHERE s.status='OPEN' GROUP BY s.id,s.name,s.address,s.latitude,s.longitude,"
            "s.price_yuan_per_kwh"))) {
        throw std::runtime_error(query.lastError().text().toStdString());
    }

    QVector<StationSummary> stations;
    while (query.next()) {
        StationSummary station;
        station.stationId = query.value(0).toLongLong();
        station.name = query.value(1).toString();
        station.address = query.value(2).toString();
        station.latitude = query.value(3).toDouble();
        station.longitude = query.value(4).toDouble();
        const auto price = DecimalValue::parseCanonical(query.value(5).toString());
        if (!price) {
            throw std::runtime_error("invalid price_yuan_per_kwh in database");
        }
        station.priceYuanPerKwh = *price;
        station.totalPiles = query.value(6).toInt();
        station.availablePiles = query.value(7).toInt();
        station.distanceKm = haversineKm(origin, {station.latitude, station.longitude});
        stations.append(station);
    }
    // 排序：距离升序为主，距离相同按站点 ID 升序，保证输出顺序确定可复现。
    std::sort(stations.begin(), stations.end(), [](const auto &left, const auto &right) {
        if (left.distanceKm == right.distanceKm) {
            return left.stationId < right.stationId;
        }
        return left.distanceKm < right.distanceKm;
    });
    return stations;
}

// 站点详情：站点基础信息 + 按 ID 排序的电桩列表；两段查询失败均通过 error 返回错误码。
std::optional<StationDetail> StationService::stationDetail(qint64 stationId,
                                                            QString *error) const
{
    if (stationId <= 0) {
        if (error) *error = QStringLiteral("PARAM_INVALID");
        return std::nullopt;
    }
    QSqlQuery stationQuery(database_);
    stationQuery.prepare(QStringLiteral(
        "SELECT id,name,address,latitude,longitude,price_yuan_per_kwh,status "
        "FROM station_info WHERE id=?"));
    stationQuery.addBindValue(stationId);
    if (!stationQuery.exec()) {
        if (error) *error = QStringLiteral("DATABASE_ERROR");
        return std::nullopt;
    }
    if (!stationQuery.next()) {
        if (error) *error = QStringLiteral("NOT_FOUND");
        return std::nullopt;
    }
    const auto price = DecimalValue::parseCanonical(stationQuery.value(5).toString());
    if (!price) {
        if (error) *error = QStringLiteral("DATABASE_ERROR");
        return std::nullopt;
    }
    StationDetail result{stationQuery.value(0).toLongLong(),
                         stationQuery.value(1).toString(),
                         stationQuery.value(2).toString(),
                         stationQuery.value(3).toDouble(),
                         stationQuery.value(4).toDouble(), *price,
                         stationQuery.value(6).toString(), {}};
    QSqlQuery pileQuery(database_);
    pileQuery.prepare(QStringLiteral(
        "SELECT id,pile_sn,charge_type,rated_power_kw,status,last_heartbeat_at "
        "FROM pile_info WHERE station_id=? ORDER BY id"));
    pileQuery.addBindValue(stationId);
    if (!pileQuery.exec()) {
        if (error) *error = QStringLiteral("DATABASE_ERROR");
        return std::nullopt;
    }
    while (pileQuery.next()) {
        result.piles.append({pileQuery.value(0).toLongLong(),
                             pileQuery.value(1).toString(),
                             pileQuery.value(2).toString(),
                             pileQuery.value(3).toDouble(),
                             pileQuery.value(4).toString(),
                             pileQuery.value(5).toString()});
    }
    if (error) error->clear();
    return result;
}
