#include "adapters/remote_station_provider.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

#include <cmath>
#include <limits>
#include <utility>

namespace {

StationQueryResult errorResult(const QString &message)
{
    return {StationQueryResultKind::Error, message, {}, QStringLiteral("CLIENT_ERROR")};
}

QString responseCode(const QJsonObject &response)
{
    const QString inner = response.value(QStringLiteral("error")).toObject()
                              .value(QStringLiteral("code")).toString();
    return inner.isEmpty() ? response.value(QStringLiteral("code")).toString() : inner;
}

bool readInteger(const QJsonValue &value, qint64 *result)
{
    if (!value.isDouble()) {
        return false;
    }
    constexpr qint64 invalid = std::numeric_limits<qint64>::min();
    const qint64 integer = value.toInteger(invalid);
    if ((integer == invalid && value != QJsonValue(invalid))
        || value != QJsonValue(integer)) {
        return false;
    }
    *result = integer;
    return true;
}

bool readFiniteNumber(const QJsonValue &value, double *result)
{
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number)) {
        return false;
    }
    *result = number;
    return true;
}

bool mapStation(const QJsonValue &value, StationSummary *result, QString *error)
{
    if (!value.isObject()) {
        *error = QStringLiteral("站点条目必须是对象。");
        return false;
    }
    const QJsonObject object = value.toObject();
    qint64 stationId = 0;
    qint64 totalPiles = 0;
    qint64 availablePiles = 0;
    double latitude = 0.0;
    double longitude = 0.0;
    double distanceKm = 0.0;
    static const QRegularExpression decimal(
        QStringLiteral("^(0|[1-9]\\d*)\\.\\d{2}$"));

    if (!readInteger(object.value(QStringLiteral("stationId")), &stationId)
        || stationId <= 0) {
        *error = QStringLiteral("stationId 必须是正整数。");
        return false;
    }
    if (!object.value(QStringLiteral("name")).isString()
        || !object.value(QStringLiteral("address")).isString()) {
        *error = QStringLiteral("站点名称或地址类型错误。");
        return false;
    }
    if (!readFiniteNumber(object.value(QStringLiteral("latitude")), &latitude)
        || latitude < -90.0 || latitude > 90.0
        || !readFiniteNumber(object.value(QStringLiteral("longitude")), &longitude)
        || longitude < -180.0 || longitude > 180.0) {
        *error = QStringLiteral("站点坐标无效。");
        return false;
    }
    const QJsonValue price = object.value(QStringLiteral("priceYuanPerKwh"));
    if (!price.isString() || !decimal.match(price.toString()).hasMatch()) {
        *error = QStringLiteral("priceYuanPerKwh 必须是两位十进制字符串。");
        return false;
    }
    if (!readInteger(object.value(QStringLiteral("totalPiles")), &totalPiles)
        || !readInteger(object.value(QStringLiteral("availablePiles")), &availablePiles)
        || totalPiles < 0 || totalPiles > std::numeric_limits<int>::max()
        || availablePiles < 0 || availablePiles > totalPiles) {
        *error = QStringLiteral("电桩数量无效。");
        return false;
    }
    if (!readFiniteNumber(object.value(QStringLiteral("distanceKm")), &distanceKm)
        || distanceKm < 0.0) {
        *error = QStringLiteral("distanceKm 必须是非负数。");
        return false;
    }

    *result = {stationId,
               object.value(QStringLiteral("name")).toString(),
               object.value(QStringLiteral("address")).toString(),
               latitude,
               longitude,
               price.toString(),
               static_cast<int>(totalPiles),
               static_cast<int>(availablePiles),
               distanceKm};
    return true;
}

bool mapPile(const QJsonValue &value, PileSummary *result, QString *error)
{
    if (!value.isObject()) {
        *error = QStringLiteral("电桩条目必须是对象。");
        return false;
    }
    const QJsonObject object = value.toObject();
    qint64 pileId = 0;
    double ratedPower = 0.0;
    const QJsonValue heartbeat = object.value(QStringLiteral("lastHeartbeatAt"));
    if (!readInteger(object.value(QStringLiteral("pileId")), &pileId) || pileId <= 0
        || !object.value(QStringLiteral("serial")).isString()
        || !object.value(QStringLiteral("chargeType")).isString()
        || !readFiniteNumber(object.value(QStringLiteral("ratedPowerKw")), &ratedPower)
        || ratedPower < 0.0 || !object.value(QStringLiteral("status")).isString()
        || (!heartbeat.isNull() && !heartbeat.isString())) {
        *error = QStringLiteral("电桩字段类型无效。");
        return false;
    }
    *result = {pileId, object.value(QStringLiteral("serial")).toString(),
               object.value(QStringLiteral("chargeType")).toString(), ratedPower,
               object.value(QStringLiteral("status")).toString(),
               object.value(QStringLiteral("lastHeartbeatAt")).toString()};
    return true;
}

