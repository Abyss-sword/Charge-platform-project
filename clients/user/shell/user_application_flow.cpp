#include "shell/user_application_flow.h"

#include <QTimer>
#include <QWidget>

// S1-E 验收钩子：CHARGE_UI_SHOT_DIR 设置后每 2 秒抓取窗口存 PNG（仅测试取证）。
static void installUiShotHook(QWidget *root)
{
    const QString dir = qEnvironmentVariable("CHARGE_UI_SHOT_DIR");
    if (dir.isEmpty() || !root) return;
    auto *timer = new QTimer(root);
    timer->setInterval(2000);
    QObject::connect(timer, &QTimer::timeout, root, [root, dir] {
        static int sequence = 0;
        ++sequence;
        const QString path = QStringLiteral("%1/user-shot-%3.png")
            .arg(dir).arg(sequence, 4, 10, QLatin1Char('0'));
        root->grab().save(path);
    });
    timer->start();
}

#include "adapters/station_provider.h"
#include "adapters/user_order_provider.h"
#include "adapters/user_account_provider.h"
#include "adapters/user_login_provider.h"
#include "models/user_context.h"
#include "pages/user_login_page.h"
#include "shell/main_window.h"

UserApplicationFlow::UserApplicationFlow(IUserLoginProvider *provider, QObject *parent)
    : UserApplicationFlow(provider, nullptr, nullptr, parent)
{
}

UserApplicationFlow::UserApplicationFlow(IUserLoginProvider *provider,
                                         IStationProvider *stationProvider,
                                         QObject *parent)
    : UserApplicationFlow(provider, stationProvider, nullptr, parent)
{
}

UserApplicationFlow::UserApplicationFlow(IUserLoginProvider *provider,
                                         IStationProvider *stationProvider,
                                         IUserOrderProvider *orderProvider,
                                         QObject *parent)
    : UserApplicationFlow(provider, stationProvider, orderProvider, nullptr, parent)
{
}

UserApplicationFlow::UserApplicationFlow(IUserLoginProvider *provider,
                                         IStationProvider *stationProvider,
                                         IUserOrderProvider *orderProvider,
                                         IUserAccountProvider *accountProvider,
                                         QObject *parent)
    : QObject(parent)
    , login_(new UserLoginPage(provider))
    , stationProvider_(stationProvider)
    , orderProvider_(orderProvider)
    , accountProvider_(accountProvider)
{
    installUiShotHook(login_);
    connect(login_, &UserLoginPage::loginSucceeded, this, [this](const LoginResult &result) {
        if (orderProvider_) orderProvider_->setUserToken(result.userToken);
        if (accountProvider_) accountProvider_->setUserToken(result.userToken);
        if (!main_) {
            main_ = new UserMainWindow(
                UserContext{result.userId, result.nickname, result.restricted},
                stationProvider_, orderProvider_, accountProvider_);
            connect(main_, &UserMainWindow::logoutRequested, this, [this] {
                if (!main_) return;
                if (orderProvider_) orderProvider_->setUserToken({});
                if (accountProvider_) accountProvider_->setUserToken({});
                main_->hide();
                main_->deleteLater();
                login_->resetForNewSession();
                login_->show();
            });
        }
        main_->show();
        installUiShotHook(main_);  // S1-E：登录成功后主窗口纳入抓图取证。
        login_->hide();
    });
}

UserApplicationFlow::~UserApplicationFlow()
{
    delete login_;
    delete main_;
}

void UserApplicationFlow::start()
{
    login_->show();
}

UserLoginPage *UserApplicationFlow::loginPage() const
{
    return login_.data();
}

UserMainWindow *UserApplicationFlow::mainWindow() const
{
    return main_.data();
}
