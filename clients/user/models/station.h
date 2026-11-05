#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

struct StationQuery
{
    double latitude = 0.0;
    double longitude = 0.0;
};

struct StationSummary
{
    qint64 stationId = 0;
    QString name;
    QString address;
    double latitude = 0.0;
    double longitude = 0.0;
    QString priceYuanPerKwh;
    int totalPiles = 0;
    int availablePiles = 0;
    double distanceKm = 0.0;
};

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
    QString priceYuanPerKwh;
    QString status;
    QList<PileSummary> piles;
};

Q_DECLARE_METATYPE(StationSummary)
Q_DECLARE_METATYPE(QList<StationSummary>)
Q_DECLARE_METATYPE(PileSummary)
Q_DECLARE_METATYPE(StationDetail)
