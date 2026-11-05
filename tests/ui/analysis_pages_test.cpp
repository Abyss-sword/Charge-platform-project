#include <QtTest>

#include "admin_main_window.h"
#include "admin_analysis_page.h"
#include "admin_mock_data_provider.h"
#include "pages/user_analysis_page.h"
#include "shell/main_window.h"
#include "widgets/analysis_state_presenter.h"

#include <QComboBox>
#include <QDateTimeEdit>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QToolButton>

using charge::analysis::ForecastStatus;

class AnalysisPagesTest final : public QObject
{
    Q_OBJECT

private slots:
    void userAnalysisPageHasFormalEmptySkeleton()
    {
        UserAnalysisPage page;
        QCOMPARE(page.objectName(), QStringLiteral("userAnalysisPage"));
        QCOMPARE(page.stateWidget()->state(), Ui::PageState::Error);
        QVERIFY(page.findChild<QComboBox *>(QStringLiteral("userAnalysisHorizon")));
        QVERIFY(page.findChild<QPushButton *>(QStringLiteral("manualStationSearchAction")));
        QVERIFY(page.findChild<QWidget *>(QStringLiteral("recommendationPanel")));
        verifyNoFakeResults(page);
        QVERIFY(page.findChildren<QNetworkAccessManager *>().isEmpty());
    }

    void userParametersStayAvailableAndResetTheResultContext()
    {
        UserAnalysisPage page;
        page.show();
        auto *arrival = page.findChild<QDateTimeEdit *>(
            QStringLiteral("userAnalysisArrivalTime"));
        auto *horizon = page.findChild<QComboBox *>(QStringLiteral("userAnalysisHorizon"));
        QVERIFY(arrival && arrival->isEnabled() && !arrival->isHidden());
        QVERIFY(horizon && horizon->isEnabled() && !horizon->isHidden());
        QCOMPARE(horizon->count(), 3);

        const QList<int> hours{6, 24, 1};
        for (int hoursValue : hours) {
            page.setAnalysisStatus(ForecastStatus::ParamInvalid);
            horizon->setCurrentIndex(horizon->findData(hoursValue));
            QCOMPARE(page.selectedHorizonHours(), hoursValue);
            QCOMPARE(page.analysisStatus(), ForecastStatus::DependencyUnavailable);
            QCOMPARE(page.stateWidget()->message(), dependencyUnavailableMessage());
            verifyNoFakeResults(page);
        }

        page.setAnalysisStatus(ForecastStatus::ModelUnavailable);
        arrival->setDateTime(arrival->dateTime().addSecs(3600));
        QCOMPARE(page.analysisStatus(), ForecastStatus::DependencyUnavailable);
        QCOMPARE(page.stateWidget()->message(), dependencyUnavailableMessage());
    }

    void adminAnalysisPageHasFormalEmptySkeleton()
    {
        AdminAnalysisPage page;
        QCOMPARE(page.objectName(), QStringLiteral("adminAnalysisPage"));
        QCOMPARE(page.stateWidget()->state(), Ui::PageState::Error);
        auto *horizon = page.findChild<QComboBox *>(QStringLiteral("adminAnalysisHorizon"));
        QVERIFY(horizon);
        QCOMPARE(horizon->count(), 3);
        QCOMPARE(horizon->itemData(0).toInt(), 1);
        QCOMPARE(horizon->itemData(1).toInt(), 6);
        QCOMPARE(horizon->itemData(2).toInt(), 24);
        QVERIFY(page.findChild<QWidget *>(QStringLiteral("peakPanel")));
        verifyNoFakeResults(page);
        QVERIFY(page.findChildren<QNetworkAccessManager *>().isEmpty());
    }

    void adminSectionsAreDistinctAndSwitchable()
    {
        AdminAnalysisPage page;
        page.show();
        auto *section = page.findChild<QComboBox *>(QStringLiteral("adminAnalysisSection"));
        auto *sections = page.findChild<QStackedWidget *>(
            QStringLiteral("adminAnalysisSections"));
        QVERIFY(section && section->isEnabled() && !section->isHidden());
        QVERIFY(sections);
        QCOMPARE(section->count(), 4);
        QCOMPARE(sections->count(), 4);

        const QStringList containers{
            QStringLiteral("adminLoadForecastContainer"),
            QStringLiteral("adminFreePilesForecastContainer"),
            QStringLiteral("adminPeakForecastContainer"),
            QStringLiteral("adminForecastWarningContainer")};
        for (const int index : QList<int>{1, 2, 3, 0}) {
            QVERIFY(page.findChild<QWidget *>(containers.at(index)));
            page.setAnalysisStatus(ForecastStatus::ParamInvalid);
            section->setCurrentIndex(index);
            QCOMPARE(sections->currentIndex(), index);
            QCOMPARE(page.currentSection(), index);
            QCOMPARE(page.analysisStatus(), ForecastStatus::DependencyUnavailable);
        }

        auto *risk = page.findChild<QLabel *>(QStringLiteral("adminForecastRiskLabel"));
        auto *fault = page.findChild<QLabel *>(QStringLiteral("adminActualFaultLabel"));
        QVERIFY(risk && fault);
        QVERIFY(risk->text().contains(QStringLiteral("预测风险")));
        QVERIFY(fault->text().contains(QStringLiteral("已发生设备故障")));
        QVERIFY(risk->property("tone") != fault->property("tone"));
        verifyNoFakeResults(page);
    }

