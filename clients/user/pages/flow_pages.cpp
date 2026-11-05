#include "pages/flow_pages.h"
#include "adapters/station_provider.h"
#include "adapters/navigation_provider.h"
#include "adapters/user_order_provider.h"
#include "theme.h"
#include "widgets/enum_labels.h"
#include <QPushButton>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFrame>
#include <QLabel>
#include <QComboBox>
#include <QTimer>
#include <QDateTime>
#include <QVBoxLayout>
#include <QUrl>

#include <cmath>
#if defined(CHARGE_HAS_WEBENGINE)
#include <QWebEngineView>
#endif

namespace {
PagePreview formalPreview(const PagePreview &preview, const QString &subtitle)
{
    return {preview.title, subtitle, {}, {}, Ui::PageState::Normal};
}

void showRecoverableState(FlowPage *page, Ui::PageState state)
{
    page->setPageState(state);
}
}

FlowPage::FlowPage(const PagePreview &preview, QWidget *parent) : PreviewPage(preview, parent)
{
    appendCards(preview.cards);
}
void FlowPage::addFlowButton(const QString &text, SecondaryPage page)
{
    auto *button = new QPushButton(text, this);
    button->setObjectName(QStringLiteral("secondaryAction%1").arg(static_cast<int>(page)));
    button->setMinimumHeight(Ui::Theme::TouchHeight);
    addContent(button);
    connect(button, &QPushButton::clicked, this, [this, page] { emit secondaryRequested(page); });
}
void FlowPage::addPrimaryButton(const QString &text, PrimaryPage page)
{
    auto *button = new QPushButton(text, this);
    button->setMinimumHeight(Ui::Theme::TouchHeight);
    addContent(button);
    connect(button, &QPushButton::clicked, this, [this, page] { emit primaryRequested(page); });
}
StationDetailPage::StationDetailPage(const PagePreview &preview, QWidget *parent)
    : FlowPage(preview, parent)
{
    setObjectName("stationDetailPage");
    addFlowButton(QStringLiteral("查看模拟导航"), SecondaryPage::Navigation);
    addFlowButton(QStringLiteral("预约此桩（模拟）"), SecondaryPage::ReservationConfirm);
}

