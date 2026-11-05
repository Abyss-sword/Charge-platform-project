#include <QApplication>
#include "theme.h"
#include "admin_main_window.h"
#include "admin_pages.h"
#include "config/user_runtime_config.h"
#include "remote_admin_provider.h"
#include <QPointer>
#include <QTimer>
#include <memory>
// S1-E 验收钩子：设置 CHARGE_UI_SHOT_DIR 后，每 2 秒把当前顶层窗口抓图存盘（仅测试取证，
// 不参与业务；未设置时零开销）。窗口级 QWidget::grab 不依赖屏幕合成器，Wayland/X11 均可用。
static void installUiShotHook(QWidget *root)
{
    const QString dir = qEnvironmentVariable("CHARGE_UI_SHOT_DIR");
    if (dir.isEmpty() || !root) return;
    auto *timer = new QTimer(root);
    timer->setInterval(2000);
    QObject::connect(timer, &QTimer::timeout, root, [root, dir] {
        static int sequence = 0;
        ++sequence;
        const QString path = QStringLiteral("%1/%2-shot-%3.png")
            .arg(dir, root->objectName().isEmpty() ? QStringLiteral("window")
                                                  : root->objectName())
            .arg(sequence, 4, 10, QLatin1Char('0'));
        root->grab().save(path);
    });
    timer->start();
}

int main(int argc,char **argv){
    QApplication app(argc,argv); Ui::Theme::apply(app);
    std::unique_ptr<IAdminProvider> provider;
    std::unique_ptr<AdminLoginPage> login;
    const bool offlineDemo = qEnvironmentVariable("OFFLINE_DEMO") == QStringLiteral("1");
    if (offlineDemo) {
        login = std::make_unique<AdminLoginPage>();
    } else {
        const UserRuntimeConfig config = loadUserRuntimeConfig();
        provider = std::make_unique<RemoteAdminProvider>(config.serverHost, config.serverPort);
        login = std::make_unique<AdminLoginPage>(provider.get());
    }
    QPointer<AdminMainWindow> window;
    QObject::connect(login.get(), &AdminLoginPage::loginSucceeded, [&login,&window]{
        if (!window) window = new AdminMainWindow;
        login->hide(); window->show();
    });
    QObject::connect(login.get(), &AdminLoginPage::authenticated,
                     [&login,&window,&provider](const QString &token){
        if (!window) window = new AdminMainWindow(token, provider.get());
        login->hide(); window->show();
        installUiShotHook(window.data());
    });
    login->show();
    installUiShotHook(login.get());  // S1-E：登录页同样纳入抓图取证。
    return app.exec();
}
