#include <QtTest>

#include "analysis/forecast_parser.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using charge::analysis::ForecastDataMode;
using charge::analysis::ForecastModelMode;
using charge::analysis::ForecastParseError;
using charge::analysis::ForecastParser;
using charge::analysis::ForecastSourceMode;
using charge::analysis::ForecastStatus;
using charge::analysis::ForecastStatusCategory;

class ForecastContractTest final : public QObject
{
    Q_OBJECT

private slots:
    void officialFixturesParse_data()
    {
        QTest::addColumn<QString>("fileName");
        QTest::addColumn<ForecastStatus>("status");
        QTest::addColumn<ForecastModelMode>("modelMode");
        QTest::addColumn<int>("pointCount");

        QTest::newRow("normal")
            << QStringLiteral("normal.json") << ForecastStatus::Ok
            << ForecastModelMode::Trained << 24;
        QTest::newRow("baseline")
            << QStringLiteral("baseline.json") << ForecastStatus::Ok
            << ForecastModelMode::Baseline << 24;
        QTest::newRow("no-data")
            << QStringLiteral("no_data.json") << ForecastStatus::NoData
            << ForecastModelMode::Unavailable << 0;
        QTest::newRow("expired")
            << QStringLiteral("expired.json") << ForecastStatus::Expired
            << ForecastModelMode::Trained << 0;
        QTest::newRow("model-failure")
            << QStringLiteral("model_failure.json") << ForecastStatus::ModelUnavailable
            << ForecastModelMode::Unavailable << 0;
        QTest::newRow("capacity-missing")
            << QStringLiteral("capacity_missing.json") << ForecastStatus::Ok
            << ForecastModelMode::Baseline << 24;
    }

    void officialFixturesParse()
    {
        QFETCH(QString, fileName);
        QFETCH(ForecastStatus, status);
        QFETCH(ForecastModelMode, modelMode);
        QFETCH(int, pointCount);

        const auto result = ForecastParser::parse(loadFixture(fileName));
        QVERIFY2(result.valid, qPrintable(result.message));
        QCOMPARE(result.error, ForecastParseError::None);
        QCOMPARE(result.forecast.contractVersion, QStringLiteral("forecast_v1"));
        QCOMPARE(result.forecast.status, status);
        QCOMPARE(result.forecast.modelMode, modelMode);
        QCOMPARE(result.forecast.points.size(), pointCount);
        QCOMPARE(result.forecast.generatedAt,
                 utc(QStringLiteral("2026-09-13T00:00:30Z")));
    }

    void mapsEnvelopeAndForecastPoints()
    {
        const auto result = ForecastParser::parse(loadFixture(QStringLiteral("normal.json")));
        QVERIFY2(result.valid, qPrintable(result.message));

        const auto &forecast = result.forecast;
        QCOMPARE(forecast.originTime, utc(QStringLiteral("2026-09-13T00:00:00Z")));
        QCOMPARE(forecast.targetStart, utc(QStringLiteral("2026-09-13T01:00:00Z")));
        QCOMPARE(forecast.targetEnd, utc(QStringLiteral("2026-09-14T01:00:00Z")));
        QCOMPARE(forecast.horizonHours, 24);
        QCOMPARE(forecast.analysisStationId, QStringLiteral("acn:caltech-1"));
        QVERIFY(!forecast.businessStationId.has_value());
        QCOMPARE(forecast.dataMode, ForecastDataMode::PublicAggregated);
        QCOMPARE(forecast.datasetVersion, QStringLiteral("fixture-v1"));
        QCOMPARE(forecast.sourceMode, ForecastSourceMode::Fixture);
        QVERIFY(!forecast.degraded);
        QVERIFY(forecast.reasons.isEmpty());

        const auto &first = forecast.points.first();
        QCOMPARE(first.targetStart, utc(QStringLiteral("2026-09-13T01:00:00Z")));
        QCOMPARE(first.targetEnd, utc(QStringLiteral("2026-09-13T02:00:00Z")));
        QCOMPARE(first.horizonHours, 1);
        QCOMPARE(first.energyKwh.value(), QStringLiteral("9.96"));
        QCOMPARE(first.meanPowerKw.value(), QStringLiteral("9.96"));
        QCOMPARE(first.freePilesExpected.value(), 4);
        QVERIFY(!first.nullReason.has_value());

        const auto &last = forecast.points.last();
        QCOMPARE(last.horizonHours, 24);
        QCOMPARE(last.energyKwh.value(), QStringLiteral("13.94"));
        QCOMPARE(last.freePilesExpected.value(), 7);
    }

    void emptyArraysAreValid()
    {
        const auto result = ForecastParser::parse(loadFixture(QStringLiteral("no_data.json")));
        QVERIFY2(result.valid, qPrintable(result.message));
        QVERIFY(result.forecast.points.isEmpty());
        QCOMPARE(result.forecast.reasons.size(), 1);
        QVERIFY(result.forecast.reasons.first().startsWith(QStringLiteral("NO_HOURLY_DATA:")));
    }

    void nullableFieldsArePreserved()
    {
        const auto result = ForecastParser::parse(
            loadFixture(QStringLiteral("capacity_missing.json")));
        QVERIFY2(result.valid, qPrintable(result.message));
        QVERIFY(!result.forecast.businessStationId.has_value());
        QCOMPARE(result.forecast.points.size(), 24);
        for (const auto &point : result.forecast.points) {
            QVERIFY(!point.freePilesExpected.has_value());
            QVERIFY(!point.nullReason.has_value());
            QVERIFY(point.energyKwh.has_value());
        }
    }

