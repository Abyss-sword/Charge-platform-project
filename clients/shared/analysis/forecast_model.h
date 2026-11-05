#pragma once

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <optional>

namespace charge::analysis {

enum class ForecastStatus
{
    Ok,
    NoData,
    Expired,
    ModelUnavailable,
    DependencyUnavailable,
    ParamInvalid
};

enum class ForecastStatusCategory
{
    Available,
    DataInsufficient,
    DependencyUnavailable,
    InvalidRequest
};

enum class ForecastDataMode
{
    Simulated,
    RealAnonymized,
    PublicAggregated
};

enum class ForecastModelMode
{
    Trained,
    Baseline,
    Unavailable
};

enum class ForecastSourceMode
{
    LiveArtifact,
    Fixture
};

enum class ForecastNullReason
{
    NoModelArtifact,
    NoHourlyData,
    NoCapacityData,
    ArtifactExpired,
    DataInsufficient
};

struct ForecastPoint
{
    QDateTime targetStart;
    QDateTime targetEnd;
    int horizonHours = 0;
    std::optional<QString> energyKwh;
    std::optional<QString> meanPowerKw;
    std::optional<int> freePilesExpected;
    std::optional<ForecastNullReason> nullReason;
};

struct ForecastEnvelope
{
    QString contractVersion;
    QDateTime originTime;
    QDateTime targetStart;
    QDateTime targetEnd;
    int horizonHours = 0;
    QString analysisStationId;
    std::optional<qint64> businessStationId;
    ForecastDataMode dataMode = ForecastDataMode::Simulated;
    QString datasetVersion;
    ForecastModelMode modelMode = ForecastModelMode::Unavailable;
    ForecastStatus status = ForecastStatus::NoData;
    bool degraded = false;
    QStringList reasons;
    QList<ForecastPoint> points;
    ForecastSourceMode sourceMode = ForecastSourceMode::Fixture;
    QDateTime generatedAt;
};

std::optional<ForecastStatus> forecastStatusFromString(const QString &value);
QString forecastStatusToString(ForecastStatus status);
ForecastStatusCategory forecastStatusCategory(ForecastStatus status);

}  // namespace charge::analysis

Q_DECLARE_METATYPE(charge::analysis::ForecastStatus)
Q_DECLARE_METATYPE(charge::analysis::ForecastStatusCategory)
Q_DECLARE_METATYPE(charge::analysis::ForecastDataMode)
Q_DECLARE_METATYPE(charge::analysis::ForecastModelMode)
Q_DECLARE_METATYPE(charge::analysis::ForecastSourceMode)
Q_DECLARE_METATYPE(charge::analysis::ForecastNullReason)
Q_DECLARE_METATYPE(charge::analysis::ForecastPoint)
Q_DECLARE_METATYPE(charge::analysis::ForecastEnvelope)
