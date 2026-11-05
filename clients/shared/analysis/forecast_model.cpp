#include "analysis/forecast_model.h"

namespace charge::analysis {

std::optional<ForecastStatus> forecastStatusFromString(const QString &value)
{
    if (value == QStringLiteral("OK")) {
        return ForecastStatus::Ok;
    }
    if (value == QStringLiteral("NO_DATA")) {
        return ForecastStatus::NoData;
    }
    if (value == QStringLiteral("EXPIRED")) {
        return ForecastStatus::Expired;
    }
    if (value == QStringLiteral("MODEL_UNAVAILABLE")) {
        return ForecastStatus::ModelUnavailable;
    }
    if (value == QStringLiteral("DEPENDENCY_UNAVAILABLE")) {
        return ForecastStatus::DependencyUnavailable;
    }
    if (value == QStringLiteral("PARAM_INVALID")) {
        return ForecastStatus::ParamInvalid;
    }
    return std::nullopt;
}

QString forecastStatusToString(ForecastStatus status)
{
    switch (status) {
    case ForecastStatus::Ok:
        return QStringLiteral("OK");
    case ForecastStatus::NoData:
        return QStringLiteral("NO_DATA");
    case ForecastStatus::Expired:
        return QStringLiteral("EXPIRED");
    case ForecastStatus::ModelUnavailable:
        return QStringLiteral("MODEL_UNAVAILABLE");
    case ForecastStatus::DependencyUnavailable:
        return QStringLiteral("DEPENDENCY_UNAVAILABLE");
    case ForecastStatus::ParamInvalid:
        return QStringLiteral("PARAM_INVALID");
    }
    return {};
}

ForecastStatusCategory forecastStatusCategory(ForecastStatus status)
{
    switch (status) {
    case ForecastStatus::Ok:
        return ForecastStatusCategory::Available;
    case ForecastStatus::NoData:
    case ForecastStatus::Expired:
        return ForecastStatusCategory::DataInsufficient;
    case ForecastStatus::ModelUnavailable:
    case ForecastStatus::DependencyUnavailable:
        return ForecastStatusCategory::DependencyUnavailable;
    case ForecastStatus::ParamInvalid:
        return ForecastStatusCategory::InvalidRequest;
    }
    return ForecastStatusCategory::DependencyUnavailable;
}

}  // namespace charge::analysis
