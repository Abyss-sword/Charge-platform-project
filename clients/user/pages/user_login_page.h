#pragma once

#include "adapters/user_login_provider.h"
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

namespace Ui {
class PageStateWidget;
}

class UserLoginPage final : public QWidget
{
    Q_OBJECT
public:
    explicit UserLoginPage(IUserLoginProvider *provider, QWidget *parent = nullptr);
    void setOfflineDemoMode(bool enabled);
    void resetForNewSession();

signals:
    void loginSucceeded(const LoginResult &result);

private slots:
    void submit();
    void handleResult(const LoginResult &result);

private:
    bool validatePhone();
    void setLoading(bool loading);
    void setValidationError(const QString &message);

    IUserLoginProvider *provider_;
    QLineEdit *phone_;
    QPushButton *submit_;
    QPushButton *retry_;
    QLabel *feedback_;
    QLabel *modeBanner_;
    Ui::PageStateWidget *status_;
};
