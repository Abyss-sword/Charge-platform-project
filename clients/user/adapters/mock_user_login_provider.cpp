#include "adapters/mock_user_login_provider.h"

#include <QTimer>

MockUserLoginProvider::MockUserLoginProvider(QObject *parent)
    : IUserLoginProvider(parent)
{
}

void MockUserLoginProvider::submit(const LoginRequest &request)
{
    Q_UNUSED(request)
    QTimer::singleShot(0, this, [this] {
        const MockResult mock = data_.load(UserMockDataset::Login, scenario_);
        if (mock.valid) {
            emit loginFinished(LoginResult{LoginResultKind::Success,
                                           QStringLiteral("登录模拟成功"),
                                           0,
                                           {},
                                           false,
                                           {},
                                           {}});
        } else if (mock.state == Ui::PageState::Offline) {
            emit loginFinished(LoginResult{LoginResultKind::Offline,
                                           mock.message,
                                           0,
                                           {},
                                           false,
                                           {},
                                           {}});
        } else {
            emit loginFinished(LoginResult{LoginResultKind::Error,
                                           mock.message,
                                           0,
                                           {},
                                           false,
                                           {},
                                           {}});
        }
    });
}
