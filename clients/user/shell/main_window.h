#pragma once
#include "models/user_context.h"
#include "models/station.h"
#include "shell/primary_page.h"

#include <QMainWindow>
#include <QList>

class QStackedWidget;
class IStationProvider;
class IUserOrderProvider;
class IUserAccountProvider;
class MobilePageHeader;
class BottomNavigationBar;
class UserMainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit UserMainWindow(QWidget *parent = nullptr);
    explicit UserMainWindow(UserContext context, QWidget *parent = nullptr);
    UserMainWindow(UserContext context, IStationProvider *stationProvider,
                   QWidget *parent = nullptr);
    UserMainWindow(UserContext context, IStationProvider *stationProvider,
                   IUserOrderProvider *orderProvider, QWidget *parent = nullptr);
    UserMainWindow(UserContext context, IStationProvider *stationProvider,
                   IUserOrderProvider *orderProvider, IUserAccountProvider *accountProvider,
                   QWidget *parent = nullptr);

    const UserContext &userContext() const noexcept { return userContext_; }
signals:
    void logoutRequested();
public slots:
    void setCurrentPage(PrimaryPage page);
    void navigateTo(SecondaryPage page);
    void goBack();
private:
    UserContext userContext_;
    QStackedWidget *pages_;
    MobilePageHeader *header_;
    BottomNavigationBar *navigation_;
    int primaryIndex_ = 0;
    QList<int> pageHistory_;
    PileSummary pendingReservationPile_;
    bool reservationCheckPending_ = false;
    bool activeQueryInFlight_ = false;
};