StationDetailPage::StationDetailPage(const PagePreview &preview, IStationProvider *provider,
                                     QWidget *parent)
    : FlowPage(formalPreview(preview, QStringLiteral("正式站点与电桩详情")), parent)
    , provider_(provider)
{
    setObjectName("stationDetailPage");
    setNormalStateMessage(QStringLiteral("服务已连接，数据状态正常"));
    setSource(QStringLiteral("正式站点服务 · STATION_DETAIL"));
    enablePolling();  // S3：可见时每 3 秒刷新站点/空闲桩状态，离开页面停止
    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    addContent(summary_);
    piles_ = new QLabel(this);
    piles_->setWordWrap(true);
    addContent(piles_);
    availablePiles_ = new QComboBox(this);
    availablePiles_->setObjectName(QStringLiteral("availablePileSelector"));
    addContent(availablePiles_);
    reserve_ = new QPushButton(QStringLiteral("预约选中空闲桩"), this);
    reserve_->setObjectName(QStringLiteral("reserveSelectedPileAction"));
    reserve_->setMinimumHeight(Ui::Theme::TouchHeight);
    reserve_->setEnabled(false);
    addContent(reserve_);
    navigate_ = new QPushButton(QStringLiteral("导航到此站"), this);
    navigate_->setObjectName(QStringLiteral("stationNavigationAction"));
    navigate_->setMinimumHeight(Ui::Theme::TouchHeight);
    navigate_->setEnabled(false);
    addContent(navigate_);
    connect(reserve_, &QPushButton::clicked, this, [this] {
        const int index = availablePiles_->currentIndex();
        if (index >= 0 && index < availablePileModels_.size())
            emit reservationRequested(availablePileModels_.at(index));
    });
    connect(provider_, &IStationProvider::detailFinished, this,
            [this](const StationDetailResult &result) {
        if (result.kind != StationQueryResultKind::Success) {
            setSource(result.message);
            setPageState(result.kind == StationQueryResultKind::Offline
                             ? Ui::PageState::Offline : Ui::PageState::Error);
            reserve_->setEnabled(false);
            navigate_->setEnabled(false);
            return;
        }
        const StationDetail &detail = result.detail;
        navigationRequest_ = {23.1351, 113.3245, detail.latitude,
                              detail.longitude, detail.name};
        navigate_->setEnabled(true);
        summary_->setText(QStringLiteral("%1\n%2\n%3 元/kWh · %4\n坐标：%5, %6")
                              .arg(detail.name, detail.address, detail.priceYuanPerKwh,
                                   detail.status)
                              .arg(detail.latitude, 0, 'f', 6)
                              .arg(detail.longitude, 0, 'f', 6));
        QStringList lines;
        availablePiles_->clear();
        availablePileModels_.clear();
        for (const PileSummary &pile : detail.piles) {
            // S1-C：类型/状态显示中文标签（协议值仅提交端使用）。
            const QString typeLabel = Ui::labelFor(Ui::chargeTypeOptions(), pile.chargeType);
            const QString statusLabel = Ui::labelFor(Ui::pileStatusOptions(), pile.status);
            lines.append(QStringLiteral("#%1 · %2 · %3 · %4 kW · %5")
                             .arg(pile.pileId).arg(pile.serial, typeLabel)
                             .arg(pile.ratedPowerKw, 0, 'f', 1)
                             .arg(statusLabel));
            if (pile.status == QStringLiteral("IDLE")) {
                availablePileModels_.append(pile);
                availablePiles_->addItem(
                    QStringLiteral("#%1 · %2 · %3 · %4 kW · 可用")
                        .arg(pile.pileId).arg(pile.serial, typeLabel)
                        .arg(pile.ratedPowerKw, 0, 'f', 1));
            }
        }
        piles_->setText(lines.isEmpty() ? QStringLiteral("该站点暂无电桩。")
                                        : lines.join(QLatin1Char('\n')));
        reserve_->setEnabled(availablePiles_->count() > 0);
        setSource(QStringLiteral("正式站点服务 · 数据已更新"));
        setPageState(detail.piles.isEmpty() ? Ui::PageState::Empty : Ui::PageState::Normal);
    });
    connect(navigate_, &QPushButton::clicked, this, [this] {
        emit routeNavigationRequested(navigationRequest_);
    });
    connect(provider_, &IStationProvider::connectivityLost, this, [this] {
        setSource(QStringLiteral("连接已断开，当前站点详情可能已过期。"));
        setPageState(Ui::PageState::Offline);
        reserve_->setEnabled(false);
        navigate_->setEnabled(false);
    });
    connect(provider_, &IStationProvider::connectivityRestored, this, [this] {
        if (isVisible() && stationId_ > 0) refresh();
    });
}

void StationDetailPage::loadStation(qint64 stationId)
{
    stationId_ = stationId;
    refresh();
}

void StationDetailPage::refresh()
{
    if (!provider_ || stationId_ <= 0) return;
    reserve_->setEnabled(false);
    navigate_->setEnabled(false);
    markRefreshing();
    provider_->queryDetail(stationId_);
}
NavigationPage::NavigationPage(const PagePreview &preview, QWidget *parent)
    : NavigationPage(preview, nullptr, parent) {}

