#include <QtTest>
#include "fake_analysis_http_server.h"
#include "admin_analysis_page.h"
#include "pages/user_analysis_page.h"
#include "widgets/forecast_panel.h"
#include <QComboBox>
#include <QDateTimeEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QChartView>
#include <QChart>
#include <QLineSeries>
#include <QScrollArea>
#include <QScrollBar>
#include <memory>

using namespace charge::analysis;
class ForecastPagesHttpTest final : public QObject
{
    Q_OBJECT
private slots:
    void loadedPagesFitViewports_data()
    {
        QTest::addColumn<bool>("admin"); QTest::addColumn<QSize>("viewport");
        QTest::newRow("user-minimum") << false << QSize(360, 780);
        QTest::newRow("user-baseline") << false << QSize(480, 1040);
        QTest::newRow("admin-minimum") << true << QSize(1180, 720);
        QTest::newRow("admin-hd") << true << QSize(1366, 768);
        QTest::newRow("admin-full-hd") << true << QSize(1920, 1080);
    }
    void loadedPagesFitViewports()
    {
        QFETCH(bool, admin); QFETCH(QSize, viewport);
        FakeAnalysisHttpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        server.respond = [](const QUrl &) { return FakeAnalysisHttpServer::Response{FakeAnalysisHttpServer::forecast("OK", 24)}; };
        std::unique_ptr<Ui::PageScaffold> page;
        if (admin) {
            auto *p = new AdminAnalysisPage;
            p->findChild<QComboBox *>("adminAnalysisHorizon")->setCurrentIndex(2);
            p->setAnalysisBaseUrl(server.baseUrl()); page.reset(p);
        } else {
            auto *p = new UserAnalysisPage;
            p->findChild<QComboBox *>("userAnalysisHorizon")->setCurrentIndex(2);
            p->setAnalysisBaseUrl(server.baseUrl()); page.reset(p);
        }
        page->resize(viewport); page->show();
        auto *panel = page->findChild<Ui::ForecastPanel *>();
        QTRY_VERIFY(panel->displayedForecast().has_value());
        QCoreApplication::processEvents();
        QCOMPARE(page->size(), viewport);
        auto *scroll = page->findChild<QScrollArea *>("pageScroll");
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        QVERIFY(panel->width() <= scroll->viewport()->width());
        auto *table = panel->findChild<QTableWidget *>("forecastPoints");
        QCOMPARE(table->rowCount(), 24);
        QVERIFY(table->verticalScrollBar()->maximum() > 0);
    }
    void userHorizonOnlyLastWinsAndArrivalIsExplicitlyDisabled()
    {
        FakeAnalysisHttpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        server.respond = [](const QUrl &url) {
            const int h = QUrlQuery(url).queryItemValue("horizon").toInt();
            return FakeAnalysisHttpServer::Response{FakeAnalysisHttpServer::forecast("OK", h), 200, h == 24 ? 5 : 180};
        };
        UserAnalysisPage page; page.setAnalysisBaseUrl(server.baseUrl()); page.show();
        QTRY_COMPARE(server.requests.size(), 1);
        auto *arrival = page.findChild<QDateTimeEdit *>("userAnalysisArrivalTime");
        QVERIFY(arrival && !arrival->isEnabled());
        QVERIFY(arrival->toolTip().contains(QStringLiteral("尚未参与")));
        auto *horizon = page.findChild<QComboBox *>("userAnalysisHorizon");
        horizon->setCurrentIndex(1); QTRY_COMPARE(server.requests.size(), 2);
        horizon->setCurrentIndex(2); QTRY_COMPARE(server.requests.size(), 3);
        auto *table = page.findChild<QTableWidget *>("forecastPoints");
        QTRY_COMPARE(table->rowCount(), 24);
        QCOMPARE(table->item(23, 1)->text(), QString("24.25"));
        QTest::qWait(220); QCOMPARE(table->rowCount(), 24);
        for (const auto &request : server.requests) {
            QUrlQuery query{QUrl(request)};
            QVERIFY(!query.hasQueryItem("originTime")); QVERIFY(!query.hasQueryItem("arrivedAt"));
        }
        const auto *manual = page.findChild<QPushButton *>("manualStationSearchAction");
        QVERIFY(manual && manual->isEnabled());
    }
    void sixStates_data()
    {
        QTest::addColumn<QString>("state");
        for (const auto &state : {"OK", "NO_DATA", "EXPIRED", "MODEL_UNAVAILABLE", "DEPENDENCY_UNAVAILABLE", "PARAM_INVALID"})
            QTest::newRow(state) << QString(state);
    }
    void sixStates()
    {
        QFETCH(QString, state);
        FakeAnalysisHttpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        server.respond = [state](const QUrl &) { return FakeAnalysisHttpServer::Response{FakeAnalysisHttpServer::forecast(state)}; };
        AdminAnalysisPage page; page.setAnalysisBaseUrl(server.baseUrl()); page.show();
        auto *panel = page.findChild<Ui::ForecastPanel *>();
        auto *label = panel->findChild<QLabel *>("forecastState");
        QTRY_VERIFY(!server.requests.isEmpty());
        QTRY_VERIFY(!label->text().contains(QStringLiteral("正在获取")));
        QCOMPARE(page.analysisStatus(), *forecastStatusFromString(state));
        QCOMPARE(panel->findChild<QWidget *>("forecastBody")->isVisible(), state == "OK" || state == "EXPIRED");
        if (state == "PARAM_INVALID") QVERIFY(!page.retryAllowed());
        QTest::qWait(30); QCOMPARE(server.requests.size(), 1);
    }
    void expiredKeepsLastSuccessButStationChangeClearsIt()
    {
        FakeAnalysisHttpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        int requests = 0;
        server.respond = [&requests](const QUrl &) {
            auto body = FakeAnalysisHttpServer::forecast(++requests == 1 ? "OK" : "EXPIRED");
            if (requests > 1) {
                auto object = QJsonDocument::fromJson(body).object(); object["points"] = QJsonArray{};
                body = QJsonDocument(object).toJson();
            }
            return FakeAnalysisHttpServer::Response{body};
        };
        AdminAnalysisPage page; page.setAnalysisBaseUrl(server.baseUrl()); page.show();
        auto *panel = page.findChild<Ui::ForecastPanel *>();
        auto *table = panel->findChild<QTableWidget *>("forecastPoints");
        QTRY_COMPARE(table->rowCount(), 1);
        const auto old = table->item(0, 1)->text();
        panel->refresh(); QTRY_COMPARE(page.analysisStatus(), ForecastStatus::Expired);
        QCOMPARE(table->item(0, 1)->text(), old);
        QVERIFY(panel->findChild<QWidget *>("forecastBody")->isVisible());
        panel->findChild<QLineEdit *>("forecastStation")->setText("course:other");
        QCOMPARE(table->rowCount(), 0); QVERIFY(!panel->displayedForecast());
        QVERIFY(panel->findChild<QWidget *>("forecastBody")->isHidden());
    }
    void hidingAndSwitchingAdminSectionsCancelRequests()
    {
        FakeAnalysisHttpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        server.respond = [](const QUrl &) { return FakeAnalysisHttpServer::Response{FakeAnalysisHttpServer::forecast(), 200, 150}; };
        AdminAnalysisPage page; page.setAnalysisBaseUrl(server.baseUrl()); page.show();
        auto *panel = page.findChild<Ui::ForecastPanel *>();
        QTRY_COMPARE(server.requests.size(), 1);
        page.hide(); QTest::qWait(200); QVERIFY(!panel->displayedForecast());
        page.show(); QTRY_COMPARE(server.requests.size(), 2);
        auto *section = page.findChild<QComboBox *>("adminAnalysisSection");
        section->setCurrentIndex(1); QTest::qWait(200);
        QVERIFY(!panel->displayedForecast());
        QCOMPARE(page.analysisStatus(), ForecastStatus::DependencyUnavailable);
        section->setCurrentIndex(0); QTRY_COMPARE(server.requests.size(), 3);
        QTRY_VERIFY(panel->displayedForecast().has_value());
        QCOMPARE(page.analysisStatus(), ForecastStatus::Ok);
    }
    void userLeavingAndReenteringStartsFresh()
    {
        FakeAnalysisHttpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        int count = 0;
        server.respond = [&count](const QUrl &) { return FakeAnalysisHttpServer::Response{
            FakeAnalysisHttpServer::forecast(++count == 1 ? "PARAM_INVALID" : "OK"), 200, 120}; };
        UserAnalysisPage page; page.setAnalysisBaseUrl(server.baseUrl()); page.show();
        QTRY_COMPARE(server.requests.size(), 1);
        page.hide(); page.show(); QTRY_COMPARE(server.requests.size(), 2);
        QTRY_COMPARE(page.analysisStatus(), ForecastStatus::Ok);
        QTest::qWait(150); QCOMPARE(page.analysisStatus(), ForecastStatus::Ok);
    }
    void chartUsesServerTimesAndRemoteRejectsFixture()
    {
        FakeAnalysisHttpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        bool fixture = false;
        server.respond = [&fixture](const QUrl &) {
            auto object = QJsonDocument::fromJson(FakeAnalysisHttpServer::forecast()).object();
            if (fixture) object["sourceMode"] = "FIXTURE";
            return FakeAnalysisHttpServer::Response{QJsonDocument(object).toJson()};
        };
        AdminAnalysisPage page; page.setAnalysisBaseUrl(server.baseUrl()); page.show();
        auto *panel = page.findChild<Ui::ForecastPanel *>();
        QTRY_VERIFY(panel->displayedForecast().has_value());
        const auto forecast = *panel->displayedForecast();
        auto *chart = panel->findChild<QChartView *>()->chart();
        QCOMPARE(chart->series().size(), 1);
        const auto points = qobject_cast<QLineSeries *>(chart->series().first())->points();
        QCOMPARE(qint64(points.first().x()), forecast.points.first().targetStart.toMSecsSinceEpoch());
        QCOMPARE(qint64(points.last().x()), forecast.points.first().targetEnd.toMSecsSinceEpoch());
        QVERIFY(panel->findChild<QLabel *>("forecastMetadata")->text().contains(forecast.datasetVersion));
        fixture = true; panel->refresh();
        QTRY_COMPARE(page.analysisStatus(), ForecastStatus::DependencyUnavailable);
        QVERIFY(panel->findChild<QWidget *>("forecastBody")->isHidden());
    }
};
QTEST_MAIN(ForecastPagesHttpTest)
#include "forecast_pages_http_test.moc"
