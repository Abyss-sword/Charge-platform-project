#pragma once

#include "analysis/forecast_model.h"

#include <QByteArray>
#include <QJsonObject>

namespace charge::analysis {

enum class ForecastParseError
{
    None,
    MalformedJson,
    RootNotObject,
    SchemaVersionMismatch,
    MissingRequiredField,
    InvalidFieldType,
    InvalidFieldValue,
    UnsupportedStatus
};

struct ForecastParseResult
{
    bool valid = false;
    ForecastParseError error = ForecastParseError::None;
    QString errorCode;
    QString message;
    ForecastEnvelope forecast;
};

class ForecastParser final
{
public:
    static ForecastParseResult parse(const QByteArray &json);
    static ForecastParseResult parse(const QJsonObject &object);
};

}  // namespace charge::analysis

Q_DECLARE_METATYPE(charge::analysis::ForecastParseError)
Q_DECLARE_METATYPE(charge::analysis::ForecastParseResult)
