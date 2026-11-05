#include "analysis/forecast_parser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>

#include <cmath>
#include <limits>

namespace charge::analysis {
namespace {

constexpr auto kContractVersion = "forecast_v1";

ForecastParseResult failure(ForecastParseError error,
                            const QString &errorCode,
                            const QString &message)
{
    ForecastParseResult result;
    result.error = error;
    result.errorCode = errorCode;
    result.message = message;
    return result;
}

ForecastParseResult missing(const QString &path)
{
    return failure(ForecastParseError::MissingRequiredField,
                   QStringLiteral("MISSING_REQUIRED_FIELD"),
                   QStringLiteral("Missing required field: %1").arg(path));
}

ForecastParseResult invalidType(const QString &path, const QString &expected)
{
    return failure(ForecastParseError::InvalidFieldType,
                   QStringLiteral("INVALID_FIELD_TYPE"),
                   QStringLiteral("Field %1 must be %2").arg(path, expected));
}

ForecastParseResult invalidValue(const QString &path, const QString &detail)
{
    return failure(ForecastParseError::InvalidFieldValue,
                   QStringLiteral("INVALID_FIELD_VALUE"),
                   QStringLiteral("Invalid value for %1: %2").arg(path, detail));
}

bool requireValue(const QJsonObject &object,
                  const QString &key,
                  const QString &path,
                  QJsonValue *value,
                  ForecastParseResult *error)
{
    if (!object.contains(key)) {
        *error = missing(path);
        return false;
    }
    *value = object.value(key);
    return true;
}

bool readString(const QJsonObject &object,
                const QString &key,
                const QString &path,
                QString *output,
                ForecastParseResult *error)
{
    QJsonValue value;
    if (!requireValue(object, key, path, &value, error)) {
        return false;
    }
    if (!value.isString()) {
        *error = invalidType(path, QStringLiteral("a string"));
        return false;
    }
    *output = value.toString();
    return true;
}

bool readDateTime(const QJsonObject &object,
                  const QString &key,
                  const QString &path,
                  QDateTime *output,
                  ForecastParseResult *error)
{
    QString text;
    if (!readString(object, key, path, &text, error)) {
        return false;
    }
    static const QRegularExpression format(
        QStringLiteral("^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$"));
    const QDateTime parsed = QDateTime::fromString(text, Qt::ISODate);
    if (!format.match(text).hasMatch() || !parsed.isValid()
        || parsed.timeSpec() != Qt::UTC) {
        *error = invalidValue(path, QStringLiteral("expected UTC YYYY-MM-DDTHH:mm:ssZ"));
        return false;
    }
    *output = parsed;
    return true;
}

bool jsonInteger(const QJsonValue &value, qint64 minimum, qint64 maximum, qint64 *output)
{
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < static_cast<double>(minimum)
        || number > static_cast<double>(maximum)) {
        return false;
    }
    *output = static_cast<qint64>(number);
    return true;
}

bool readInteger(const QJsonObject &object,
                 const QString &key,
                 const QString &path,
                 int minimum,
                 int maximum,
                 int *output,
                 ForecastParseResult *error)
{
    QJsonValue value;
    if (!requireValue(object, key, path, &value, error)) {
        return false;
    }
    qint64 parsed = 0;
    if (!jsonInteger(value, minimum, maximum, &parsed)) {
        *error = invalidValue(path,
                              QStringLiteral("expected an integer from %1 to %2")
                                  .arg(minimum)
                                  .arg(maximum));
        return false;
    }
    *output = static_cast<int>(parsed);
    return true;
}

bool rejectUnknownFields(const QJsonObject &object,
                         const QSet<QString> &allowed,
                         const QString &path,
                         ForecastParseResult *error)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowed.contains(it.key())) {
            *error = invalidValue(path + it.key(), QStringLiteral("field is not in forecast_v1"));
            return false;
        }
    }
    return true;
}

std::optional<ForecastDataMode> dataModeFromString(const QString &value)
{
    if (value == QStringLiteral("SIMULATED")) {
        return ForecastDataMode::Simulated;
    }
    if (value == QStringLiteral("REAL_ANONYMIZED")) {
        return ForecastDataMode::RealAnonymized;
    }
    if (value == QStringLiteral("PUBLIC_AGGREGATED")) {
        return ForecastDataMode::PublicAggregated;
    }
    return std::nullopt;
}

