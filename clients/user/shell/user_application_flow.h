#pragma once

#include <QObject>
#include <QPointer>

class IUserLoginProvider;
class IStationProvider;
class IUserOrderProvider;
class IUserAccountProvider;
class UserLoginPage;
class UserMainWindow;

class UserApplicationFlow final : public QObject
{
    Q_OBJECT
public:
    explicit UserApplicationFlow(IUserLoginProvider *provider, QObject *parent = nullptr);
    UserApplicationFlow(IUserLoginProvider *provider, IStationProvider *stationProvider,
                        QObject *parent = nullptr);
    UserApplicationFlow(IUserLoginProvider *provider, IStationProvider *stationProvider,
                        IUserOrderProvider *orderProvider, QObject *parent = nullptr);
    UserApplicationFlow(IUserLoginProvider *provider, IStationProvider *stationProvider,
                        IUserOrderProvider *orderProvider,
                        IUserAccountProvider *accountProvider, QObject *parent = nullptr);
    ~UserApplicationFlow() override;

    void start();
    UserLoginPage *loginPage() const;
    UserMainWindow *mainWindow() const;

private:
    QPointer<UserLoginPage> login_;
    QPointer<UserMainWindow> main_;
    QPointer<IStationProvider> stationProvider_;
    QPointer<IUserOrderProvider> orderProvider_;
    QPointer<IUserAccountProvider> accountProvider_;
};
