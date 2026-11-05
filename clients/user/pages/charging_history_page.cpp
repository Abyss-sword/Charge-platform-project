#include "pages/charging_history_page.h"
#include "adapters/user_order_provider.h"
#include <QHeaderView>
#include <QTableWidget>
ChargingHistoryPage::ChargingHistoryPage(const PagePreview &preview, QWidget *parent) : PreviewPage(preview, parent)
{
    setObjectName("chargingHistoryPage");
    appendCards(preview.cards);
}

ChargingHistoryPage::ChargingHistoryPage(const PagePreview &preview,
                                         IUserOrderProvider *provider, qint64 userId,
                                         QWidget *parent)
    : PreviewPage({preview.title, QStringLiteral("历史订单"), {}, {},
                   Ui::PageState::Normal}, parent)
    , provider_(provider), userId_(userId)
{
    setObjectName("chargingHistoryPage");
    enablePolling();  // S3：可见页每 3 秒刷新
    setNormalStateMessage(QStringLiteral("服务已连接，数据状态正常"));
    setSource(QStringLiteral("正式订单服务 · ORDER_HISTORY"));
    rows_ = new QTableWidget(0, 4, this);
    rows_->setObjectName(QStringLiteral("historyOrdersTable"));
    rows_->setHorizontalHeaderLabels({QStringLiteral("订单"), QStringLiteral("状态"),
                                      QStringLiteral("电量(kWh)"), QStringLiteral("金额(元)")});
    rows_->horizontalHeader()->setStretchLastSection(true);
    rows_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    rows_->setMinimumHeight(280);
    addContent(rows_);
    connect(provider_, &IUserOrderProvider::queryFinished, this,
            [this](const UserOrderQueryResult &result) {
        if (result.query != UserOrderQueryKind::History) return;
        if (result.kind != UserOrderResultKind::Success) {
            setSource(result.message);
            setPageState(result.kind == UserOrderResultKind::Offline
                             ? Ui::PageState::Offline : Ui::PageState::Error);
            return;
        }
        rows_->setRowCount(result.orders.size());
        for (int row = 0; row < result.orders.size(); ++row) {
            const UserOrderSnapshot &order = result.orders.at(row);
            const QStringList values{order.orderId, order.status, order.energyKwh,
                                     order.amountYuan};
            for (int column = 0; column < values.size(); ++column)
                rows_->setItem(row, column, new QTableWidgetItem(values.at(column)));
        }
        setSource(QStringLiteral("正式订单服务 · 已同步 %1 笔历史订单")
                      .arg(result.orders.size()));
        setPageState(result.orders.isEmpty() ? Ui::PageState::Empty
                                              : Ui::PageState::Normal);
    });
    connect(provider_, &IUserOrderProvider::connectivityLost, this, [this] {
        setSource(QStringLiteral("连接已断开，当前历史订单可能已过期。"));
        setPageState(Ui::PageState::Offline);
    });
    connect(provider_, &IUserOrderProvider::connectivityRestored, this, [this] {
        if (isVisible()) refresh();
    });
}

void ChargingHistoryPage::refresh()
{
    if (!provider_ || userId_ <= 0) return;
    markRefreshing();
    provider_->queryHistory(userId_);
}