std::optional<ForecastModelMode> modelModeFromString(const QString &value)
{
    if (value == QStringLiteral("TRAINED")) {
        return ForecastModelMode::Trained;
    }
    if (value == QStringLiteral("BASELINE")) {
        return ForecastModelMode::Baseline;
    }
    if (value == QStringLiteral("UNAVAILABLE")) {
        return ForecastModelMode::Unavailable;
    }
    return std::nullopt;
}

std::optional<ForecastSourceMode> sourceModeFromString(const QString &value)
{
    if (value == QStringLiteral("LIVE_ARTIFACT")) {
        return ForecastSourceMode::LiveArtifact;
    }
    if (value == QStringLiteral("FIXTURE")) {
        return ForecastSourceMode::Fixture;
    }
    return std::nullopt;
}

std::optional<ForecastNullReason> nullReasonFromString(const QString &value)
{
    if (value == QStringLiteral("NO_MODEL_ARTIFACT")) {
        return ForecastNullReason::NoModelArtifact;
    }
    if (value == QStringLiteral("NO_HOURLY_DATA")) {
        return ForecastNullReason::NoHourlyData;
    }
    if (value == QStringLiteral("NO_CAPACITY_DATA")) {
        return ForecastNullReason::NoCapacityData;
    }
    if (value == QStringLiteral("ARTIFACT_EXPIRED")) {
        return ForecastNullReason::ArtifactExpired;
    }
    if (value == QStringLiteral("DATA_INSUFFICIENT")) {
        return ForecastNullReason::DataInsufficient;
    }
    return std::nullopt;
}

bool readNullableDecimal(const QJsonObject &object,
                         const QString &key,
                         const QString &path,
                         std::optional<QString> *output,
                         ForecastParseResult *error)
{
    QJsonValue value;
    if (!requireValue(object, key, path, &value, error)) {
        return false;
    }
    if (value.isNull()) {
        output->reset();
        return true;
    }
    if (!value.isString()) {
        *error = invalidType(path, QStringLiteral("a two-decimal string or null"));
        return false;
    }
    static const QRegularExpression format(QStringLiteral("^[0-9]+\\.[0-9]{2}$"));
    if (!format.match(value.toString()).hasMatch()) {
        *error = invalidValue(path, QStringLiteral("expected a two-decimal string"));
        return false;
    }
    *output = value.toString();
    return true;
}

bool parsePoint(const QJsonValue &value,
                int index,
                ForecastPoint *point,
                ForecastParseResult *error)
{
    const QString prefix = QStringLiteral("points[%1].").arg(index);
    if (!value.isObject()) {
        *error = invalidType(QStringLiteral("points[%1]").arg(index),
                             QStringLiteral("an object"));
        return false;
    }
    const QJsonObject object = value.toObject();
    static const QSet<QString> fields{
        QStringLiteral("targetStart"), QStringLiteral("targetEnd"),
        QStringLiteral("horizonHours"), QStringLiteral("energyKwh"),
        QStringLiteral("meanPowerKw"), QStringLiteral("freePilesExpected"),
        QStringLiteral("nullReason")};
    if (!rejectUnknownFields(object, fields, prefix, error)
        || !readDateTime(object, QStringLiteral("targetStart"),
                         prefix + QStringLiteral("targetStart"), &point->targetStart, error)
        || !readDateTime(object, QStringLiteral("targetEnd"),
                         prefix + QStringLiteral("targetEnd"), &point->targetEnd, error)
        || !readInteger(object, QStringLiteral("horizonHours"),
                        prefix + QStringLiteral("horizonHours"), 1, 24,
                        &point->horizonHours, error)
        || !readNullableDecimal(object, QStringLiteral("energyKwh"),
                                prefix + QStringLiteral("energyKwh"),
                                &point->energyKwh, error)
        || !readNullableDecimal(object, QStringLiteral("meanPowerKw"),
                                prefix + QStringLiteral("meanPowerKw"),
                                &point->meanPowerKw, error)) {
        return false;
    }

    QJsonValue freePiles;
    const QString freePilesPath = prefix + QStringLiteral("freePilesExpected");
    if (!requireValue(object, QStringLiteral("freePilesExpected"), freePilesPath,
                      &freePiles, error)) {
        return false;
    }
    if (freePiles.isNull()) {
        point->freePilesExpected.reset();
    } else {
        qint64 parsed = 0;
        if (!jsonInteger(freePiles, 0, std::numeric_limits<int>::max(), &parsed)) {
            *error = invalidValue(freePilesPath,
                                  QStringLiteral("expected a non-negative integer or null"));
            return false;
        }
        point->freePilesExpected = static_cast<int>(parsed);
    }

    QJsonValue nullReason;
    const QString nullReasonPath = prefix + QStringLiteral("nullReason");
    if (!requireValue(object, QStringLiteral("nullReason"), nullReasonPath,
                      &nullReason, error)) {
        return false;
    }
    if (nullReason.isNull()) {
        point->nullReason.reset();
        return true;
    }
    if (!nullReason.isString()) {
        *error = invalidType(nullReasonPath, QStringLiteral("a string or null"));
        return false;
    }
    point->nullReason = nullReasonFromString(nullReason.toString());
    if (!point->nullReason.has_value()) {
        *error = invalidValue(nullReasonPath,
                              QStringLiteral("unsupported null reason %1")
                                  .arg(nullReason.toString()));
        return false;
    }
    return true;
}

}  // namespace

