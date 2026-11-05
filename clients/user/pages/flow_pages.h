#pragma once
#include "models/station.h"
#include "adapters/navigation_provider.h"
#include "pages/preview_page.h"

#include <QPointer>

class IUserOrderProvider;
class IStationProvider;
class QPushButton;
class QLabel;
class QComboBox;
class QTimer;
class NavigationProvider;

class FlowPage : public PreviewPage
{
    Q_OBJECT
public:
    FlowPage(const PagePreview &preview, QWidget *parent = nullptr);
signals:
    void primaryRequested(PrimaryPage page);
protected:
    void addFlowButton(const QString &text, SecondaryPage page);
    void addPrimaryButton(const QString &text, PrimaryPage page);
};

#define DECLARE_FLOW_PAGE(Name) \
class Name final : public FlowPage \
{ \
public: \
    Name(const PagePreview &preview, QWidget *parent = nullptr); \
};
DECLARE_FLOW_PAGE(ActiveOrderRecoveryPage)
#undef DECLARE_FLOW_PAGE

class NavigationPage final : public FlowPage
{
    Q_OBJECT
public:
    explicit NavigationPage(const PagePreview &preview, QWidget *parent = nullptr);
    NavigationPage(const PagePreview &preview, NavigationProvider *provider,
                   QWidget *parent = nullptr);
    void setRoute(const NavigationRequest &request);
private:
    void updateRoute();
    void showStaticFallback(const QString &reason);
    QPointer<NavigationProvider> provider_;
    NavigationRequest request_;
    QComboBox *mode_ = nullptr;
    QWidget *mapContainer_ = nullptr;
};

class StationDetailPage final : public FlowPage
{
    Q_OBJECT
public:
    explicit StationDetailPage(const PagePreview &preview, QWidget *parent = nullptr);
    StationDetailPage(const PagePreview &preview, IStationProvider *provider,
                      QWidget *parent = nullptr);
    void loadStation(qint64 stationId);
signals:
    void reservationRequested(const PileSummary &pile);
    void routeNavigationRequested(const NavigationRequest &request);
private:
    void refresh() override;
    QPointer<IStationProvider> provider_;
    qint64 stationId_ = 0;
    QLabel *summary_ = nullptr;
    QLabel *piles_ = nullptr;
    QComboBox *availablePiles_ = nullptr;
    QPushButton *reserve_ = nullptr;
    QPushButton *navigate_ = nullptr;
    QList<PileSummary> availablePileModels_;
    NavigationRequest navigationRequest_;
};

class ReservationConfirmPage final : public FlowPage
{
    Q_OBJECT
public:
    explicit ReservationConfirmPage(const PagePreview &preview, QWidget *parent = nullptr);
    ReservationConfirmPage(const PagePreview &preview, IUserOrderProvider *provider,
                           qint64 userId, QWidget *parent = nullptr);
    void resetForNewReservation();
    void beginForPile(const PileSummary &pile);
    void showRecoverableError(const QString &message, Ui::PageState state = Ui::PageState::Error);
signals:
    void orderReserved(const QString &orderId);
private:
    void submit();
    QPointer<IUserOrderProvider> provider_;
    QLabel *selectedPile_ = nullptr;
    PileSummary pile_;
    qint64 userId_ = 0;
};

class ChargingStatusPage final : public FlowPage
{
    Q_OBJECT
public:
    explicit ChargingStatusPage(const PagePreview &preview, QWidget *parent = nullptr);
    ChargingStatusPage(const PagePreview &preview, IUserOrderProvider *provider,
                       QWidget *parent = nullptr);
    void setOrderId(const QString &orderId);
    void refresh() override;  // S3：基类轮询入口
signals:
    void readyForSettlement(const QString &orderId);
private:
    void submit();
    QPointer<IUserOrderProvider> provider_;
    QString orderId_;
    QPushButton *cancel_ = nullptr;
    bool charging_ = false;
    bool operationPending_ = false;
    bool detailPending_ = false;
    QString lastQueryTime_;
    QLabel *detail_ = nullptr;
    void refreshDetail();
};

class SettlementPage final : public FlowPage
{
    Q_OBJECT
public:
    explicit SettlementPage(const PagePreview &preview, QWidget *parent = nullptr);
    SettlementPage(const PagePreview &preview, IUserOrderProvider *provider,
                   QWidget *parent = nullptr);
    void setOrderId(const QString &orderId);
signals:
    void paymentConfirmed();
private:
    void submit();
    void verifyPaymentState();
    QPointer<IUserOrderProvider> provider_;
    QString orderId_;
    bool paymentUncertain_ = false;
    bool verificationPending_ = false;
    bool paymentComplete_ = false;
};