bool mapDetail(const QJsonObject &object, StationDetail *result, QString *error)
{
    qint64 stationId = 0;
    double latitude = 0.0;
    double longitude = 0.0;
    static const QRegularExpression decimal(QStringLiteral("^(0|[1-9]\\d*)\\.\\d{2}$"));
    const QJsonValue price = object.value(QStringLiteral("priceYuanPerKwh"));
    const QJsonValue piles = object.value(QStringLiteral("piles"));
    if (!readInteger(object.value(QStringLiteral("stationId")), &stationId) || stationId <= 0
        || !object.value(QStringLiteral("name")).isString()
        || !object.value(QStringLiteral("address")).isString()
        || !readFiniteNumber(object.value(QStringLiteral("latitude")), &latitude)
        || latitude < -90.0 || latitude > 90.0
        || !readFiniteNumber(object.value(QStringLiteral("longitude")), &longitude)
        || longitude < -180.0 || longitude > 180.0
        || !price.isString() || !decimal.match(price.toString()).hasMatch()
        || !object.value(QStringLiteral("status")).isString() || !piles.isArray()) {
        *error = QStringLiteral("站点详情字段无效。");
        return false;
    }
    QList<PileSummary> mappedPiles;
    for (const QJsonValue &value : piles.toArray()) {
        PileSummary pile;
        if (!mapPile(value, &pile, error)) return false;
        mappedPiles.append(pile);
    }
    *result = {stationId, object.value(QStringLiteral("name")).toString(),
               object.value(QStringLiteral("address")).toString(), latitude, longitude,
               price.toString(), object.value(QStringLiteral("status")).toString(),
               mappedPiles};
    return true;
}

QString failureMessage(const QJsonObject &response)
{
    const QString inner = response.value(QStringLiteral("error")).toObject()
                              .value(QStringLiteral("message")).toString();
    if (!inner.isEmpty()) {
        return inner;
    }
    const QString outer = response.value(QStringLiteral("message")).toString();
    return outer.isEmpty() ? QStringLiteral("站点查询失败，请稍后重试。") : outer;
}

} // namespace

RemoteStationProvider::RemoteStationProvider(QString host, quint16 port, QObject *parent)
    : IStationProvider(parent)
    , client_(new ServiceClient(std::move(host), port, this))
{
    connectClient();
}

RemoteStationProvider::RemoteStationProvider(ServiceClient *client, QObject *parent)
    : IStationProvider(parent)
    , client_(client)
{
    connectClient();
}

void RemoteStationProvider::queryNearby(const StationQuery &query)
{
    if (!client_) {
        emit queryFinished(errorResult(QStringLiteral("站点服务不可用。")));
        return;
    }
    if (!std::isfinite(query.latitude) || query.latitude < -90.0 || query.latitude > 90.0
        || !std::isfinite(query.longitude) || query.longitude < -180.0
        || query.longitude > 180.0) {
        emit queryFinished(errorResult(QStringLiteral("查询位置坐标无效。")));
        return;
    }
    if (pending_ != Pending::None || client_->hasPendingRequest()) {
        emit queryFinished(errorResult(QStringLiteral("站点查询正在处理中，请稍候。")));
        return;
    }

    pending_ = Pending::Nearby;
    const QString requestId = client_->sendRequest(
        QStringLiteral("STATION_NEARBY"),
        QJsonObject{{QStringLiteral("latitude"), query.latitude},
                    {QStringLiteral("longitude"), query.longitude}});
    if (requestId.isEmpty()) {
        pending_ = Pending::None;
        emit queryFinished(errorResult(QStringLiteral("站点查询正在处理中，请稍候。")));
    }
}

void RemoteStationProvider::queryDetail(qint64 stationId)
{
    if (!client_) {
        emit detailFinished({StationQueryResultKind::Error, QStringLiteral("站点服务不可用。"),
                             QStringLiteral("UNAVAILABLE"), {}});
        return;
    }
    if (stationId <= 0) {
        emit detailFinished({StationQueryResultKind::Error, QStringLiteral("站点标识无效。"),
                             QStringLiteral("PARAM_INVALID"), {}});
        return;
    }
    if (pending_ != Pending::None || client_->hasPendingRequest()) {
        emit detailFinished({StationQueryResultKind::Error,
                             QStringLiteral("站点查询正在处理中，请稍候。"),
                             QStringLiteral("REQUEST_BUSY"), {}});
        return;
    }
    pending_ = Pending::Detail;
    if (client_->sendRequest(QStringLiteral("STATION_DETAIL"),
                             {{QStringLiteral("stationId"), stationId}}).isEmpty()) {
        pending_ = Pending::None;
        emit detailFinished({StationQueryResultKind::Error,
                             QStringLiteral("站点查询正在处理中，请稍候。"),
                             QStringLiteral("REQUEST_BUSY"), {}});
    }
}