    void allContractStatusesMap_data()
    {
        QTest::addColumn<QString>("wireValue");
        QTest::addColumn<ForecastStatus>("status");
        QTest::addColumn<ForecastStatusCategory>("category");
        QTest::newRow("ok") << QStringLiteral("OK") << ForecastStatus::Ok
                            << ForecastStatusCategory::Available;
        QTest::newRow("no-data") << QStringLiteral("NO_DATA") << ForecastStatus::NoData
                                 << ForecastStatusCategory::DataInsufficient;
        QTest::newRow("expired") << QStringLiteral("EXPIRED") << ForecastStatus::Expired
                                 << ForecastStatusCategory::DataInsufficient;
        QTest::newRow("model-unavailable")
            << QStringLiteral("MODEL_UNAVAILABLE") << ForecastStatus::ModelUnavailable
            << ForecastStatusCategory::DependencyUnavailable;
        QTest::newRow("dependency-unavailable")
            << QStringLiteral("DEPENDENCY_UNAVAILABLE")
            << ForecastStatus::DependencyUnavailable
            << ForecastStatusCategory::DependencyUnavailable;
        QTest::newRow("param-invalid")
            << QStringLiteral("PARAM_INVALID") << ForecastStatus::ParamInvalid
            << ForecastStatusCategory::InvalidRequest;
    }

    void allContractStatusesMap()
    {
        QFETCH(QString, wireValue);
        QFETCH(ForecastStatus, status);
        QFETCH(ForecastStatusCategory, category);
        const auto mapped = charge::analysis::forecastStatusFromString(wireValue);
        QVERIFY(mapped.has_value());
        QCOMPARE(mapped.value(), status);
        QCOMPARE(charge::analysis::forecastStatusToString(status), wireValue);
        QCOMPARE(charge::analysis::forecastStatusCategory(status), category);
    }

    void nullablePointValuesArePreserved()
    {
        QJsonObject object = fixtureObject(QStringLiteral("normal.json"));
        QJsonArray points = object.value(QStringLiteral("points")).toArray();
        QJsonObject first = points.first().toObject();
        first.insert(QStringLiteral("energyKwh"), QJsonValue::Null);
        first.insert(QStringLiteral("meanPowerKw"), QJsonValue::Null);
        first.insert(QStringLiteral("freePilesExpected"), QJsonValue::Null);
        first.insert(QStringLiteral("nullReason"), QStringLiteral("NO_CAPACITY_DATA"));
        points.replace(0, first);
        object.insert(QStringLiteral("points"), points);

        const auto result = ForecastParser::parse(object);
        QVERIFY2(result.valid, qPrintable(result.message));
        const auto &point = result.forecast.points.first();
        QVERIFY(!point.energyKwh.has_value());
        QVERIFY(!point.meanPowerKw.has_value());
        QVERIFY(!point.freePilesExpected.has_value());
        QCOMPARE(point.nullReason.value(),
                 charge::analysis::ForecastNullReason::NoCapacityData);
    }

    void rejectsSchemaVersionMismatch()
    {
        QJsonObject object = fixtureObject(QStringLiteral("normal.json"));
        object.insert(QStringLiteral("contractVersion"), QStringLiteral("forecast_v2"));
        const auto result = ForecastParser::parse(object);
        QVERIFY(!result.valid);
        QCOMPARE(result.error, ForecastParseError::SchemaVersionMismatch);
        QCOMPARE(result.errorCode, QStringLiteral("SCHEMA_VERSION_MISMATCH"));
        QVERIFY(result.message.contains(QStringLiteral("contractVersion")));
    }

    void rejectsMissingRequiredField()
    {
        QJsonObject object = fixtureObject(QStringLiteral("normal.json"));
        object.remove(QStringLiteral("datasetVersion"));
        const auto result = ForecastParser::parse(object);
        QVERIFY(!result.valid);
        QCOMPARE(result.error, ForecastParseError::MissingRequiredField);
        QCOMPARE(result.errorCode, QStringLiteral("MISSING_REQUIRED_FIELD"));
        QVERIFY(result.message.contains(QStringLiteral("datasetVersion")));
    }

    void rejectsMalformedJson()
    {
        const auto result = ForecastParser::parse(QByteArrayLiteral("{not-json"));
        QVERIFY(!result.valid);
        QCOMPARE(result.error, ForecastParseError::MalformedJson);
        QCOMPARE(result.errorCode, QStringLiteral("MALFORMED_JSON"));
        QVERIFY(!result.message.isEmpty());
    }

    void rejectsUnknownStatus()
    {
        QJsonObject object = fixtureObject(QStringLiteral("normal.json"));
        object.insert(QStringLiteral("status"), QStringLiteral("LOADING"));
        const auto result = ForecastParser::parse(object);
        QVERIFY(!result.valid);
        QCOMPARE(result.error, ForecastParseError::UnsupportedStatus);
        QCOMPARE(result.errorCode, QStringLiteral("UNSUPPORTED_STATUS"));
        QVERIFY(result.message.contains(QStringLiteral("LOADING")));
    }

private:
    static QByteArray loadFixture(const QString &fileName)
    {
        QFile file(QStringLiteral(CHARGE_PLATFORM_SOURCE_DIR)
                   + QStringLiteral("/tests/fixtures/stage2/forecast/") + fileName);
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return file.readAll();
    }

    static QJsonObject fixtureObject(const QString &fileName)
    {
        return QJsonDocument::fromJson(loadFixture(fileName)).object();
    }

    static QDateTime utc(const QString &text)
    {
        return QDateTime::fromString(text, Qt::ISODate);
    }
};

QTEST_GUILESS_MAIN(ForecastContractTest)
#include "forecast_contract_test.moc"
