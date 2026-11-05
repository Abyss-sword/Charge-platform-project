#include <QtTest>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QToolButton>

#include <utility>

#include "adapters/user_login_provider.h"
#include "adapters/user_account_provider.h"
#include "adapters/user_order_provider.h"
#include "adapters/mock_user_login_provider.h"
#include "pages/user_login_page.h"
#include "shell/main_window.h"
#include "shell/user_application_flow.h"
#include "theme.h"
#include "widgets/page_state_widget.h"

class FakeLoginProvider final : public IUserLoginProvider
{
    Q_OBJECT
public:
    using IUserLoginProvider::IUserLoginProvider;

    void submit(const LoginRequest &request) override
    {
        ++submissionCount;
        lastPhone = request.phone;
    }

    void complete(LoginResultKind kind, const QString &message = {})
    {
        emit loginFinished(LoginResult{kind, message, 0, {}, false, {}, {}});
    }

    void complete(const LoginResult &result)
    {
        emit loginFinished(result);
    }

    int submissionCount = 0;
    QString lastPhone;
};

class FakeAccountProvider final : public IUserAccountProvider
{
public:
    using IUserAccountProvider::IUserAccountProvider;
    void setUserToken(const QString &token) override { userToken = token; }
    void queryProfile(qint64 userId) override
    {
        ++profileQueries;
        pendingUserId = userId;
        if (deferProfile) return;
        completeProfile();
    }
    void completeProfile()
    {
        emit profileFinished({UserAccountResultKind::Success, {}, QStringLiteral("OK"),
                              {pendingUserId, QStringLiteral("138****0000"),
                               QStringLiteral("Alice"), QStringLiteral("88.00"),
                               QStringLiteral("NORMAL")}});
    }
    void queryBalance(qint64 userId) override
    {
        ++balanceQueries;
        emit balanceFinished({UserAccountResultKind::Success, {}, QStringLiteral("OK"),
                              userId, QStringLiteral("88.00")});
    }
    int profileQueries = 0;
    int balanceQueries = 0;
    qint64 pendingUserId = 0;
    bool deferProfile = false;
    QString userToken;
};

class FakeRecoveryOrderProvider final : public IUserOrderProvider
{
public:
    explicit FakeRecoveryOrderProvider(QString status, QObject *parent = nullptr)
        : IUserOrderProvider(parent), status_(std::move(status)) {}
    void reserve(qint64, qint64) override {}
    void start(const QString &) override {}
    void stop(const QString &) override {}
    void cancel(const QString &) override {}
    void pay(const QString &) override {}
    void recharge(qint64, const QString &) override {}
    void setUserToken(const QString &token) override { userToken = token; }
    void queryActive(qint64 userId) override
    {
        ++activeQueries;
        UserOrderQueryResult result;
        result.kind = UserOrderResultKind::Success;
        result.query = UserOrderQueryKind::Active;
        result.errorCode = QStringLiteral("OK");
        result.hasOrder = !status_.isEmpty();
        if (result.hasOrder)
            result.order = snapshot(userId);
        emit queryFinished(result);
    }
    void queryDetail(const QString &) override
    {
        UserOrderQueryResult result;
        result.kind = UserOrderResultKind::Success;
        result.query = UserOrderQueryKind::Detail;
        result.errorCode = QStringLiteral("OK");
        result.hasOrder = true;
        result.order = snapshot(42);
        emit queryFinished(result);
    }
    int activeQueries = 0;
    QString userToken;
private:
    UserOrderSnapshot snapshot(qint64 userId) const
    {
        return {QStringLiteral("ORD-RECOVERY"), userId, 101, status_,
                QStringLiteral("1.25"), QStringLiteral("1.20"),
                QStringLiteral("1.50"), {}, QStringLiteral("2026-09-09T10:00:00Z"),
                {}, QStringLiteral("2026-09-09T09:50:00Z")};
    }
    QString status_;
};

class UserLoginTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { Ui::Theme::apply(*qApp); }

    void createsMobileLoginPage()
    {
        FakeLoginProvider provider;
        UserLoginPage page(&provider);
        QCOMPARE(page.size(), QSize(480, 1040));
        QCOMPARE(page.minimumSize(), QSize(360, 780));
        page.resize(360, 780);
        page.show();
        QTest::qWait(20);

        auto *phone = page.findChild<QLineEdit *>("userLoginPhoneEdit");
        auto *submit = page.findChild<QPushButton *>("userLoginSubmitButton");
        auto *retry = page.findChild<QPushButton *>("userLoginRetryButton");
        auto *status = page.findChild<Ui::PageStateWidget *>("userLoginConnectionStatus");
        QVERIFY(phone && submit && retry && status);
        QVERIFY(page.rect().contains(QRect(phone->mapTo(&page, QPoint()), phone->size())));
        QVERIFY(page.rect().contains(QRect(submit->mapTo(&page, QPoint()), submit->size())));
        QCOMPARE(page.findChild<QScrollArea *>("userLoginScroll")->horizontalScrollBar()->maximum(), 0);
    }

    void loginPageFitsSupportedSizes_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("minimum") << QSize(360, 780);
        QTest::newRow("baseline") << QSize(480, 1040);
    }

    void loginPageFitsSupportedSizes()
    {
        QFETCH(QSize, size);
        FakeLoginProvider provider;
        UserLoginPage page(&provider);
        page.resize(size);
        page.show();
        QCoreApplication::processEvents();
        QCOMPARE(page.size(), size);
        auto *scroll = page.findChild<QScrollArea *>(QStringLiteral("userLoginScroll"));
        auto *submit = page.findChild<QPushButton *>(QStringLiteral("userLoginSubmitButton"));
        QVERIFY(scroll && submit && submit->isVisible());
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        QVERIFY(page.rect().contains(QRect(submit->mapTo(&page, QPoint()), submit->size())));
    }

    void offlineDemoModeIsExplicitAndRemoteModeIsFormal()
    {
        FakeLoginProvider provider;
        UserLoginPage page(&provider);
        auto *banner = page.findChild<QLabel *>(QStringLiteral("offlineDemoBanner"));
        QVERIFY(banner);
        QVERIFY(banner->isHidden());
        QVERIFY(!page.windowTitle().contains(QStringLiteral("OFFLINE_DEMO")));

        page.setOfflineDemoMode(true);
        QVERIFY(!banner->isHidden());
        QVERIFY(banner->text().contains(QStringLiteral("OFFLINE_DEMO")));
        QVERIFY(banner->text().contains(QStringLiteral("不连接正式服务")));
        QVERIFY(page.windowTitle().contains(QStringLiteral("OFFLINE_DEMO")));

        page.setOfflineDemoMode(false);
        QVERIFY(banner->isHidden());
        QVERIFY(!page.windowTitle().contains(QStringLiteral("OFFLINE_DEMO")));
    }

    void rejectsEmptyAndInvalidPhone()
    {
        FakeLoginProvider provider;
        UserLoginPage page(&provider);
        auto *phone = page.findChild<QLineEdit *>("userLoginPhoneEdit");
        auto *submit = page.findChild<QPushButton *>("userLoginSubmitButton");
        QTest::mouseClick(submit, Qt::LeftButton);
        QCOMPARE(provider.submissionCount, 0);
        phone->setText(QStringLiteral("12345"));
        QTest::mouseClick(submit, Qt::LeftButton);
        QCOMPARE(provider.submissionCount, 0);
        QCOMPARE(phone->property("invalid").toBool(), true);
    }

    void validatesMainlandPhoneBoundary_data()
    {
        QTest::addColumn<QString>("phone");
        QTest::addColumn<bool>("valid");
        QTest::newRow("valid-138") << QStringLiteral("13812345678") << true;
        QTest::newRow("valid-199") << QStringLiteral("19912345678") << true;
        QTest::newRow("invalid-128") << QStringLiteral("12812345678") << false;
        QTest::newRow("invalid-100") << QStringLiteral("10012345678") << false;
        QTest::newRow("too-short") << QStringLiteral("1381234567") << false;
        QTest::newRow("too-long") << QStringLiteral("138123456789") << false;
        QTest::newRow("non-numeric") << QStringLiteral("abcdefghijk") << false;
    }

    void validatesMainlandPhoneBoundary()
    {
        QFETCH(QString, phone);
        QFETCH(bool, valid);
        FakeLoginProvider provider;
        UserLoginPage page(&provider);
        page.findChild<QLineEdit *>("userLoginPhoneEdit")->setText(phone);
        QTest::mouseClick(page.findChild<QPushButton *>("userLoginSubmitButton"),
                          Qt::LeftButton);
        QCOMPARE(provider.submissionCount, valid ? 1 : 0);
    }

    void inputRetainsDigitsOnly()
    {
        FakeLoginProvider provider;
        UserLoginPage page(&provider);
        auto *phone = page.findChild<QLineEdit *>("userLoginPhoneEdit");
        phone->setFocus();
        QTest::keyClicks(phone, QStringLiteral("138a-0013x8000"));
        QCOMPARE(phone->text(), QStringLiteral("13800138000"));
    }

    void submitsValidPhoneAndShowsLoading()
    {
        FakeLoginProvider provider;
        UserLoginPage page(&provider);
        auto *phone = page.findChild<QLineEdit *>("userLoginPhoneEdit");
        auto *submit = page.findChild<QPushButton *>("userLoginSubmitButton");
        auto *state = page.findChild<Ui::PageStateWidget *>("userLoginConnectionStatus");
        phone->setText(QStringLiteral("13800138000"));
        QTest::mouseClick(submit, Qt::LeftButton);
        QCOMPARE(provider.submissionCount, 1);
        QCOMPARE(provider.lastPhone, QStringLiteral("13800138000"));
        QCOMPARE(state->state(), Ui::PageState::Loading);
        QVERIFY(!submit->isEnabled());
    }

    void preservesPhoneAfterErrorAndOffline()
    {
        FakeLoginProvider provider;
        UserLoginPage page(&provider);
        auto *phone = page.findChild<QLineEdit *>("userLoginPhoneEdit");
        auto *submit = page.findChild<QPushButton *>("userLoginSubmitButton");
        auto *retry = page.findChild<QPushButton *>("userLoginRetryButton");
        auto *state = page.findChild<Ui::PageStateWidget *>("userLoginConnectionStatus");
        phone->setText(QStringLiteral("13800138000"));
        QTest::mouseClick(submit, Qt::LeftButton);
        provider.complete(LoginResultKind::Error, QStringLiteral("模拟服务错误"));
        QCOMPARE(phone->text(), QStringLiteral("13800138000"));
        QCOMPARE(state->state(), Ui::PageState::Error);
        QVERIFY(submit->isEnabled());

        QTest::mouseClick(submit, Qt::LeftButton);
        provider.complete(LoginResultKind::Offline, QStringLiteral("模拟数据源离线"));
        QCOMPARE(phone->text(), QStringLiteral("13800138000"));
        QCOMPARE(state->state(), Ui::PageState::Offline);
        QVERIFY(retry->isVisibleTo(&page));
    }

    void retriesAndRecoversToSuccess()
    {
        FakeLoginProvider provider;
        UserLoginPage page(&provider);
        page.show();
        auto *phone = page.findChild<QLineEdit *>("userLoginPhoneEdit");
        auto *submit = page.findChild<QPushButton *>("userLoginSubmitButton");
        auto *retry = page.findChild<QPushButton *>("userLoginRetryButton");
        phone->setText(QStringLiteral("13800138000"));
        QTest::mouseClick(submit, Qt::LeftButton);
        provider.complete(LoginResultKind::Offline, QStringLiteral("离线"));
        QSignalSpy success(&page, &UserLoginPage::loginSucceeded);
        QTest::mouseClick(retry, Qt::LeftButton);
        QCOMPARE(provider.submissionCount, 2);
        provider.complete(LoginResultKind::Success, QStringLiteral("登录模拟成功"));
        QCOMPARE(success.count(), 1);
        QCOMPARE(page.findChild<Ui::PageStateWidget *>("userLoginConnectionStatus")->state(),
                 Ui::PageState::Normal);
    }

    void successfulFlowShowsOneMainWindow()
    {
        FakeLoginProvider provider;
        UserApplicationFlow flow(&provider);
        flow.start();
        auto *login = flow.loginPage();
        QVERIFY(login && login->isVisible());
        login->findChild<QLineEdit *>("userLoginPhoneEdit")->setText(QStringLiteral("13800138000"));
        QTest::mouseClick(login->findChild<QPushButton *>("userLoginSubmitButton"), Qt::LeftButton);
        provider.complete(LoginResult{
            LoginResultKind::Success,
            QStringLiteral("登录成功"),
            42,
            QStringLiteral("Alice"),
            true,
            QStringLiteral("flow-token"),
            QStringLiteral("2026-09-15T18:00:00Z")
        });
        QVERIFY(flow.mainWindow());
        QVERIFY(flow.mainWindow()->isVisible());
        QVERIFY(!login->isVisible());
        auto *first = flow.mainWindow();
        QCOMPARE(first->userContext().userId, qint64(42));
        QCOMPARE(first->userContext().nickname, QStringLiteral("Alice"));
        QVERIFY(first->userContext().restricted);
        provider.complete(LoginResult{
            LoginResultKind::Success,
            QStringLiteral("重复结果"),
            99,
            QStringLiteral("Bob"),
            false,
            {},
            {}
        });
        QCOMPARE(flow.mainWindow(), first);
        QCOMPARE(first->userContext().userId, qint64(42));
        auto *stack = first->findChild<QStackedWidget *>("primaryPages");
        QVERIFY(stack);
        QCOMPARE(stack->count(), 12);
        QCOMPARE(first->findChild<QWidget *>("bottomNavigationBar")->findChildren<QToolButton *>().size(), 5);
    }


    void mockProviderMapsExistingScenarios()
    {
        MockUserLoginProvider provider;
        QSignalSpy results(&provider, &IUserLoginProvider::loginFinished);
        const LoginRequest request{QStringLiteral("13800138000")};

        provider.submit(request);
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(qvariant_cast<LoginResult>(results.takeFirst().at(0)).kind,
                 LoginResultKind::Success);

        provider.setScenario(MockScenario::ServiceError);
        provider.submit(request);
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(qvariant_cast<LoginResult>(results.takeFirst().at(0)).kind,
                 LoginResultKind::Error);

        provider.setScenario(MockScenario::Timeout);
        provider.submit(request);
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(qvariant_cast<LoginResult>(results.takeFirst().at(0)).kind,
                 LoginResultKind::Error);

        provider.setScenario(MockScenario::Offline);
        provider.submit(request);
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(qvariant_cast<LoginResult>(results.takeFirst().at(0)).kind,
                 LoginResultKind::Offline);

        provider.setScenario(MockScenario::Normal);
        provider.submit(request);
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(qvariant_cast<LoginResult>(results.takeFirst().at(0)).kind,
                 LoginResultKind::Success);
    }

    void logoutClearsSessionAndReloginUsesNewContext()
    {
        FakeLoginProvider provider;
        UserApplicationFlow flow(&provider);
        flow.start();
        auto loginAs = [&provider, &flow](const QString &phone, qint64 id,
                                          const QString &name) {
            auto *login = flow.loginPage();
            login->findChild<QLineEdit *>("userLoginPhoneEdit")->setText(phone);
            login->findChild<QPushButton *>("userLoginSubmitButton")->click();
            provider.complete({LoginResultKind::Success, QStringLiteral("登录成功"),
                               id, name, false, {}, {}});
        };
        loginAs(QStringLiteral("13800138000"), 42, QStringLiteral("Alice"));
        QPointer<UserMainWindow> first = flow.mainWindow();
        QVERIFY(first);
        first->setCurrentPage(PrimaryPage::Account);
        auto *logout = first->findChild<QPushButton *>(QStringLiteral("logoutAction"));
        QVERIFY(logout);
        logout->click();
        QTRY_VERIFY(flow.mainWindow() == nullptr);
        QTRY_VERIFY(first.isNull());
        QVERIFY(flow.loginPage()->isVisible());
        QVERIFY(flow.loginPage()->findChild<QLineEdit *>("userLoginPhoneEdit")->text().isEmpty());

        loginAs(QStringLiteral("13900139000"), 84, QStringLiteral("Bob"));
        QVERIFY(flow.mainWindow());
        QCOMPARE(flow.mainWindow()->userContext().userId, qint64(84));
        QCOMPARE(flow.mainWindow()->userContext().nickname, QStringLiteral("Bob"));
    }

    void loginRestoresProfileBalanceAndActiveOrderAfterRestart()
    {
        for (int launch = 0; launch < 2; ++launch) {
            FakeLoginProvider loginProvider;
            FakeAccountProvider accountProvider;
            FakeRecoveryOrderProvider orderProvider(QStringLiteral("CHARGING"));
            UserApplicationFlow flow(&loginProvider, nullptr, &orderProvider,
                                     &accountProvider);
            flow.start();
            auto *phone = flow.loginPage()->findChild<QLineEdit *>(
                QStringLiteral("userLoginPhoneEdit"));
            phone->setText(QStringLiteral("13800138000"));
            flow.loginPage()->findChild<QPushButton *>(
                QStringLiteral("userLoginSubmitButton"))->click();
            loginProvider.complete({LoginResultKind::Success, QStringLiteral("登录成功"),
                                    42, QStringLiteral("Alice"), false,
                                    QStringLiteral("session-token"),
                                    QStringLiteral("2026-09-15T18:00:00Z")});

            QVERIFY(flow.mainWindow());
            QCOMPARE(accountProvider.userToken, QStringLiteral("session-token"));
            QCOMPARE(orderProvider.userToken, QStringLiteral("session-token"));
            QCOMPARE(accountProvider.profileQueries, 1);
            QCOMPARE(accountProvider.balanceQueries, 1);
            QCOMPARE(orderProvider.activeQueries, 1);
            auto *stack = flow.mainWindow()->findChild<QStackedWidget *>(
                QStringLiteral("primaryPages"));
            QVERIFY(stack);
            QCOMPARE(stack->currentWidget()->objectName(),
                     QStringLiteral("chargingStatusPage"));
        }
    }

    void loginWithReservationOpensActiveOrderPage()
    {
        FakeLoginProvider loginProvider;
        FakeAccountProvider accountProvider;
        FakeRecoveryOrderProvider orderProvider(QStringLiteral("RESERVED"));
        UserApplicationFlow flow(&loginProvider, nullptr, &orderProvider, &accountProvider);
        flow.start();
        flow.loginPage()->findChild<QLineEdit *>(QStringLiteral("userLoginPhoneEdit"))
            ->setText(QStringLiteral("13800138000"));
        flow.loginPage()->findChild<QPushButton *>(QStringLiteral("userLoginSubmitButton"))
            ->click();
        loginProvider.complete({LoginResultKind::Success, {}, 42,
                                QStringLiteral("Alice"), false, {}, {}});
        QVERIFY(flow.mainWindow());
        auto *stack = flow.mainWindow()->findChild<QStackedWidget *>(
            QStringLiteral("primaryPages"));
        QCOMPARE(stack->currentIndex(), int(PrimaryPage::Orders));
    }

    void loginWithoutActiveOrderStaysOnHomeAndSequencesAccountQueries()
    {
        FakeLoginProvider loginProvider;
        FakeAccountProvider accountProvider;
        accountProvider.deferProfile = true;
        FakeRecoveryOrderProvider orderProvider(QString{});
        UserApplicationFlow flow(&loginProvider, nullptr, &orderProvider, &accountProvider);
        flow.start();
        flow.loginPage()->findChild<QLineEdit *>(QStringLiteral("userLoginPhoneEdit"))
            ->setText(QStringLiteral("13800138000"));
        flow.loginPage()->findChild<QPushButton *>(QStringLiteral("userLoginSubmitButton"))
            ->click();
        loginProvider.complete({LoginResultKind::Success, {}, 42,
                                QStringLiteral("Alice"), false, {}, {}});

        QVERIFY(flow.mainWindow());
        QCOMPARE(accountProvider.profileQueries, 1);
        QCOMPARE(accountProvider.balanceQueries, 0);
        QCOMPARE(orderProvider.activeQueries, 1);
        auto *stack = flow.mainWindow()->findChild<QStackedWidget *>(
            QStringLiteral("primaryPages"));
        QCOMPARE(stack->currentIndex(), int(PrimaryPage::Home));

        accountProvider.completeProfile();
        QCOMPARE(accountProvider.balanceQueries, 1);
    }
};

QTEST_MAIN(UserLoginTest)
#include "user_login_test.moc"