    void bothPagesRenderEveryForecastStatus_data()
    {
        QTest::addColumn<ForecastStatus>("status");
        QTest::newRow("ok") << ForecastStatus::Ok;
        QTest::newRow("no-data") << ForecastStatus::NoData;
        QTest::newRow("expired") << ForecastStatus::Expired;
        QTest::newRow("model-unavailable") << ForecastStatus::ModelUnavailable;
        QTest::newRow("dependency-unavailable") << ForecastStatus::DependencyUnavailable;
        QTest::newRow("param-invalid") << ForecastStatus::ParamInvalid;
    }

    void bothPagesRenderEveryForecastStatus()
    {
        QFETCH(ForecastStatus, status);
        UserAnalysisPage userPage;
        AdminAnalysisPage adminPage;
        userPage.setAnalysisStatus(status);
        adminPage.setAnalysisStatus(status);
        const auto expected = Ui::analysisStatePresentation(status);

        QCOMPARE(userPage.retryAllowed(), expected.retryAllowed);
        QCOMPARE(adminPage.retryAllowed(), expected.retryAllowed);
        QCOMPARE(userPage.analysisStatus(), status);
        QCOMPARE(adminPage.analysisStatus(), status);

        for (Ui::PageScaffold *page : QList<Ui::PageScaffold *>{&userPage, &adminPage}) {
            QCOMPARE(page->stateWidget()->state(), expected.pageState);
            QVERIFY(page->stateWidget()->message().contains(expected.title));
            QVERIFY(page->stateWidget()->message().contains(expected.description));
            auto *content = page->findChild<QWidget *>(QStringLiteral("scrollContent"));
            QVERIFY(content);
            QCOMPARE(content->isHidden(), !expected.showContent);
        }
    }

    void reenteringPagesClearsAnOldErrorState()
    {
        UserAnalysisPage userPage;
        userPage.show();
        userPage.setAnalysisStatus(ForecastStatus::ParamInvalid);
        userPage.hide();
        userPage.show();
        QCoreApplication::processEvents();
        QCOMPARE(userPage.analysisStatus(), ForecastStatus::DependencyUnavailable);
        QCOMPARE(userPage.stateWidget()->message(), dependencyUnavailableMessage());

        AdminAnalysisPage adminPage;
        adminPage.show();
        adminPage.setAnalysisStatus(ForecastStatus::ModelUnavailable);
        adminPage.hide();
        adminPage.show();
        QCoreApplication::processEvents();
        QCOMPARE(adminPage.analysisStatus(), ForecastStatus::DependencyUnavailable);
        QCOMPARE(adminPage.stateWidget()->message(), dependencyUnavailableMessage());
    }

    void supportedViewportSizesDoNotOverflow_data()
    {
        QTest::addColumn<bool>("admin");
        QTest::addColumn<QSize>("size");
        QTest::newRow("user-minimum") << false << QSize(360, 780);
        QTest::newRow("user-baseline") << false << QSize(480, 1040);
        QTest::newRow("admin-minimum") << true << QSize(1180, 720);
        QTest::newRow("admin-stage2") << true << QSize(1366, 768);
        QTest::newRow("admin-full-hd") << true << QSize(1920, 1080);
    }

