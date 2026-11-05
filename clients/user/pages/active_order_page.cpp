#include "pages/active_order_page.h"
#include "adapters/user_order_provider.h"
#include "theme.h"
#include <QLabel>
#include <QPushButton>
ActiveOrderPage::ActiveOrderPage(const PagePreview &preview, QWidget *parent) : PreviewPage(preview, parent)
{
    setObjectName("activeOrderPage");
    appendCards(preview.cards);
    if (preview.state == Ui::PageState::Degraded) return;
    auto *charging = new QPushButton(QStringLiteral("查看充电状态（模拟）"), this);
    charging->setMinimumHeight(Ui::Theme::TouchHeight);
    addContent(charging);
    connect(charging, &QPushButton::clicked, this, [this] { emit secondaryRequested(SecondaryPage::ChargingStatus); });
    auto *recovery = new QPushButton(QStringLiteral("恢复活动订单（模拟）"), this);
    recovery->setMinimumHeight(Ui::Theme::TouchHeight);
    addContent(recovery);
    connect(recovery, &QPushButton::clicked, this, [this] { emit secondaryRequested(SecondaryPage::ActiveOrderRecovery); });
}

ActiveOrderPage::ActiveOrderPage(const PagePreview &preview, IUserOrderProvider *provider,
                                 qint64 userId, QWidget *parent)
    : PreviewPage({preview.title, QStringLiteral("当前活动订单"), {}, {},
                   Ui::PageState::Normal}, parent)
    , provider_(provider), userId_(userId)
{
    setObjectName("activeOrderPage");
    enablePolling();  // S3：可见页每 3 秒刷新
    setNormalStateMessage(QStringLiteral("服务已连接，数据状态正常"));
    setSource(QStringLiteral("正式订单服务 · ORDER_ACTIVE"));
    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    addContent(summary_);
    continue_ = new QPushButton(QStringLiteral("继续处理订单"), this);
    continue_->setObjectName(QStringLiteral("continueActiveOrderAction"));
    continue_->setMinimumHeight(Ui::Theme::TouchHeight);
    continue_->setEnabled(false);
    addContent(continue_);
    connect(continue_, &QPushButton::clicked, this,
            [this] { if (!order_.orderId.isEmpty()) emit continueOrderRequested(order_); });
    connect(provider_, &IUserOrderProvider::queryFinished, this,
            [this](const UserOrderQueryResult &result) {
        if (result.query != UserOrderQueryKind::Active) return;
        if (result.kind != UserOrderResultKind::Success) {
            setSource(result.message);
            setPageState(result.kind == UserOrderResultKind::Offline
                             ? Ui::PageState::Offline : Ui::PageState::Error);
            continue_->setEnabled(false);
            emit activeOrderLoaded(result);
            return;
        }
        if (!result.hasOrder) {
            order_ = {};
            summary_->setText(QStringLiteral("当前没有活动订单。"));
            continue_->setEnabled(false);
            setPageState(Ui::PageState::Empty);
            emit activeOrderLoaded(result);
            return;
        }
        order_ = result.order;
        summary_->setText(QStringLiteral("订单：%1\n电桩：%2\n状态：%3\n电量：%4 kWh\n金额：%5 元")
                              .arg(order_.orderId).arg(order_.pileId).arg(order_.status,
                                  order_.energyKwh, order_.amountYuan));
        continue_->setEnabled(true);
        setSource(QStringLiteral("正式订单服务 · 活动订单已更新"));
        setPageState(Ui::PageState::Normal);
        emit activeOrderLoaded(result);
    });
    connect(provider_, &IUserOrderProvider::connectivityLost, this, [this] {
        setSource(QStringLiteral("连接已断开，当前活动订单可能已过期。"));
        setPageState(Ui::PageState::Offline);
        continue_->setEnabled(false);
    });
    connect(provider_, &IUserOrderProvider::connectivityRestored, this, [this] {
        if (isVisible()) refresh();
    });
}

void ActiveOrderPage::refresh()
{
    if (!provider_ || userId_ <= 0) return;
    markRefreshing();
    provider_->queryActive(userId_);
}