NavigationPage::NavigationPage(const PagePreview &preview, NavigationProvider *provider,
                               QWidget *parent)
    : FlowPage(preview, parent), provider_(provider)
{
    setObjectName("navigationPage");
    auto *copy = new QPushButton(QStringLiteral("复制坐标"), this);
    copy->setObjectName("copyCoordinatesAction");
    copy->setMinimumHeight(Ui::Theme::TouchHeight);
    addContent(copy);
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(QStringLiteral("%1, %2")
            .arg(request_.destinationLatitude, 0, 'f', 6)
            .arg(request_.destinationLongitude, 0, 'f', 6));
    });
    mode_ = new QComboBox(this);
    mode_->setObjectName(QStringLiteral("navigationMode"));
    mode_->addItem(QStringLiteral("驾车"), static_cast<int>(NavigationMode::Driving));
    mode_->addItem(QStringLiteral("步行"), static_cast<int>(NavigationMode::Walking));
    addContent(mode_);
    mapContainer_ = new QWidget(this);
    mapContainer_->setObjectName(QStringLiteral("navigationMap"));
    mapContainer_->setLayout(new QVBoxLayout);
    addContent(mapContainer_);
    auto *continueButton = new QPushButton(
        provider_ ? QStringLiteral("返回站点详情") : QStringLiteral("继续预约（演示）"), this);
    continueButton->setObjectName("continueReservationAction");
    continueButton->setMinimumHeight(Ui::Theme::TouchHeight);
    addContent(continueButton);
    connect(continueButton, &QPushButton::clicked, this,
            [this] { emit secondaryRequested(SecondaryPage::ReservationConfirm); });
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] { updateRoute(); });
    if (provider_ && !provider_->isAvailable()) {
        setSource(QStringLiteral("导航服务暂不可用"));
        setPageState(Ui::PageState::Degraded);
    }
}

void NavigationPage::setRoute(const NavigationRequest &request)
{
    request_ = request;
    updateRoute();
}