void RemoteStationProvider::connectClient()
{
    if (!client_) {
        return;
    }
    connect(client_, &ServiceClient::connectionStateChanged, this,
            [this](bool connected) {
        if (connected) emit connectivityRestored();
        else emit connectivityLost();
    });
    connect(client_, &ServiceClient::responseReceived, this,
            [this](const QJsonObject &response) {
        if (pending_ == Pending::None) return;
        const Pending pending = pending_;
        pending_ = Pending::None;
        handleResponse(response, pending);
    });
    connect(client_, &ServiceClient::requestFailed, this,
            [this](ServiceClientError error, const QString &message) {
        if (pending_ == Pending::None) return;
        handleFailure(error, message);
    });
    connect(client_, &QObject::destroyed, this, [this] {
        client_.clear();
        if (pending_ == Pending::None) return;
        const Pending pending = pending_;
        pending_ = Pending::None;
        if (pending == Pending::Nearby)
            emit queryFinished(errorResult(QStringLiteral("站点服务不可用。")));
        else
            emit detailFinished({StationQueryResultKind::Error,
                                 QStringLiteral("站点服务不可用。"),
                                 QStringLiteral("UNAVAILABLE"), {}});
    });
}

void RemoteStationProvider::handleResponse(const QJsonObject &response, Pending pending)
{
    if (!response.value(QStringLiteral("ok")).toBool()) {
        if (pending == Pending::Nearby)
            emit queryFinished({StationQueryResultKind::Error, failureMessage(response), {},
                                responseCode(response)});
        else
            emit detailFinished({StationQueryResultKind::Error, failureMessage(response),
                                 responseCode(response), {}});
        return;
    }
    if (pending == Pending::Detail) {
        StationDetail detail;
        QString error;
        if (!mapDetail(response.value(QStringLiteral("data")).toObject(), &detail, &error)) {
            emit detailFinished({StationQueryResultKind::Error,
                                 QStringLiteral("站点详情响应无效：") + error,
                                 QStringLiteral("MALFORMED_DATA"), {}});
            return;
        }
        emit detailFinished({StationQueryResultKind::Success,
                             response.value(QStringLiteral("message")).toString(),
                             response.value(QStringLiteral("code")).toString(), detail});
        return;
    }
    const QJsonValue stationsValue = response.value(QStringLiteral("data"))
                                         .toObject().value(QStringLiteral("stations"));
    if (!stationsValue.isArray()) {
        emit queryFinished(errorResult(QStringLiteral("站点响应缺少 stations 数组。")));
        return;
    }

    QList<StationSummary> stations;
    for (const QJsonValue &value : stationsValue.toArray()) {
        StationSummary station;
        QString error;
        if (!mapStation(value, &station, &error)) {
            emit queryFinished(errorResult(QStringLiteral("站点响应数据无效：") + error));
            return;
        }
        stations.append(station);
    }
    emit queryFinished({StationQueryResultKind::Success,
                        response.value(QStringLiteral("message")).toString(),
                        stations, response.value(QStringLiteral("code")).toString()});
}

void RemoteStationProvider::handleFailure(ServiceClientError error, const QString &message)
{
    const Pending pending = pending_;
    pending_ = Pending::None;
    const bool offline = error == ServiceClientError::Offline
        || error == ServiceClientError::Disconnected;
    const QString fallback = offline ? QStringLiteral("无法连接站点服务，请检查网络。")
                                     : QStringLiteral("站点查询失败，请稍后重试。");
    const StationQueryResultKind kind = offline ? StationQueryResultKind::Offline
                                                 : StationQueryResultKind::Error;
    const QString code = error == ServiceClientError::Timeout
        ? QStringLiteral("TIMEOUT")
        : (error == ServiceClientError::Offline ? QStringLiteral("OFFLINE")
           : (error == ServiceClientError::Disconnected ? QStringLiteral("DISCONNECTED")
                                                         : QStringLiteral("PROTOCOL_ERROR")));
    if (pending == Pending::Nearby)
        emit queryFinished({kind, message.isEmpty() ? fallback : message, {}, code});
    else
        emit detailFinished({kind, message.isEmpty() ? fallback : message, code, {}});
}
