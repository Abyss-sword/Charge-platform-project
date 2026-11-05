#include <QApplication>

#include "adapters/mock_user_login_provider.h"
#include "adapters/mock_station_provider.h"
#include "adapters/remote_user_login_provider.h"
#include "adapters/remote_station_provider.h"
#include "adapters/remote_user_order_provider.h"
#include "adapters/remote_user_account_provider.h"
#include "config/user_runtime_config.h"
#include "shell/main_window.h"
#include "shell/user_application_flow.h"
#include "pages/user_login_page.h"
#include "theme.h"

#include <QTimer>
#include <QWidget>
#include <memory>
// S1-E 验收钩子：设置 CHARGE_UI_SHOT_DIR 后，每 2 秒抓取主窗口存 PNG（仅测试取证，不参与业务）。
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


int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("EV FLOW"));
    Ui::Theme::apply(application);

    std::unique_ptr<IUserLoginProvider> loginProvider;
    std::unique_ptr<IStationProvider> stationProvider;
    std::unique_ptr<IUserOrderProvider> orderProvider;
    std::unique_ptr<IUserAccountProvider> accountProvider;
    const bool offlineDemo = qEnvironmentVariable("OFFLINE_DEMO") == QStringLiteral("1");
    if (offlineDemo) {
        loginProvider = std::make_unique<MockUserLoginProvider>();
        stationProvider = std::make_unique<MockStationProvider>();
    } else {
        const UserRuntimeConfig config = loadUserRuntimeConfig();
        loginProvider = std::make_unique<RemoteUserLoginProvider>(
            config.serverHost, config.serverPort);
        stationProvider = std::make_unique<RemoteStationProvider>(
            config.serverHost, config.serverPort);
        orderProvider = std::make_unique<RemoteUserOrderProvider>(
            config.serverHost, config.serverPort);
        accountProvider = std::make_unique<RemoteUserAccountProvider>(
            config.serverHost, config.serverPort);
    }

    UserApplicationFlow flow(loginProvider.get(), stationProvider.get(), orderProvider.get(),
                             accountProvider.get());
    flow.loginPage()->setOfflineDemoMode(offlineDemo);
    flow.start();
    // 主窗口在登录后创建；延迟挂抓图钩子（窗口就绪后生效）。
    QTimer::singleShot(0, &flow, [&flow] {
        if (flow.mainWindow()) installUiShotHook(qobject_cast<QWidget *>(flow.mainWindow()));
    });
    return application.exec();
}