void NavigationPage::updateRoute()
{
    if (!mapContainer_) return;
    auto *layout = qobject_cast<QVBoxLayout *>(mapContainer_->layout());
    while (QLayoutItem *item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (!provider_) {
        auto *label = new QLabel(QStringLiteral("离线演示地图\n目标：%1\n坐标：%2, %3")
            .arg(request_.stationName)
            .arg(request_.destinationLatitude, 0, 'f', 6)
            .arg(request_.destinationLongitude, 0, 'f', 6), mapContainer_);
        label->setWordWrap(true);
        layout->addWidget(label);
        return;
    }
    if (!provider_->isAvailable()) {
        showStaticFallback(QStringLiteral("导航服务暂不可用，已切换为静态站点信息。"));
        return;
    }
#if defined(CHARGE_HAS_WEBENGINE)
    auto *view = new QWebEngineView(mapContainer_);
    view->setObjectName(QStringLiteral("navigationWebView"));
    view->setUrl(provider_->routeUrl(request_, static_cast<NavigationMode>(
        mode_->currentData().toInt())));
    layout->addWidget(view);
    setSource(QStringLiteral("腾讯地图路线规划 · %1").arg(request_.stationName));
    setPageState(Ui::PageState::Normal);
    // S1-C：在线地图加载失败时兜底显示静态站点卡，不允许空白页面（审查 P1）。
    connect(view, &QWebEngineView::loadFinished, view,
            [this, view, layout](bool ok) {
        if (ok) return;
        layout->removeWidget(view);
        view->deleteLater();
        showStaticFallback(QStringLiteral("地图在线加载失败（无网络或服务受限）。"));
    });
#endif
}

// S1-C 静态兜底卡：站点、经纬度、近似距离、模拟起点标注与外部地图入口；
// 路径仍可复制坐标后自行导航，页面提供“返回站点详情/继续预约”按钮不受影响。
void NavigationPage::showStaticFallback(const QString &reason)
{
    if (!mapContainer_) return;
    auto *layout = qobject_cast<QVBoxLayout *>(mapContainer_->layout());
    while (QLayoutItem *item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    // 近似直线距离（Haversine 简化球面距离，仅作展示）。
    const double lat1 = qDegreesToRadians(request_.originLatitude);
    const double lon1 = qDegreesToRadians(request_.originLongitude);
    const double lat2 = qDegreesToRadians(request_.destinationLatitude);
    const double lon2 = qDegreesToRadians(request_.destinationLongitude);
    const double deltaLat = lat2 - lat1;
    const double deltaLon = lon2 - lon1;
    const double a = std::sin(deltaLat / 2) * std::sin(deltaLat / 2)
        + std::cos(lat1) * std::cos(lat2) * std::sin(deltaLon / 2) * std::sin(deltaLon / 2);
    const double distanceKm = 6371.0 * 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));

    auto *card = new QFrame(mapContainer_);
    card->setObjectName(QStringLiteral("navigationStaticCard"));
    card->setStyleSheet(QStringLiteral(
        "QFrame{background:#f7f8fa;border:1px solid #d9dee5;border-radius:8px;padding:12px;}"));
    auto *cardLayout = new QVBoxLayout(card);
    auto *title = new QLabel(QStringLiteral("%1 · 静态站点信息").arg(request_.stationName), card);
    title->setStyleSheet(QStringLiteral("font-weight:600;"));
    auto *coordinate = new QLabel(QStringLiteral("目的地坐标：%1, %2")
        .arg(request_.destinationLatitude, 0, 'f', 6)
        .arg(request_.destinationLongitude, 0, 'f', 6), card);
    coordinate->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *distance = new QLabel(QStringLiteral("距模拟起点约 %1 公里").arg(distanceKm, 0, 'f', 2), card);
    auto *originNote = new QLabel(QStringLiteral("起点为模拟位置，仅供教学演示。"), card);
    originNote->setStyleSheet(QStringLiteral("color:#92400e;font-size:11px;"));
    auto *reasonLabel = new QLabel(reason, card);
    reasonLabel->setStyleSheet(QStringLiteral("color:#92400e;font-size:11px;"));
    auto *openExternal = new QPushButton(QStringLiteral("用系统浏览器打开外部地图"), card);
    openExternal->setObjectName(QStringLiteral("openExternalMapAction"));
    connect(openExternal, &QPushButton::clicked, card, [this] {
        if (provider_) {
            QDesktopServices::openUrl(provider_->routeUrl(request_, static_cast<NavigationMode>(
                mode_->currentData().toInt())));
        }
    });
    cardLayout->addWidget(title);
    cardLayout->addWidget(coordinate);
    cardLayout->addWidget(distance);
    cardLayout->addWidget(originNote);
    cardLayout->addWidget(reasonLabel);
    cardLayout->addWidget(openExternal);
    layout->addWidget(card);
    setSource(QStringLiteral("%1 · 已显示静态兜底信息").arg(reason));
    setPageState(Ui::PageState::Degraded);
}
ActiveOrderRecoveryPage::ActiveOrderRecoveryPage(const PagePreview &preview, QWidget *parent) : FlowPage(preview, parent)
{ setObjectName("activeOrderRecoveryPage"); if(preview.state!=Ui::PageState::Degraded){addFlowButton(QStringLiteral("继续查看充电状态"), SecondaryPage::ChargingStatus);addPrimaryButton(QStringLiteral("返回订单"), PrimaryPage::Orders);} }

ReservationConfirmPage::ReservationConfirmPage(const PagePreview &preview, QWidget *parent)
    : ReservationConfirmPage(preview, nullptr, 0, parent)
{
}

