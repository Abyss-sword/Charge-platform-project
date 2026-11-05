#include <QtTest>
#include "analysis/analysis_http_client.h"
#include "fake_analysis_http_server.h"
#include <QSignalSpy>

using namespace charge::analysis;

class AnalysisHttpClientTest final : public QObject
{
    Q_OBJECT
private slots:
    void sixStatesAndHttpErrors_data()
    {
        QTest::addColumn<QString>("state");
        QTest::addColumn<int>("code");
        QTest::newRow("ok") << QString("OK") << 200;
        QTest::newRow("empty") << QString("NO_DATA") << 200;
        QTest::newRow("expired") << QString("EXPIRED") << 200;
        QTest::newRow("model") << QString("MODEL_UNAVAILABLE") << 200;
        QTest::newRow("dependency") << QString("DEPENDENCY_UNAVAILABLE") << 503;
        QTest::newRow("parameter") << QString("PARAM_INVALID") << 400;
        QTest::newRow("500") << QString("DEPENDENCY_UNAVAILABLE") << 500;
    }
    void sixStatesAndHttpErrors()
    {
        QFETCH(QString, state); QFETCH(int, code);
        FakeAnalysisHttpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.respond = [state, code](const QUrl &) { return FakeAnalysisHttpServer::Response{FakeAnalysisHttpServer::forecast(state), code}; };
        AnalysisHttpClient client(server.baseUrl());
        QSignalSpy spy(&client, &AnalysisHttpClient::forecastFinished);
        const auto id = client.forecast(1, "course:test");
        QTRY_COMPARE(spy.size(), 1);
        const auto result = qvariant_cast<AnalysisHttpResult>(spy.at(0).at(0));
        QCOMPARE(result.generation, id);
        QCOMPARE(result.httpStatus, code);
        QCOMPARE(result.error, code == 200 ? AnalysisHttpError::None : AnalysisHttpError::Http);
        QVERIFY2(result.parsed.valid, qPrintable(result.parsed.message));
        QCOMPARE(forecastStatusToString(result.parsed.forecast.status), state);
        QCOMPARE(result.parsed.forecast.contractVersion, QString("forecast_v1"));
        const QUrlQuery query(QUrl(server.requests.first()));
        QCOMPARE(query.queryItemValue("stationId"), QString("course:test"));
        QCOMPARE(query.queryItemValue("horizon"), QString("1"));
        QVERIFY(!query.hasQueryItem("arrivedAt"));
        if (state == "OK") QCOMPARE(*result.parsed.forecast.points.first().energyKwh, QString("1.25"));
    }
    void healthSuccess()
    {
        FakeAnalysisHttpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        server.respond = [](const QUrl &) { return FakeAnalysisHttpServer::Response{R"({"service":"member3-stage2-flask","framework":"Flask","status":"OK","degraded":false,"reasons":[]})"}; };
        AnalysisHttpClient client(server.baseUrl());
        QSignalSpy spy(&client, &AnalysisHttpClient::healthFinished);
        client.health(); QTRY_COMPARE(spy.size(), 1);
        const auto result = qvariant_cast<AnalysisHttpResult>(spy.at(0).at(0));
        QCOMPARE(result.error, AnalysisHttpError::None);
        QCOMPARE(result.health.framework, QString("Flask"));
        QCOMPARE(result.health.status, ForecastStatus::Ok);
        QCOMPARE(server.requests.first(), QString("/health"));
    }
    void malformedAndUntrusted_data()
    {
        QTest::addColumn<QByteArray>("body"); QTest::addColumn<int>("http"); QTest::addColumn<int>("error");
        QTest::newRow("malformed") << QByteArray("{") << 200 << int(AnalysisHttpError::Contract);
        QTest::newRow("simplified-400") << QByteArray(R"({"status":"PARAM_INVALID","reasons":["bad"]})") << 400 << int(AnalysisHttpError::Http);
        QTest::newRow("html-500") << QByteArray("<html>error</html>") << 500 << int(AnalysisHttpError::Http);
        auto object = QJsonDocument::fromJson(FakeAnalysisHttpServer::forecast()).object();
        object["sourceMode"] = "FIXTURE";
        QTest::newRow("fixture") << QJsonDocument(object).toJson() << 200 << int(AnalysisHttpError::UntrustedSource);
        object["sourceMode"] = "LIVE_ARTIFACT"; object["horizonHours"] = 6;
        QTest::newRow("wrong-context") << QJsonDocument(object).toJson() << 200 << int(AnalysisHttpError::Contract);
        object["status"] = "PARAM_INVALID";
        object["points"] = QJsonArray{};
        QTest::newRow("wrong-context-http-error") << QJsonDocument(object).toJson() << 400 << int(AnalysisHttpError::Contract);
    }
    void malformedAndUntrusted()
    {
        QFETCH(QByteArray, body); QFETCH(int, http); QFETCH(int, error);
        FakeAnalysisHttpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        server.respond = [=](const QUrl &) { return FakeAnalysisHttpServer::Response{body, http}; };
        AnalysisHttpClient client(server.baseUrl()); QSignalSpy spy(&client, &AnalysisHttpClient::forecastFinished);
        client.forecast(1); QTRY_COMPARE(spy.size(), 1);
        const auto result = qvariant_cast<AnalysisHttpResult>(spy.at(0).at(0));
        QCOMPARE(int(result.error), error); QVERIFY(!result.parsed.valid); QVERIFY(!result.message.isEmpty());
        QTest::qWait(40); QCOMPARE(server.requests.size(), 1);
    }
    void timeoutAndConnectionRefused()
    {
        FakeAnalysisHttpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        server.respond = [](const QUrl &) { return FakeAnalysisHttpServer::Response{FakeAnalysisHttpServer::forecast(), 200, 200}; };
        AnalysisHttpClient client(server.baseUrl()); client.setTimeoutMs(25);
        QSignalSpy spy(&client, &AnalysisHttpClient::forecastFinished);
        client.forecast(1); QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(qvariant_cast<AnalysisHttpResult>(spy.at(0).at(0)).error, AnalysisHttpError::Timeout);
        server.close(); spy.clear(); client.setTimeoutMs(800);
        client.forecast(1); QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(qvariant_cast<AnalysisHttpResult>(spy.at(0).at(0)).error, AnalysisHttpError::Network);
    }
    void lastRequestWinsAndCancelDiscardsLateResponses()
    {
        FakeAnalysisHttpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        server.respond = [](const QUrl &url) {
            const int hours = QUrlQuery(url).queryItemValue("horizon").toInt();
            return FakeAnalysisHttpServer::Response{FakeAnalysisHttpServer::forecast("OK", hours), 200, hours == 24 ? 5 : 150};
        };
        AnalysisHttpClient client(server.baseUrl()); QSignalSpy spy(&client, &AnalysisHttpClient::forecastFinished);
        client.forecast(1); QTRY_COMPARE(server.requests.size(), 1);
        client.forecast(6); QTRY_COMPARE(server.requests.size(), 2);
        const auto last = client.forecast(24); QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(qvariant_cast<AnalysisHttpResult>(spy.at(0).at(0)).generation, last);
        QCOMPARE(qvariant_cast<AnalysisHttpResult>(spy.at(0).at(0)).parsed.forecast.horizonHours, 24);
        QTest::qWait(180); QCOMPARE(spy.size(), 1);
        spy.clear(); client.forecast(1); QTRY_COMPARE(server.requests.size(), 4);
        client.cancel(); QTest::qWait(180); QVERIFY(spy.isEmpty());
    }
    void cancellationInvalidatesGeneration()
    {
        charge::analysis::AnalysisHttpClient client(QUrl("http://127.0.0.1:8001"));
        const auto generation = client.generation();
        client.cancel();
        QVERIFY(client.generation() > generation);
    }
};
QTEST_GUILESS_MAIN(AnalysisHttpClientTest)
#include "analysis_http_client_test.moc"