ForecastParseResult ForecastParser::parse(const QByteArray &json)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return failure(ForecastParseError::MalformedJson,
                       QStringLiteral("MALFORMED_JSON"),
                       QStringLiteral("Malformed JSON at offset %1: %2")
                           .arg(parseError.offset)
                           .arg(parseError.errorString()));
    }
    if (!document.isObject()) {
        return failure(ForecastParseError::RootNotObject,
                       QStringLiteral("ROOT_NOT_OBJECT"),
                       QStringLiteral("forecast_v1 root must be an object"));
    }
    return parse(document.object());
}

ForecastParseResult ForecastParser::parse(const QJsonObject &object)
{
    ForecastParseResult error;
    ForecastEnvelope forecast;
    static const QSet<QString> fields{
        QStringLiteral("contractVersion"), QStringLiteral("originTime"),
        QStringLiteral("targetStart"), QStringLiteral("targetEnd"),
        QStringLiteral("horizonHours"), QStringLiteral("analysisStationId"),
        QStringLiteral("businessStationId"), QStringLiteral("dataMode"),
        QStringLiteral("datasetVersion"), QStringLiteral("modelMode"),
        QStringLiteral("status"), QStringLiteral("degraded"),
        QStringLiteral("reasons"), QStringLiteral("points"),
        QStringLiteral("sourceMode"), QStringLiteral("generatedAt")};
    if (!rejectUnknownFields(object, fields, QString(), &error)) {
        return error;
    }

    if (!readString(object, QStringLiteral("contractVersion"),
                    QStringLiteral("contractVersion"), &forecast.contractVersion, &error)) {
        return error;
    }
    if (forecast.contractVersion != QLatin1String(kContractVersion)) {
        return failure(ForecastParseError::SchemaVersionMismatch,
                       QStringLiteral("SCHEMA_VERSION_MISMATCH"),
                       QStringLiteral("Unsupported contractVersion: %1")
                           .arg(forecast.contractVersion));
    }
    if (!readDateTime(object, QStringLiteral("originTime"), QStringLiteral("originTime"),
                      &forecast.originTime, &error)
        || !readDateTime(object, QStringLiteral("targetStart"), QStringLiteral("targetStart"),
                         &forecast.targetStart, &error)
        || !readDateTime(object, QStringLiteral("targetEnd"), QStringLiteral("targetEnd"),
                         &forecast.targetEnd, &error)
        || !readInteger(object, QStringLiteral("horizonHours"),
                        QStringLiteral("horizonHours"), 1, 24,
                        &forecast.horizonHours, &error)
        || !readString(object, QStringLiteral("analysisStationId"),
                       QStringLiteral("analysisStationId"),
                       &forecast.analysisStationId, &error)) {
        return error;
    }
    static const QRegularExpression stationIdFormat(
        QStringLiteral("^[a-z][a-z0-9_-]*:[A-Za-z0-9_.\\-]+$"));
    if (!stationIdFormat.match(forecast.analysisStationId).hasMatch()) {
        return invalidValue(QStringLiteral("analysisStationId"),
                            QStringLiteral("expected a namespaced station id"));
    }

    QJsonValue businessStationId;
    if (!requireValue(object, QStringLiteral("businessStationId"),
                      QStringLiteral("businessStationId"), &businessStationId, &error)) {
        return error;
    }
    if (businessStationId.isNull()) {
        forecast.businessStationId.reset();
    } else {
        qint64 parsed = 0;
        if (!jsonInteger(businessStationId, 1, 9007199254740991LL, &parsed)) {
            return invalidValue(QStringLiteral("businessStationId"),
                                QStringLiteral("expected a positive integer or null"));
        }
        forecast.businessStationId = parsed;
    }

    QString wireValue;
    if (!readString(object, QStringLiteral("dataMode"), QStringLiteral("dataMode"),
                    &wireValue, &error)) {
        return error;
    }
    const auto dataMode = dataModeFromString(wireValue);
    if (!dataMode.has_value()) {
        return invalidValue(QStringLiteral("dataMode"),
                            QStringLiteral("unsupported mode %1").arg(wireValue));
    }
    forecast.dataMode = dataMode.value();

    if (!readString(object, QStringLiteral("datasetVersion"),
                    QStringLiteral("datasetVersion"), &forecast.datasetVersion, &error)) {
        return error;
    }
    if (forecast.datasetVersion.isEmpty()) {
        return invalidValue(QStringLiteral("datasetVersion"),
                            QStringLiteral("must not be empty"));
    }

    if (!readString(object, QStringLiteral("modelMode"), QStringLiteral("modelMode"),
                    &wireValue, &error)) {
        return error;
    }
    const auto modelMode = modelModeFromString(wireValue);
    if (!modelMode.has_value()) {
        return invalidValue(QStringLiteral("modelMode"),
                            QStringLiteral("unsupported mode %1").arg(wireValue));
    }
    forecast.modelMode = modelMode.value();

    if (!readString(object, QStringLiteral("status"), QStringLiteral("status"),
                    &wireValue, &error)) {
        return error;
    }
    const auto status = forecastStatusFromString(wireValue);
    if (!status.has_value()) {
        return failure(ForecastParseError::UnsupportedStatus,
                       QStringLiteral("UNSUPPORTED_STATUS"),
                       QStringLiteral("Unsupported forecast status: %1").arg(wireValue));
    }
    forecast.status = status.value();

    QJsonValue degraded;
    if (!requireValue(object, QStringLiteral("degraded"), QStringLiteral("degraded"),
                      &degraded, &error)) {
        return error;
    }
    if (!degraded.isBool()) {
        return invalidType(QStringLiteral("degraded"), QStringLiteral("a boolean"));
    }
    forecast.degraded = degraded.toBool();

    QJsonValue reasons;
    if (!requireValue(object, QStringLiteral("reasons"), QStringLiteral("reasons"),
                      &reasons, &error)) {
        return error;
    }
    if (!reasons.isArray()) {
        return invalidType(QStringLiteral("reasons"), QStringLiteral("an array"));
    }
    for (int index = 0; index < reasons.toArray().size(); ++index) {
        const QJsonValue reason = reasons.toArray().at(index);
        if (!reason.isString()) {
            return invalidType(QStringLiteral("reasons[%1]").arg(index),
                               QStringLiteral("a string"));
        }
        if (reason.toString().isEmpty()) {
            return invalidValue(QStringLiteral("reasons[%1]").arg(index),
                                QStringLiteral("must not be empty"));
        }
        forecast.reasons.append(reason.toString());
    }

    QJsonValue points;
    if (!requireValue(object, QStringLiteral("points"), QStringLiteral("points"),
                      &points, &error)) {
        return error;
    }
    if (!points.isArray()) {
        return invalidType(QStringLiteral("points"), QStringLiteral("an array"));
    }
    const QJsonArray pointArray = points.toArray();
    for (int index = 0; index < pointArray.size(); ++index) {
        ForecastPoint point;
        if (!parsePoint(pointArray.at(index), index, &point, &error)) {
            return error;
        }
        forecast.points.append(point);
    }

    if (!readString(object, QStringLiteral("sourceMode"), QStringLiteral("sourceMode"),
                    &wireValue, &error)) {
        return error;
    }
    const auto sourceMode = sourceModeFromString(wireValue);
    if (!sourceMode.has_value()) {
        return invalidValue(QStringLiteral("sourceMode"),
                            QStringLiteral("unsupported mode %1").arg(wireValue));
    }
    forecast.sourceMode = sourceMode.value();
    if (!readDateTime(object, QStringLiteral("generatedAt"), QStringLiteral("generatedAt"),
                      &forecast.generatedAt, &error)) {
        return error;
    }

    ForecastParseResult result;
    result.valid = true;
    result.forecast = forecast;
    return result;
}

}  // namespace charge::analysis