ReservationConfirmPage::ReservationConfirmPage(const PagePreview &preview,
                                               IUserOrderProvider *provider,
                                               qint64 userId, QWidget *parent)
    : FlowPage(provider ? formalPreview(preview, QStringLiteral("创建正式预约订单"))
                        : preview, parent)
    , provider_(provider), userId_(userId)
{
    setObjectName("reservationConfirmPage");
    if (provider_) setNormalStateMessage(QStringLiteral("服务已连接，数据状态正常"));
    if (!provider_) {
        setAction(QStringLiteral("确认预约（模拟）"));
    } else {
        setSource(QStringLiteral("正式订单服务 · 预约结果由服务端裁决"));
        selectedPile_ = new QLabel(QStringLiteral("请从站点详情选择空闲电桩。"), this);
        selectedPile_->setObjectName(QStringLiteral("selectedReservationPile"));
        selectedPile_->setWordWrap(true);
        addContent(selectedPile_);
        setAction(QStringLiteral("提交正式预约"), false);
        connect(primaryAction(), &QPushButton::clicked, this,
                &ReservationConfirmPage::submit);
        connect(provider_, &IUserOrderProvider::operationFinished, this,
                [this](const UserOrderResult &result) {
            if (result.action != UserOrderAction::Reserve) return;
            if (result.kind == UserOrderResultKind::Success) {
                setSource(QStringLiteral("预约成功 · 订单：") + result.orderId);
                setPageState(Ui::PageState::Normal);
                emit orderReserved(result.orderId);
            } else {
                setSource(result.message);
                showRecoverableState(this,
                    result.kind == UserOrderResultKind::Offline
                        ? Ui::PageState::Offline : Ui::PageState::Error);
            }
        });
        connect(provider_, &IUserOrderProvider::connectivityLost, this, [this] {
            setSource(QStringLiteral("连接已断开，当前订单数据可能已过期。"));
            setPageState(Ui::PageState::Offline);
        });
        connect(provider_, &IUserOrderProvider::connectivityRestored, this, [this] {
            setSource(QStringLiteral("正式订单服务 · 连接已恢复，请重新提交操作。"));
            setPageState(Ui::PageState::Normal);
        });
    }
    addFlowButton(QStringLiteral("返回站点详情"), SecondaryPage::StationDetail);
}

void ReservationConfirmPage::submit()
{
    if (!provider_ || pile_.pileId <= 0 || pile_.status != QStringLiteral("IDLE")
        || userId_ <= 0) {
        setSource(QStringLiteral("请从站点详情重新选择可用电桩。"));
        showRecoverableState(this, Ui::PageState::Error);
        return;
    }
    setPageState(Ui::PageState::Loading);
    provider_->reserve(userId_, pile_.pileId);
}

void ReservationConfirmPage::resetForNewReservation()
{
    if (!provider_) return;
    pile_ = {};
    selectedPile_->setText(QStringLiteral("请从站点详情选择空闲电桩。"));
    setSource(QStringLiteral("正式订单服务 · 预约结果由服务端裁决"));
    setAction(QStringLiteral("提交正式预约"), false);
    setPageState(Ui::PageState::Normal);
}

void ReservationConfirmPage::beginForPile(const PileSummary &pile)
{
    resetForNewReservation();
    if (pile.pileId <= 0 || pile.status != QStringLiteral("IDLE")) return;
    pile_ = pile;
    selectedPile_->setText(
        QStringLiteral("电桩 ID：%1\n类型：%2\n额定功率：%3 kW\n当前状态：%4")
            .arg(pile.pileId).arg(pile.chargeType)
            .arg(pile.ratedPowerKw, 0, 'f', 1).arg(pile.status));
    setSource(QStringLiteral("已选择服务端空闲桩 · 预约结果由服务端裁决"));
    setAction(QStringLiteral("提交正式预约"), true);
    setPageState(Ui::PageState::Normal);
}

void ReservationConfirmPage::showRecoverableError(const QString &message, Ui::PageState state)
{
    if (!provider_) return;
    setSource(message);
    setPageState(state);
}

ChargingStatusPage::ChargingStatusPage(const PagePreview &preview, QWidget *parent)
    : ChargingStatusPage(preview, nullptr, parent)
{
}

