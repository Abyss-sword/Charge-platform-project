#pragma once

#include "models/station.h"

#include <QObject>
#include <QString>

enum class StationQueryResultKind
{
    Success,
    Error,
    Offline
};

struct StationQueryResult
{
    StationQueryResultKind kind = StationQueryResultKind::Error;
    QString message;
    QList<StationSummary> stations;
    QString errorCode;
};

struct StationDetailResult
{
    StationQueryResultKind kind = StationQueryResultKind::Error;
    QString message;
    QString errorCode;
    StationDetail detail;
};

class IStationProvider : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~IStationProvider() override = default;

    virtual void queryNearby(const StationQuery &query) = 0;
    virtual void queryDetail(qint64) {}

signals:
    void queryFinished(const StationQueryResult &result);
    void detailFinished(const StationDetailResult &result);
    void connectivityRestored();
    void connectivityLost();
};

Q_DECLARE_METATYPE(StationQueryResult)
Q_DECLARE_METATYPE(StationDetailResult)
