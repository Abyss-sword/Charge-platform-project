#pragma once

#include <QObject>
#include <QString>
#include <QtGlobal>

struct LoginRequest
{
    QString phone;
};

enum class LoginResultKind
{
    Success,
    Error,
    Offline
};

struct LoginResult
{
    LoginResultKind kind = LoginResultKind::Error;
    QString message;
    qint64 userId = 0;
    QString nickname;
    bool restricted = false;
    QString userToken;
    QString expiresAt;
};

class IUserLoginProvider : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~IUserLoginProvider() override = default;

    virtual void submit(const LoginRequest &request) = 0;

signals:
    void loginFinished(const LoginResult &result);
};

Q_DECLARE_METATYPE(LoginResult)