ChargingStatusPage::ChargingStatusPage(const PagePreview &preview,
                                       IUserOrderProvider *provider, QWidget *parent)
    : FlowPage(provider ? formalPreview(preview, QStringLiteral("正式订单状态操作"))
                        : preview, parent)
    , provider_(provider)
{
    setObjectName("chargingStatusPage");
    if (provider_) setNormalStateMessage(QStringLiteral("服务已连接，数据状态正常"));
    setAction(provider_ ? QStringLiteral("开始充电")
                        : QStringLiteral("结束充电并查看结算（模拟）"));
    connect(primaryAction(), &QPushButton::clicked, this, &ChargingStatusPage::submit);
    if (provider_) {
        setSource(QStringLiteral("正式订单服务 · 状态变更由服务端裁决"));
        detail_ = new QLabel(this);
        detail_->setWordWrap(true);
        addContent(detail_);
        enablePolling();  // S3：改用基类轮询（showEvent 启动 / hideEvent 停止），离开页面不再轮询
        cancel_ = new QPushButton(QStringLiteral("取消预约"), this);
        cancel_->setObjectName(QStringLiteral("cancelReservationAction"));
        cancel_->setMinimumHeight(Ui::Theme::TouchHeight);
        addContent(cancel_);
        connect(cancel_, &QPushButton::clicked, this, [this] {
            if (orderId_.isEmpty()) return;
            setPageState(Ui::PageState::Loading);
            operationPending_ = true;
            cancel_->setEnabled(false);
            provider_->cancel(orderId_);
        });
        connect(provider_, &IUserOrderProvider::operationFinished, this,
                [this](const UserOrderResult &result) {
            if (result.action == UserOrderAction::Cancel) {
                operationPending_ = false;
                if (result.kind == UserOrderResultKind::Success) {
                    orderId_.clear();
                    charging_ = false;
                    setSource(QStringLiteral("服务端已确认取消预约。"));
                    setPageState(Ui::PageState::Normal);
                    emit primaryRequested(PrimaryPage::Home);
                } else {
                    setSource(result.message);
                    showRecoverableState(this,
                        result.kind == UserOrderResultKind::Offline
                            ? Ui::PageState::Offline : Ui::PageState::Error);
                    cancel_->setEnabled(result.kind != UserOrderResultKind::Offline);
                }
                return;
            }
            const UserOrderAction expected = charging_ ? UserOrderAction::Stop
                                                       : UserOrderAction::Start;
            if (result.action != expected) return;
            operationPending_ = false;
            if (result.kind != UserOrderResultKind::Success) {
                setSource(result.message);
                showRecoverableState(this,
                    result.kind == UserOrderResultKind::Offline
                        ? Ui::PageState::Offline : Ui::PageState::Error);
                cancel_->setEnabled(!charging_
                                    && result.kind != UserOrderResultKind::Offline);
                return;
            }
            setPageState(Ui::PageState::Normal);
            if (!charging_) {
                charging_ = true;
                cancel_->hide();
                setSource(QStringLiteral("服务端已确认开始充电 · 订单：") + result.orderId);
                setAction(QStringLiteral("停止充电并结算"));
            } else {
                disablePolling();
                setSource(QStringLiteral("服务端已生成账单：%1 kWh · %2 元")
                              .arg(result.energyKwh, result.amountYuan));
                emit readyForSettlement(result.orderId);
            }
        });
        connect(provider_, &IUserOrderProvider::queryFinished, this,
                [this](const UserOrderQueryResult &result) {
            if (result.query != UserOrderQueryKind::Detail) return;
            detailPending_ = false;
            if (result.kind != UserOrderResultKind::Success) {
                setSource(result.message);
                setPageState(result.kind == UserOrderResultKind::Offline
                                 ? Ui::PageState::Offline : Ui::PageState::Error);
                return;
            }
            if (result.order.orderId != orderId_) return;
            const UserOrderSnapshot &order = result.order;
            lastQueryTime_ = QDateTime::currentDateTime().toString(Qt::ISODate);
            detail_->setText(QStringLiteral("状态：%1\n电量：%2 kWh\n锁定单价：%3 元/kWh\n金额：%4 元\n开始时间：%5\n结束时间：%6\n最后查询时间：%7")
                                 .arg(order.status, order.energyKwh,
                                      order.unitPriceYuanPerKwh, order.amountYuan,
                                      order.startedAt.isEmpty() ? QStringLiteral("--") : order.startedAt,
                                      order.endedAt.isEmpty() ? QStringLiteral("--") : order.endedAt,
                                      lastQueryTime_));
            charging_ = order.status == QStringLiteral("CHARGING");
            if (order.status == QStringLiteral("PENDING_PAYMENT")) {
                disablePolling();
                emit readyForSettlement(order.orderId);
                return;
            }
            setAction(charging_ ? QStringLiteral("停止充电并结算")
                                : QStringLiteral("开始充电"),
                      order.status == QStringLiteral("CHARGING")
                          || order.status == QStringLiteral("RESERVED"));
            cancel_->setVisible(order.status == QStringLiteral("RESERVED"));
            cancel_->setEnabled(order.status == QStringLiteral("RESERVED"));
            setPageState(Ui::PageState::Normal);
        });
        connect(provider_, &IUserOrderProvider::connectivityLost, this, [this] {
            setSource(QStringLiteral("连接已断开，当前订单数据可能已过期。"));
            setPageState(Ui::PageState::Offline);
            cancel_->setEnabled(false);
        });
        connect(provider_, &IUserOrderProvider::connectivityRestored, this, [this] {
            setSource(QStringLiteral("正式订单服务 · 连接已恢复，请重新提交操作。"));
            setPageState(Ui::PageState::Normal);
            cancel_->setEnabled(!charging_ && !orderId_.isEmpty());
        });
    } else {
        connect(primaryAction(), &QPushButton::clicked, this,
                [this] { emit secondaryRequested(SecondaryPage::Settlement); });
    }
}

