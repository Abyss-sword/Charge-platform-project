#include "shell/main_window.h"
#include "shell/mobile_page_header.h"
#include "shell/bottom_navigation_bar.h"
#include "shell/mobile_metrics.h"
#include "adapters/mock_data_provider.h"
#include "adapters/user_order_provider.h"
#include "adapters/user_account_provider.h"
#include "adapters/navigation_provider.h"
#include "pages/home_page.h"
#include "pages/station_list_page.h"
#include "pages/active_order_page.h"
#include "pages/charging_history_page.h"
#include "pages/profile_page.h"
#include "pages/user_analysis_page.h"
#include "analysis/analysis_http_client.h"
#include "pages/flow_pages.h"
#include "widgets/confirm_dialog.h"
#include <QStackedWidget>
#include <QComboBox>
#include <QVBoxLayout>

#include <utility>

UserMainWindow::UserMainWindow(QWidget *parent)
    : UserMainWindow(UserContext{}, nullptr, parent)
{
}

UserMainWindow::UserMainWindow(UserContext context, QWidget *parent)
    : UserMainWindow(std::move(context), nullptr, parent)
{
}

UserMainWindow::UserMainWindow(UserContext context,
                               IStationProvider *stationProvider,
                               QWidget *parent)
    : UserMainWindow(std::move(context), stationProvider, nullptr, parent)
{
}

UserMainWindow::UserMainWindow(UserContext context,
                               IStationProvider *stationProvider,
                               IUserOrderProvider *orderProvider,
                               QWidget *parent)
    : UserMainWindow(std::move(context), stationProvider, orderProvider, nullptr, parent)
{
}

