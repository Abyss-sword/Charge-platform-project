#include <QtTest>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QToolButton>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QDir>
#include "shell/main_window.h"
#include "theme.h"
#include "widgets/page_state_widget.h"
#include "pages/flow_pages.h"
#include "pages/station_list_page.h"
#include "pages/active_order_page.h"
#include "shell/mobile_page_header.h"
#include "widgets/motion.h"
#include "widgets/status_badge.h"
#include "adapters/remote_station_provider.h"
#include "adapters/remote_user_order_provider.h"
#include "adapters/remote_user_account_provider.h"
#include <QSet>

class NavigationSmokeTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { Ui::Theme::apply(*qApp); }
    void createsMobileWindow()
    {
        UserMainWindow window;
        QCOMPARE(window.size(), QSize(480, 1040));
        QCOMPARE(window.minimumSize(), QSize(360, 780));
        QVERIFY(window.findChild<QWidget *>("mobilePageHeader"));
        auto *stack = window.findChild<QStackedWidget *>("primaryPages");
        QVERIFY(stack);
        QCOMPARE(stack->count(), 12);
        QCOMPARE(stack->currentWidget()->objectName(), QString("homePage"));
    }

    void remoteUiHasNoDevelopmentCopy()
    {
        RemoteStationProvider stations(QStringLiteral("127.0.0.1"),1);
        RemoteUserOrderProvider orders(QStringLiteral("127.0.0.1"),1);
        RemoteUserAccountProvider account(QStringLiteral("127.0.0.1"),1);
        UserMainWindow window({42,QStringLiteral("Alice"),false},&stations,&orders,&account);
        const QStringList forbidden{QStringLiteral("SIMULATED"), QStringLiteral("mock"),
                                    QStringLiteral("fixture"), QStringLiteral("教学"),
                                    QStringLiteral("演示数据"), QStringLiteral("界面预览"),
                                    QStringLiteral("测试数据"), QStringLiteral("preview"),
                                    QStringLiteral("fallback"), QStringLiteral("本轮未接入")};
        for (auto *label : window.findChildren<QLabel*>())
            for(const auto &word:forbidden)
                QVERIFY2(!label->text().contains(word, Qt::CaseInsensitive),
                         qPrintable(label->text()));
        for (auto *selector : window.findChildren<QComboBox *>(
                 QStringLiteral("mockStateSelector")))
            QVERIFY(selector->isHidden());
        auto *header=window.findChild<MobilePageHeader*>(QStringLiteral("mobilePageHeader")); QVERIFY(header);
        for(auto *badge:header->findChildren<Ui::StatusBadge*>()) QVERIFY(!badge->text().contains(QStringLiteral("未连接")));
        header->setNetworkIssue(true);
        auto *badge=header->findChild<Ui::StatusBadge*>(); QVERIFY(badge);
        QCOMPARE(badge->accessibleName(),QStringLiteral("网络异常"));
    }

    void offlineDemoMainWindowIsExplicitlyMarked()
    {
        UserMainWindow window;
        QVERIFY(window.windowTitle().contains(QStringLiteral("OFFLINE_DEMO")));
        QVERIFY(window.windowTitle().contains(QStringLiteral("SIMULATED")));
        QVERIFY(window.findChild<QComboBox *>(QStringLiteral("mockStateSelector")));
        bool hasVisibleMarker = false;
        for (auto *label : window.findChildren<QLabel *>()) {
            hasVisibleMarker |= label->text().contains(QStringLiteral("SIMULATED"));
        }
        QVERIFY(hasVisibleMarker);
    }

    void switchesWithoutRecreatingPages()
    {
        UserMainWindow window;
        window.show();
        auto *stack = window.findChild<QStackedWidget *>("primaryPages");
        auto *bar = window.findChild<QWidget *>("bottomNavigationBar");
        QVERIFY(stack);
        QVERIFY(bar);
        QCOMPARE(bar->findChildren<QToolButton *>().size(), 5);
        const QStringList names{"homePage", "nearbyStationsPage", "activeOrderPage",
                                "chargingHistoryPage", "accountPage"};
        QList<QPointer<QWidget>> pages;
        for (int i = 0; i < 5; ++i) pages.append(stack->widget(i));
        for (int round = 0; round < 3; ++round) {
            for (int i = 0; i < 5; ++i) {
                auto *button = bar->findChild<QToolButton *>(QString("nav%1").arg(i));
                QVERIFY(button);
                QTest::mouseClick(button, Qt::LeftButton);
                QCOMPARE(stack->currentIndex(), i);
                QCOMPARE(stack->currentWidget()->objectName(), names[i]);
                QVERIFY(button->isChecked());
                for (int j = 0; j < 5; ++j) QCOMPARE(stack->widget(j), pages[j].data());
            }
        }
    }

    void fitsTargetSizes_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("baseline") << QSize(480, 1040);
        QTest::newRow("minimum") << QSize(360, 780);
    }

    void fitsTargetSizes()
    {
        QFETCH(QSize, size);
        UserMainWindow window;
        window.resize(size);
        window.show();
        QTest::qWait(30);
        QCOMPARE(window.size(), size);
        auto *stack = window.findChild<QStackedWidget *>("primaryPages");
        auto *bar = window.findChild<QWidget *>("bottomNavigationBar");
        auto *header = window.findChild<QWidget *>("mobilePageHeader");
        QVERIFY(stack && bar && header);
        for (auto *fixed : {header, bar}) {
            QVERIFY(fixed->isVisible());
            QVERIFY(window.rect().contains(QRect(fixed->mapTo(&window, QPoint()), fixed->size())));
        }
        for (int i = 0; i < 5; ++i) {
            auto *button = bar->findChild<QToolButton *>(QString("nav%1").arg(i));
            QVERIFY(button);
            QVERIFY(bar->rect().contains(button->geometry()));
            QVERIFY(button->width() >= button->fontMetrics().horizontalAdvance(button->text()) + 8);
            QTest::mouseClick(button, Qt::LeftButton);
            QTest::qWait(10);
            auto *page = stack->currentWidget();
            auto *scroll = page->findChild<QScrollArea *>("pageScroll");
            QVERIFY(scroll);
            QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
            for (auto *action : page->findChildren<QPushButton *>("primaryAction")) {
                QVERIFY(action->isVisible());
                QVERIFY(window.rect().contains(QRect(action->mapTo(&window, QPoint()), action->size())));
                QVERIFY(action->width() >= action->fontMetrics().horizontalAdvance(action->text()) + 16);
            }
            const QString evidence = qEnvironmentVariable("UI_SCREENSHOT_DIR");
            if (!evidence.isEmpty()) {
                QVERIFY(QDir().mkpath(evidence));
                QVERIFY(window.grab().save(evidence + QString("/%1-%2x%3.png")
                    .arg(page->objectName()).arg(size.width()).arg(size.height())));
            }
            auto *state = page->findChild<QLabel *>("stateMessage");
            QVERIFY(state && state->isVisible());
            QVERIFY(window.rect().contains(QRect(state->mapTo(&window, QPoint()), state->size())));
        }
    }

    void destroysOwnedPages()
    {
        QPointer<QWidget> page;
        {
            UserMainWindow window;
            auto *stack = window.findChild<QStackedWidget *>("primaryPages");
            QVERIFY(stack);
            page = stack->widget(0);
            window.close();
        }
        QVERIFY(page.isNull());
    }

    void createsSecondaryPages()
    {
        UserMainWindow window;
        auto *stack = window.findChild<QStackedWidget *>("primaryPages");
        QVERIFY(stack);
        const QStringList names{"stationDetailPage", "navigationPage", "reservationConfirmPage",
                                "chargingStatusPage", "activeOrderRecoveryPage", "settlementPage",
                                "userAnalysisPage"};
        QCOMPARE(stack->count(), 5 + names.size());
        for (int i = 0; i < names.size(); ++i) {
            QVERIFY(stack->widget(5 + i));
            QCOMPARE(stack->widget(5 + i)->objectName(), names[i]);
        }
    }

    void secondaryPagesSupportMockStates()
    {
        UserMainWindow window;
        auto *stack = window.findChild<QStackedWidget *>("primaryPages");
        QVERIFY(stack);
        const QList<Ui::PageState> states{Ui::PageState::Loading, Ui::PageState::Normal,
            Ui::PageState::Empty, Ui::PageState::Error, Ui::PageState::Offline,
            Ui::PageState::Reconnecting, Ui::PageState::Degraded};
        for (int i = 5; i < 5 + 6; ++i) {
            auto *page = stack->widget(i);
            auto *selector = page->findChild<QComboBox *>("mockStateSelector");
            auto *state = page->findChild<Ui::PageStateWidget *>();
            QVERIFY(selector && state);
            for (const auto value : states) {
                selector->setCurrentIndex(selector->findData(static_cast<int>(value)));
                QCOMPARE(state->state(), value);
            }
        }
        QVERIFY(!stack->widget(PrimaryPageCount + static_cast<int>(SecondaryPage::Analysis))
                     ->findChild<QComboBox *>(QStringLiteral("mockStateSelector")));
    }

    void secondaryPagesFitTargetSizes_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("baseline") << QSize(480, 1040);
        QTest::newRow("minimum") << QSize(360, 780);
    }

    void secondaryPagesFitTargetSizes()
    {
        QFETCH(QSize, size);
        UserMainWindow window;
        window.resize(size);
        window.show();
        QTest::qWait(30);
        auto *stack = window.findChild<QStackedWidget *>("primaryPages");
        auto *header = window.findChild<MobilePageHeader *>("mobilePageHeader");
        QVERIFY(stack && header);
        for (int i = 5; i < stack->count(); ++i) {
            window.navigateTo(static_cast<SecondaryPage>(i - PrimaryPageCount));
            QTest::qWait(5);
            auto *page = stack->currentWidget();
            auto *scroll = page->findChild<QScrollArea *>("pageScroll");
            QVERIFY(scroll);
            QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
            QVERIFY(header->findChild<QToolButton *>()->isVisible());
            auto *state = page->findChild<QLabel *>("stateMessage");
            QVERIFY(state && state->isVisible());
            QVERIFY(window.rect().contains(QRect(state->mapTo(&window, QPoint()), state->size())));
            if (auto *action = page->findChild<QPushButton *>("primaryAction")) {
                QVERIFY(action->isVisible());
                QVERIFY(window.rect().contains(QRect(action->mapTo(&window, QPoint()), action->size())));
            }
        }
    }

    void followsBusinessFlowsAndBack()
    {
        UserMainWindow window;
        window.show();
        auto *stack = window.findChild<QStackedWidget *>("primaryPages");
        auto *bar = window.findChild<QWidget *>("bottomNavigationBar");
        QVERIFY(stack && bar);
        QTest::mouseClick(bar->findChild<QToolButton *>("nav1"), Qt::LeftButton);
        auto *stations = static_cast<NearbyStationsPage *>(stack->currentWidget());
        QVERIFY(stations);
        auto *detailAction = stations->findChild<QPushButton *>("stationDetailAction");
        QTRY_VERIFY(detailAction->isEnabled());
        QTest::mouseClick(detailAction, Qt::LeftButton);
        QCOMPARE(stack->currentWidget()->objectName(), QString("stationDetailPage"));
        auto *detail = static_cast<StationDetailPage *>(stack->currentWidget());
        QVERIFY(detail);
        auto *navigationAction = detail->findChild<QPushButton *>("secondaryAction1");
        auto *reservationAction = detail->findChild<QPushButton *>("secondaryAction2");
        QVERIFY(navigationAction && reservationAction);
        QTest::mouseClick(navigationAction, Qt::LeftButton);
        QCOMPARE(stack->currentWidget()->objectName(), QString("navigationPage"));
        auto *header = window.findChild<MobilePageHeader *>("mobilePageHeader");
        QVERIFY(header);
        QTest::mouseClick(header->findChild<QToolButton *>(), Qt::LeftButton);
        QCOMPARE(stack->currentWidget()->objectName(), QString("stationDetailPage"));
        QTest::mouseClick(reservationAction, Qt::LeftButton);
        QCOMPARE(stack->currentWidget()->objectName(), QString("reservationConfirmPage"));

        QTest::mouseClick(bar->findChild<QToolButton *>("nav2"), Qt::LeftButton);
        auto *orders = static_cast<ActiveOrderPage *>(stack->currentWidget());
        QVERIFY(orders);
        auto *chargingAction = orders->findChildren<QPushButton *>().value(0);
        QVERIFY(chargingAction);
        QTest::mouseClick(chargingAction, Qt::LeftButton);
        QCOMPARE(stack->currentWidget()->objectName(), QString("chargingStatusPage"));
        auto *charging = static_cast<ChargingStatusPage *>(stack->currentWidget());
        QVERIFY(charging);
        QTest::mouseClick(charging->primaryAction(), Qt::LeftButton);
        QCOMPARE(stack->currentWidget()->objectName(), QString("settlementPage"));
        QTest::mouseClick(header->findChild<QToolButton *>(), Qt::LeftButton);
        QCOMPARE(stack->currentWidget()->objectName(), QString("chargingStatusPage"));
    }
    void mapDegradedFallbackAndNavigationIds()
    {
        UserMainWindow window; window.show(); window.setCurrentPage(PrimaryPage::Stations); auto *stations=static_cast<NearbyStationsPage*>(window.findChild<QStackedWidget*>("primaryPages")->currentWidget()); auto *detailAction=stations->findChild<QPushButton*>("stationDetailAction"); QTRY_VERIFY(detailAction->isEnabled()); QTest::mouseClick(detailAction,Qt::LeftButton); auto *stack=window.findChild<QStackedWidget*>("primaryPages"); auto *detail=static_cast<StationDetailPage*>(stack->currentWidget()); QTest::mouseClick(detail->findChild<QPushButton*>("secondaryAction1"),Qt::LeftButton); auto *navigation=static_cast<NavigationPage*>(stack->currentWidget()); QVERIFY(navigation->findChild<QLabel*>("stateMessage")); QVERIFY(navigation->findChild<QPushButton*>("copyCoordinatesAction")); QVERIFY(navigation->findChild<QPushButton*>("continueReservationAction")); bool coordinates=false; for(auto *label:navigation->findChildren<QLabel*>()) coordinates |= label->text().contains("坐标"); QVERIFY(coordinates); navigation->setPageState(Ui::PageState::Degraded); QTest::mouseClick(navigation->findChild<QPushButton*>("continueReservationAction"),Qt::LeftButton); QCOMPARE(stack->currentWidget()->objectName(),QString("reservationConfirmPage")); QSet<QString> ids; for(auto *b:window.findChild<QWidget*>("bottomNavigationBar")->findChildren<QToolButton*>()){QVERIFY(!ids.contains(b->objectName()));ids.insert(b->objectName());}
    }
    void motionDisabledNavigationAndStates()
    {
        Ui::Motion::setEnabled(false); UserMainWindow user; auto *stack=user.findChild<QStackedWidget*>("primaryPages"); QList<QPointer<QWidget>> pages; for(int i=0;i<5;++i)pages.append(stack->widget(i)); for(int n=0;n<5;++n){user.setCurrentPage(static_cast<PrimaryPage>(n)); QCOMPARE(stack->currentIndex(),n);} user.navigateTo(SecondaryPage::StationDetail); user.goBack(); QCOMPARE(stack->currentIndex(),4); auto *state=stack->widget(0)->findChild<Ui::PageStateWidget*>(); for(auto s:{Ui::PageState::Normal,Ui::PageState::Offline,Ui::PageState::Reconnecting,Ui::PageState::Normal})state->setState(s); for(int i=0;i<5;++i)QCOMPARE(stack->widget(i),pages[i].data()); Ui::Motion::setEnabled(true);
    }
};
QTEST_MAIN(NavigationSmokeTest)
#include "navigation_smoke_test.moc"