void ChargingStatusPage::setOrderId(const QString &orderId)
{
    orderId_ = orderId;
    charging_ = false;
    if (provider_) {
        setAction(QStringLiteral("开始充电"));
        cancel_->show();
        operationPending_ = false;
        detailPending_ = false;
        setPageState(Ui::PageState::Loading);
        enablePolling();
        QTimer::singleShot(0, this, &ChargingStatusPage::refreshDetail);
    }
}

void ChargingStatusPage::submit()
{
    if (!provider_) return;
    if (orderId_.isEmpty()) {
        setSource(QStringLiteral("尚无可操作的正式订单。"));
        showRecoverableState(this, Ui::PageState::Error);
        return;
    }
    setPageState(Ui::PageState::Loading);
    operationPending_ = true;
    cancel_->setEnabled(false);
    if (charging_) provider_->stop(orderId_); else provider_->start(orderId_);
}

void ChargingStatusPage::refreshDetail()
{
    if (provider_ && !operationPending_ && !detailPending_ && !orderId_.isEmpty()) {
        detailPending_ = true;
        provider_->queryDetail(orderId_);
    }
}

// S3 轮询入口：等价于刷新订单详情（仅在有待查订单时发起）。
void ChargingStatusPage::refresh()
{
    refreshDetail();
}

SettlementPage::SettlementPage(const PagePreview &preview, QWidget *parent)
    : SettlementPage(preview, nullptr, parent)
{
}