UserMainWindow::UserMainWindow(UserContext context,
                               IStationProvider *stationProvider,
                               IUserOrderProvider *orderProvider,
                               IUserAccountProvider *accountProvider,
                               QWidget *parent)
    : QMainWindow(parent)
    , userContext_(std::move(context))
{
    setWindowTitle(QStringLiteral("EV FLOW · 充电服务"));
    auto *shell = new QWidget(this);
    shell->setObjectName("shell");
    setCentralWidget(shell);
    auto *layout = new QVBoxLayout(shell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    header_ = new MobilePageHeader(shell);
    pages_ = new QStackedWidget(shell);
    pages_->setObjectName("primaryPages");
    navigation_ = new BottomNavigationBar(shell);
    layout->addWidget(header_);
    layout->addWidget(pages_, 1);
    layout->addWidget(navigation_);
    const MockDataProvider mock;
    auto *navigationProvider = new NavigationProvider(this);
    const bool remoteMode = orderProvider != nullptr;
    if (!remoteMode)
        setWindowTitle(QStringLiteral("EV FLOW · 充电服务 · OFFLINE_DEMO · SIMULATED"));
    const auto unavailable = [](const QString &title, const QString &action) {
        return PagePreview{title, QStringLiteral("正式数据接口尚未开放"),
                           QStringLiteral("Unavailable · 等待 ") + action, {},
                           Ui::PageState::Degraded};
    };
    auto *reservation = orderProvider
        ? new ReservationConfirmPage(mock.preview(SecondaryPage::ReservationConfirm),
                                     orderProvider, userContext_.userId, pages_)
        : new ReservationConfirmPage(mock.preview(SecondaryPage::ReservationConfirm), pages_);
    auto *charging = orderProvider
        ? new ChargingStatusPage(mock.preview(SecondaryPage::ChargingStatus),
                                 orderProvider, pages_)
        : new ChargingStatusPage(mock.preview(SecondaryPage::ChargingStatus), pages_);
    auto *settlement = orderProvider
        ? new SettlementPage(mock.preview(SecondaryPage::Settlement), orderProvider, pages_)
        : new SettlementPage(mock.preview(SecondaryPage::Settlement), pages_);
    auto *account = orderProvider
        ? new AccountPage(mock.preview(PrimaryPage::Account), orderProvider,
                          accountProvider, userContext_.userId, pages_)
        : new AccountPage(mock.preview(PrimaryPage::Account), pages_);
    auto *home = accountProvider
        ? new HomePage(mock.preview(PrimaryPage::Home), accountProvider,
                       userContext_.userId, pages_)
        : new HomePage(remoteMode ? unavailable(QStringLiteral("首页"),
                                                QStringLiteral("USER_PROFILE"))
                                  : mock.preview(PrimaryPage::Home), pages_);
    auto *active = orderProvider
        ? new ActiveOrderPage(mock.preview(PrimaryPage::Orders), orderProvider,
                              userContext_.userId, pages_)
        : new ActiveOrderPage(mock.preview(PrimaryPage::Orders), pages_);
    auto *history = orderProvider
        ? new ChargingHistoryPage(mock.preview(PrimaryPage::History), orderProvider,
                                  userContext_.userId, pages_)
        : new ChargingHistoryPage(mock.preview(PrimaryPage::History), pages_);
    auto *stationDetail = stationProvider
        ? new StationDetailPage(mock.preview(SecondaryPage::StationDetail),
                                stationProvider, pages_)
        : new StationDetailPage(mock.preview(SecondaryPage::StationDetail), pages_);
    auto *nearby = stationProvider
        ? new NearbyStationsPage(mock.preview(PrimaryPage::Stations), stationProvider, pages_)
        : new NearbyStationsPage(mock.preview(PrimaryPage::Stations), pages_);
    auto *analysis = new UserAnalysisPage(pages_);
    if (remoteMode) analysis->setAnalysisBaseUrl(charge::analysis::AnalysisHttpClient::configuredBaseUrl());
    const QList<PreviewPage *> pages{
        home,
        nearby,
        active,
        history,
        account,
        stationDetail,
        new NavigationPage(remoteMode
                               ? unavailable(QStringLiteral("导航"), QStringLiteral("地图服务"))
                               : mock.preview(SecondaryPage::Navigation),
                               remoteMode ? navigationProvider
                                          : static_cast<NavigationProvider *>(nullptr), pages_),
        reservation,
        charging,
        new ActiveOrderRecoveryPage(
            remoteMode
                ? PagePreview{QStringLiteral("活动订单恢复"),
                              QStringLiteral("活动订单恢复已并入订单页"),
                              QStringLiteral("正式订单服务 · ORDER_ACTIVE"), {},
                              Ui::PageState::Degraded}
                : mock.preview(SecondaryPage::ActiveOrderRecovery), pages_),
        settlement
    };
    for (auto *page : pages) {
        pages_->addWidget(page);
        if (remoteMode) {
            if (auto *selector = page->findChild<QComboBox *>(QStringLiteral("mockStateSelector")))
                selector->hide();
        }
        connect(page, &PreviewPage::navigationRequested, this, &UserMainWindow::setCurrentPage);
        connect(page, &PreviewPage::secondaryRequested, this, &UserMainWindow::navigateTo);
        if (auto *flow = qobject_cast<FlowPage *>(page))
            connect(flow, &FlowPage::primaryRequested, this, &UserMainWindow::setCurrentPage);
    }
    pages_->addWidget(analysis);
    connect(analysis, &UserAnalysisPage::manualStationSearchRequested, this,
            [this] { setCurrentPage(PrimaryPage::Stations); });
    connect(navigation_, &BottomNavigationBar::currentPageChanged, this, &UserMainWindow::setCurrentPage);
    connect(header_, &MobilePageHeader::backRequested, this, &UserMainWindow::goBack);
    connect(account, &AccountPage::logoutRequested, this, &UserMainWindow::logoutRequested);
    if (stationProvider) {
        connect(stationProvider, &IStationProvider::connectivityLost, this,
                [this] { header_->setNetworkIssue(true); });
        connect(stationProvider, &IStationProvider::connectivityRestored, this,
                [this] { header_->setNetworkIssue(false); });
    }
    if (orderProvider) {
        connect(orderProvider, &IUserOrderProvider::connectivityLost, this,
                [this] { header_->setNetworkIssue(true); });
        connect(orderProvider, &IUserOrderProvider::connectivityRestored, this,
                [this] { header_->setNetworkIssue(false); });
    }
    if (accountProvider) {
        connect(accountProvider, &IUserAccountProvider::connectivityLost, this,
                [this] { header_->setNetworkIssue(true); });
        connect(accountProvider, &IUserAccountProvider::connectivityRestored, this,
                [this] { header_->setNetworkIssue(false); });
    }
    connect(nearby, &NearbyStationsPage::stationDetailRequested, this,
            [this, stationDetail](qint64 stationId) {
        navigateTo(SecondaryPage::StationDetail);
        stationDetail->loadStation(stationId);
    });
    connect(stationDetail, &StationDetailPage::reservationRequested, this,
            [this, reservation, orderProvider](const PileSummary &pile) {
        if (!orderProvider) {
            navigateTo(SecondaryPage::ReservationConfirm);
            reservation->beginForPile(pile);
            return;
        }
        if (reservationCheckPending_) return;
        pendingReservationPile_ = pile;
        reservationCheckPending_ = true;
        if (!activeQueryInFlight_) {
            activeQueryInFlight_ = true;
            orderProvider->queryActive(userContext_.userId);
        }
    });
    connect(nearby, &NearbyStationsPage::navigationRequested, this,
            [this, navigation = qobject_cast<NavigationPage *>(pages_->widget(
                 PrimaryPageCount + static_cast<int>(SecondaryPage::Navigation)))]
            (const NavigationRequest &request) {
        if (!navigation) return;
        navigateTo(SecondaryPage::Navigation);
        navigation->setRoute(request);
    });
    connect(stationDetail, &StationDetailPage::routeNavigationRequested, this,
            [this, navigation = qobject_cast<NavigationPage *>(pages_->widget(
                 PrimaryPageCount + static_cast<int>(SecondaryPage::Navigation)))]
            (const NavigationRequest &request) {
        if (!navigation) return;
        navigateTo(SecondaryPage::Navigation);
        navigation->setRoute(request);
    });
    if (orderProvider) {
        connect(orderProvider, &IUserOrderProvider::queryFinished, this,
                [this, reservation, settlement](const UserOrderQueryResult &result) {
            if (result.query != UserOrderQueryKind::Active)
                return;
            activeQueryInFlight_ = false;
            if (!reservationCheckPending_) return;
            reservationCheckPending_ = false;
            const PileSummary pile = pendingReservationPile_;
            pendingReservationPile_ = {};
            if (result.kind != UserOrderResultKind::Success) {
                navigateTo(SecondaryPage::ReservationConfirm);
                reservation->beginForPile(pile);
                reservation->showRecoverableError(
                    result.message.isEmpty() ? QStringLiteral("暂时无法确认活动订单，请稍后重试。")
                                             : result.message,
                    result.kind == UserOrderResultKind::Offline
                        ? Ui::PageState::Offline : Ui::PageState::Error);
                return;
            }
            const QString status = result.hasOrder ? result.order.status : QString{};
            const bool unfinished = status == QStringLiteral("RESERVED")
                || status == QStringLiteral("CHARGING")
                || status == QStringLiteral("PENDING_PAYMENT");
            if (!unfinished) {
                navigateTo(SecondaryPage::ReservationConfirm);
                reservation->beginForPile(pile);
                return;
            }
            auto *dialog = new Ui::ConfirmDialog(
                QStringLiteral("存在未完成订单"),
                QStringLiteral("您有未完成的充电订单，请先结算"),
                Ui::ConfirmMode::Normal, this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            connect(dialog, &Ui::ConfirmDialog::confirmed, this,
                    [this, result, settlement]( ) {
                if (result.order.status == QStringLiteral("PENDING_PAYMENT")) {
                    settlement->setOrderId(result.order.orderId);
                    navigateTo(SecondaryPage::Settlement);
                } else {
                    setCurrentPage(PrimaryPage::Orders);
                }
            });
            dialog->show();
        });
    }
    connect(active, &ActiveOrderPage::continueOrderRequested, this,
            [this, charging, settlement](const UserOrderSnapshot &order) {
        if (order.status == QStringLiteral("PENDING_PAYMENT")) {
            settlement->setOrderId(order.orderId);
            navigateTo(SecondaryPage::Settlement);
        } else {
            charging->setOrderId(order.orderId);
            navigateTo(SecondaryPage::ChargingStatus);
        }
    });
    connect(reservation, &ReservationConfirmPage::orderReserved, this,
            [this](const QString &) {
        setCurrentPage(PrimaryPage::Orders);
    });
    connect(charging, &ChargingStatusPage::readyForSettlement, this,
            [this, settlement](const QString &orderId) {
        settlement->setOrderId(orderId);
        navigateTo(SecondaryPage::Settlement);
    });
    connect(settlement, &SettlementPage::paymentConfirmed, this,
            [account, history] {
        account->refreshBalance();
        history->refresh();
    });
    if (accountProvider) {
        connect(accountProvider, &IUserAccountProvider::profileFinished, this,
                [account](const UserProfileQueryResult &result) {
            if (result.kind == UserAccountResultKind::Success)
                account->refreshBalance();
        });
    }
    if (remoteMode) {
        connect(active, &ActiveOrderPage::activeOrderLoaded, this,
                [this, charging, settlement, startup = true]
                (const UserOrderQueryResult &result) mutable {
            if (!startup) return;
            if (result.kind != UserOrderResultKind::Success) return;
            startup = false;
            if (!result.hasOrder) return;
            if (result.order.status == QStringLiteral("RESERVED")) {
                setCurrentPage(PrimaryPage::Orders);
            } else if (result.order.status == QStringLiteral("CHARGING")) {
                charging->setOrderId(result.order.orderId);
                navigateTo(SecondaryPage::ChargingStatus);
            } else if (result.order.status == QStringLiteral("PENDING_PAYMENT")) {
                settlement->setOrderId(result.order.orderId);
                navigateTo(SecondaryPage::Settlement);
            }
        });
        connect(orderProvider, &IUserOrderProvider::connectivityRestored, this,
                [this, active] {
            if (!active->isVisible()) {
                activeQueryInFlight_ = true;
                active->refresh();
            }
        });
    }
    setCurrentPage(PrimaryPage::Home);
    if (orderProvider) {
        activeQueryInFlight_ = true;
        active->refresh();
    }
    setMinimumSize(MobileMetrics::Minimum);
    resize(MobileMetrics::Baseline);
}
void UserMainWindow::setCurrentPage(PrimaryPage page)
{
    const int index = static_cast<int>(page);
    if (index < 0 || index >= pages_->count()) return;
    const QStringList titles{QStringLiteral("首页"), QStringLiteral("附近站点"),
        QStringLiteral("活动订单"), QStringLiteral("充电记录"), QStringLiteral("我的账户")};
    pages_->setCurrentIndex(index);
    primaryIndex_ = index;
    pageHistory_.clear();
    header_->setTitle(titles[index]);
    header_->setBackAvailable(false);
    navigation_->setCurrentPage(page);
    if (auto *home = qobject_cast<HomePage *>(pages_->currentWidget())) home->refresh();
    else if (auto *active = qobject_cast<ActiveOrderPage *>(pages_->currentWidget())) {
        activeQueryInFlight_ = true;
        active->refresh();
    }
    else if (auto *history = qobject_cast<ChargingHistoryPage *>(pages_->currentWidget())) history->refresh();
    else if (auto *account = qobject_cast<AccountPage *>(pages_->currentWidget())) {
        account->refreshProfile();
        account->refreshBalance();
    }
}

void UserMainWindow::navigateTo(SecondaryPage page)
{
    const int index = PrimaryPageCount + static_cast<int>(page);
    if (index < PrimaryPageCount || index >= pages_->count()) return;
    if (page == SecondaryPage::ReservationConfirm) {
        if (auto *reservation = qobject_cast<ReservationConfirmPage *>(pages_->widget(index)))
            reservation->resetForNewReservation();
    }
    const QStringList titles{QStringLiteral("站点详情"), QStringLiteral("导航"), QStringLiteral("预约确认"),
        QStringLiteral("充电状态"), QStringLiteral("活动订单恢复"), QStringLiteral("结算"),
        QStringLiteral("智能推荐与预测")};
    pageHistory_.append(pages_->currentIndex());
    pages_->setCurrentIndex(index);
    header_->setTitle(titles[static_cast<int>(page)]);
    header_->setBackAvailable(true);
}

void UserMainWindow::goBack()
{
    if (pageHistory_.isEmpty()) {
        setCurrentPage(static_cast<PrimaryPage>(primaryIndex_));
        return;
    }
    const int previous = pageHistory_.takeLast();
    if (previous < PrimaryPageCount) {
        setCurrentPage(static_cast<PrimaryPage>(previous));
    } else {
        pages_->setCurrentIndex(previous);
        const QStringList titles{QStringLiteral("站点详情"), QStringLiteral("导航"),
            QStringLiteral("预约确认"), QStringLiteral("充电状态"),
            QStringLiteral("活动订单恢复"), QStringLiteral("结算"),
            QStringLiteral("智能推荐与预测")};
        header_->setTitle(titles[previous - PrimaryPageCount]);
        header_->setBackAvailable(!pageHistory_.isEmpty());
    }
}