    void supportedViewportSizesDoNotOverflow()
    {
        QFETCH(bool, admin);
        QFETCH(QSize, size);
        if (admin) {
            AdminMainWindow window;
            window.resize(size);
            window.show();
            window.setCurrentPage(AdminPage::Analysis);
            QCoreApplication::processEvents();
            auto *page = window.findChild<AdminAnalysisPage *>(
                QStringLiteral("adminAnalysisPage"));
            QVERIFY(page);
            page->setAnalysisStatus(ForecastStatus::Ok);
            verifyNoHorizontalOverflow(*page);
            auto *selection = page->findChild<QWidget *>(
                QStringLiteral("adminAnalysisSelection"));
            auto *risk = page->findChild<QLabel *>(QStringLiteral("adminForecastRiskLabel"));
            auto *fault = page->findChild<QLabel *>(QStringLiteral("adminActualFaultLabel"));
            QVERIFY(selection && risk && fault);
            QVERIFY(!risk->geometry().intersects(fault->geometry()));
            QVERIFY(risk->geometry().right() <= selection->contentsRect().right());
            QVERIFY(fault->geometry().right() <= selection->contentsRect().right());
        } else {
            UserMainWindow window;
            window.resize(size);
            window.show();
            window.navigateTo(SecondaryPage::Analysis);
            QCoreApplication::processEvents();
            auto *page = window.findChild<UserAnalysisPage *>(
                QStringLiteral("userAnalysisPage"));
            QVERIFY(page);
            page->setAnalysisStatus(ForecastStatus::Ok);
            verifyNoHorizontalOverflow(*page);
            auto *arrival = page->findChild<QDateTimeEdit *>(
                QStringLiteral("userAnalysisArrivalTime"));
            auto *horizon = page->findChild<QComboBox *>(
                QStringLiteral("userAnalysisHorizon"));
            QVERIFY(arrival && horizon);
            QVERIFY(!arrival->geometry().intersects(horizon->geometry()));
        }
    }

    void userNavigationOpensAnalysisAndKeepsManualSearch()
    {
        UserMainWindow window;
        window.show();
        window.setCurrentPage(PrimaryPage::Stations);
        auto *entry = window.findChild<QPushButton *>(QStringLiteral("userAnalysisEntryAction"));
        QVERIFY(entry);
        QTest::mouseClick(entry, Qt::LeftButton);
        auto *stack = window.findChild<QStackedWidget *>(QStringLiteral("primaryPages"));
        QVERIFY(stack);
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("userAnalysisPage"));
        auto *analysis = qobject_cast<UserAnalysisPage *>(stack->currentWidget());
        QVERIFY(analysis);
        analysis->setAnalysisStatus(ForecastStatus::ParamInvalid);
        auto *manual = stack->currentWidget()->findChild<QPushButton *>(
            QStringLiteral("manualStationSearchAction"));
        QVERIFY(manual && manual->isEnabled());
        QTest::mouseClick(manual, Qt::LeftButton);
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("nearbyStationsPage"));
        QTest::mouseClick(entry, Qt::LeftButton);
        QCOMPARE(stack->currentWidget(), analysis);
        QCOMPARE(analysis->analysisStatus(), ForecastStatus::DependencyUnavailable);
    }

    void adminNavigationOpensPredictionWarnings()
    {
        AdminMainWindow window;
        window.show();
        auto *button = window.findChild<QToolButton *>(QStringLiteral("adminNav8"));
        QVERIFY(button);
        QTest::mouseClick(button, Qt::LeftButton);
        auto *stack = window.findChild<QStackedWidget *>(QStringLiteral("adminPages"));
        QVERIFY(stack);
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("adminAnalysisPage"));
        verifyNoFakeResults(*stack->currentWidget());
    }

private:
    static void verifyNoHorizontalOverflow(Ui::PageScaffold &page)
    {
        QCoreApplication::processEvents();
        auto *scroll = page.findChild<QScrollArea *>(QStringLiteral("pageScroll"));
        QVERIFY(scroll);
        QCOMPARE(scroll->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
    }

    static QString dependencyUnavailableMessage()
    {
        const auto state = Ui::analysisStatePresentation(
            ForecastStatus::DependencyUnavailable);
        return state.title + QLatin1Char('\n') + state.description;
    }

    static void verifyNoFakeResults(const QWidget &page)
    {
        QString visibleCopy;
        for (const auto *label : page.findChildren<QLabel *>()) {
            visibleCopy += label->text() + QLatin1Char('\n');
        }
        const QStringList forbidden{
            QStringLiteral("SIMULATED"), QStringLiteral("mock"),
            QStringLiteral("fixture"), QStringLiteral("模拟预测"),
            QStringLiteral("教学"), QStringLiteral("演示数据"),
            QStringLiteral("测试数据"), QStringLiteral("固定推荐"),
            QStringLiteral("preview"), QStringLiteral("FastAPI")};
        for (const QString &word : forbidden) {
            QVERIFY2(!visibleCopy.contains(word, Qt::CaseInsensitive), qPrintable(visibleCopy));
        }
    }
};

QTEST_MAIN(AnalysisPagesTest)
#include "analysis_pages_test.moc"