SettlementPage::SettlementPage(const PagePreview &preview,
                               IUserOrderProvider *provider, QWidget *parent)
    : FlowPage(provider ? formalPreview(preview, QStringLiteral("提交正式订单支付"))
                        : preview, parent)
    , provider_(provider)
{
    setObjectName("settlementPage");
    if (provider_) setNormalStateMessage(QStringLiteral("服务已连接，数据状态正常"));
    setAction(provider_ ? QStringLiteral("提交正式支付")
                        : QStringLiteral("确认支付（模拟）"));
    if (provider_) {
        setSource(QStringLiteral("正式钱包服务 · 支付与幂等规则由服务端裁决"));
        connect(primaryAction(), &QPushButton::clicked, this, &SettlementPage::submit);
        connect(provider_, &IUserOrderProvider::operationFinished, this,
                [this](const UserOrderResult &result) {
            if (result.action != UserOrderAction::Pay) return;
            if (result.kind == UserOrderResultKind::Success) {
                paymentUncertain_ = false;
                paymentComplete_ = true;
                setSource(QStringLiteral("服务端已确认支付成功。"));
                setAction(QStringLiteral("支付已完成"), false);
                setPageState(Ui::PageState::Normal);
                emit paymentConfirmed();
            } else {
                static const QStringList uncertainCodes{
                    QStringLiteral("TIMEOUT"), QStringLiteral("OFFLINE"),
                    QStringLiteral("DISCONNECTED"), QStringLiteral("MALFORMED_JSON"),
                    QStringLiteral("MALFORMED_ENVELOPE"),
                    QStringLiteral("REQUEST_ID_MISMATCH"),
                    QStringLiteral("PROTOCOL_ERROR")};
                if (uncertainCodes.contains(result.errorCode)) {
                    paymentUncertain_ = true;
                    setSource(QStringLiteral("支付结果未确认，正在查询服务端订单状态。"));
                    verifyPaymentState();
                    return;
                }
                setSource(result.message);
                showRecoverableState(this,
                    result.kind == UserOrderResultKind::Offline
                        ? Ui::PageState::Offline : Ui::PageState::Error);
            }
        });
        connect(provider_, &IUserOrderProvider::queryFinished, this,
                [this](const UserOrderQueryResult &result) {
            if (!verificationPending_ || result.query != UserOrderQueryKind::Detail) return;
            verificationPending_ = false;
            if (result.kind != UserOrderResultKind::Success || !result.hasOrder
                || result.order.orderId != orderId_) {
                setSource(QStringLiteral("支付状态仍无法确认：") + result.message);
                setAction(QStringLiteral("等待状态确认"), false);
                setPageState(result.kind == UserOrderResultKind::Offline
                                 ? Ui::PageState::Offline : Ui::PageState::Error);
                return;
            }
            if (result.order.status == QStringLiteral("FINISHED")) {
                paymentUncertain_ = false;
                paymentComplete_ = true;
                setSource(QStringLiteral("订单状态已确认：支付完成。"));
                setAction(QStringLiteral("支付已完成"), false);
                setPageState(Ui::PageState::Normal);
                emit paymentConfirmed();
            } else if (result.order.status == QStringLiteral("PENDING_PAYMENT")) {
                paymentUncertain_ = false;
                setSource(QStringLiteral("订单仍待支付，可以重新提交支付。"));
                setAction(QStringLiteral("重新提交正式支付"), true);
                setPageState(Ui::PageState::Normal);
            } else {
                setSource(QStringLiteral("订单状态为 %1，无法确认是否应继续支付。")
                              .arg(result.order.status));
                setAction(QStringLiteral("等待状态确认"), false);
                setPageState(Ui::PageState::Error);
            }
        });
        connect(provider_, &IUserOrderProvider::connectivityLost, this, [this] {
            setSource(QStringLiteral("连接已断开，当前结算数据可能已过期。"));
            setPageState(Ui::PageState::Offline);
        });
        connect(provider_, &IUserOrderProvider::connectivityRestored, this, [this] {
            if (paymentUncertain_ && !verificationPending_) {
                verifyPaymentState();
            } else if (!paymentComplete_) {
                setSource(QStringLiteral("正式钱包服务 · 连接已恢复"));
                setPageState(Ui::PageState::Normal);
            }
        });
    }
    addPrimaryButton(QStringLiteral("返回首页"), PrimaryPage::Home);
}

void SettlementPage::setOrderId(const QString &orderId)
{
    orderId_ = orderId;
    paymentUncertain_ = false;
    verificationPending_ = false;
    paymentComplete_ = false;
    setSource(QStringLiteral("正式钱包服务 · 支付与幂等规则由服务端裁决"));
    setAction(QStringLiteral("提交正式支付"), !orderId_.isEmpty());
    setPageState(Ui::PageState::Normal);
}

void SettlementPage::submit()
{
    if (!provider_ || orderId_.isEmpty()) {
        setSource(QStringLiteral("尚无可支付的正式订单。"));
        showRecoverableState(this, Ui::PageState::Error);
        return;
    }
    setPageState(Ui::PageState::Loading);
    provider_->pay(orderId_);
}

void SettlementPage::verifyPaymentState()
{
    if (!provider_ || orderId_.isEmpty() || verificationPending_) return;
    verificationPending_ = true;
    setAction(QStringLiteral("正在确认支付状态"), false);
    setPageState(Ui::PageState::Loading);
    provider_->queryDetail(orderId_);
}
