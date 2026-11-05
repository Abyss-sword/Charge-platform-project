#pragma once

#include "shared/domain/geo.h"
#include "shared/domain/decimal_value.h"

#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include <optional>

// 站点列表项：含 Haversine 球面距离（公里）与 SQL 聚合的总桩数/空闲桩数。
struct StationSummary
{
    qint64 stationId = 0;
    QString name;
    QString address;
    double latitude = 0.0;
    double longitude = 0.0;
    DecimalValue priceYuanPerKwh;
    int totalPiles = 0;
    int availablePiles = 0;
    double distanceKm = 0.0;
};

// 站点详情中的电桩摘要：状态/最近心跳直接来自 pile_info。
struct PileSummary
{
    qint64 pileId = 0;
    QString serial;
    QString chargeType;
    double ratedPowerKw = 0.0;
    QString status;
    QString lastHeartbeatAt;
};

struct StationDetail
{
    qint64 stationId = 0;
    QString name;
    QString address;
    double latitude = 0.0;
    double longitude = 0.0;
    DecimalValue priceYuanPerKwh;
    QString status;
    QVector<PileSummary> piles;
};

// StationService：站点查询业务（只读，不写库）。
// 附近站点按球面距离升序（同距按站点 ID），输出的是地理距离而非驾车路线里程；
// 单价以 DecimalValue 规范文本解析，库中格式异常直接报错而非静默继续。
class StationService
{
public:
    explicit StationService(QSqlDatabase database);

    // 附近营业站点：SQL 聚合空闲桩数 + Haversine 距离 + std::sort 排序（约 O(N log N)）。
    QVector<StationSummary> nearbyStations(const GeoPoint &origin) const;
    // 站点详情（含所属电桩列表）；失败经 error 返回 PARAM_INVALID/NOT_FOUND/DATABASE_ERROR。
    std::optional<StationDetail> stationDetail(qint64 stationId,
                                                QString *error = nullptr) const;

private:
    QSqlDatabase database_;
};
