#include "pages/station_list_page.h"

#include "adapters/mock_station_provider.h"
#include "theme.h"

#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QLayoutItem>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

NearbyStationsPage::NearbyStationsPage(const PagePreview &preview, QWidget *parent)
    : NearbyStationsPage(preview, nullptr, parent)
{
}

NearbyStationsPage::NearbyStationsPage(const PagePreview &preview,
                                       IStationProvider *provider,
                                       QWidget *parent)
    : PreviewPage(preview, parent)
    , provider_(provider ? provider : new MockStationProvider(this))
    , mockMode_(provider == nullptr || qobject_cast<MockStationProvider *>(provider))
{
    setObjectName("nearbyStationsPage");
    if (!mockMode_) enablePolling();  // S3：仅正式模式轮询，避免 mock 页被 refresh 置 Error
    if (!mockMode_) setNormalStateMessage(QStringLiteral("服务已连接，数据状态正常"));
    setSource(mockMode_ ? preview.source
                        : QStringLiteral("当前位置：广州天河（基础定位）"));
    if (auto *selector = findChild<QComboBox *>(QStringLiteral("mockStateSelector"))) {
        selector->hide();
    }

    refresh_ = new QPushButton(QStringLiteral("刷新附近站点"), this);
    refresh_->setObjectName(QStringLiteral("stationRefreshAction"));
    refresh_->setMinimumHeight(Ui::Theme::TouchHeight);
    addStateControl(refresh_);

    auto *analysis = new QPushButton(QStringLiteral("智能推荐与预测"), this);
    analysis->setObjectName(QStringLiteral("userAnalysisEntryAction"));
    analysis->setMinimumHeight(Ui::Theme::TouchHeight);
    addContent(analysis);
    connect(analysis, &QPushButton::clicked, this,
            [this] { emit secondaryRequested(SecondaryPage::Analysis); });

    auto *mode = new QComboBox(this);
    mode->setObjectName("stationMode");
    mode->setAccessibleName(QStringLiteral("站点显示模式"));
    mode->addItems({QStringLiteral("列表"), QStringLiteral("地图")});
    addContent(mode);
    auto *views = new QStackedWidget(this);
    views->setObjectName("stationViews");
    auto *list = new QWidget(views);
    stationLayout_ = new QVBoxLayout(list);
    stationLayout_->setContentsMargins(0, 0, 0, 0);
    stationLayout_->setSpacing(Ui::Theme::Space);
    views->addWidget(list);
    auto *fallback = new QLabel(
        mockMode_ ? QStringLiteral("△ 离线演示地图\n当前使用模拟位置：广州天河。\n可切回列表查看站点地址与可用桩。")
                  : QStringLiteral("△ 地图服务暂不可用\n当前仅显示站点列表数据。\n可切回列表查看站点地址与可用桩。"), views);
    fallback->setWordWrap(true);
    fallback->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    views->addWidget(fallback);
    addContent(views);
    stationSelector_ = new QComboBox(this);
    stationSelector_->setObjectName(QStringLiteral("stationSelector"));
    stationSelector_->setPlaceholderText(QStringLiteral("请选择站点"));
    addContent(stationSelector_);
    detail_ = new QPushButton(QStringLiteral("查看站点详情"), this);
    detail_->setObjectName("stationDetailAction");
    detail_->setMinimumHeight(Ui::Theme::TouchHeight);
    detail_->setEnabled(false);
    addContent(detail_);
    connect(detail_, &QPushButton::clicked, this, [this] {
        const qint64 stationId = stationSelector_->currentData().toLongLong();
        if (stationId > 0) emit stationDetailRequested(stationId);
    });
    connect(mode, &QComboBox::currentIndexChanged, views, &QStackedWidget::setCurrentIndex);
    connect(refresh_, &QPushButton::clicked, this, &NearbyStationsPage::refresh);
    connect(provider_, &IStationProvider::queryFinished,
            this, &NearbyStationsPage::handleResult);
    connect(provider_, &IStationProvider::connectivityLost, this, [this] {
        setSource(QStringLiteral("连接已断开，当前站点数据可能已过期。"));
        setPageState(Ui::PageState::Offline);
    });
    connect(provider_, &IStationProvider::connectivityRestored, this, [this] {
        if (isVisible()) refresh();
    });
    QTimer::singleShot(0, this, &NearbyStationsPage::refresh);
}

void NearbyStationsPage::refresh()
{
    if (!provider_) {
        handleResult({StationQueryResultKind::Error, QStringLiteral("站点服务不可用。"), {},
                      QStringLiteral("UNAVAILABLE")});
        return;
    }
    refresh_->setEnabled(false);
    markRefreshing();
    provider_->queryNearby({23.1351, 113.3245});
}

void NearbyStationsPage::handleResult(const StationQueryResult &result)
{
    refresh_->setEnabled(true);
    if (result.kind == StationQueryResultKind::Offline) {
        setSource(QStringLiteral("站点服务离线 · ") + result.message);
        setPageState(Ui::PageState::Offline);
        return;
    }
    if (result.kind == StationQueryResultKind::Error) {
        setSource(QStringLiteral("站点查询失败 · ") + result.message);
        setPageState(Ui::PageState::Error);
        return;
    }

    renderStations(result.stations);
    stationSelector_->clear();
    for (const StationSummary &station : result.stations)
        stationSelector_->addItem(station.name, station.stationId);
    if (stationSelector_->count() > 0) stationSelector_->setCurrentIndex(0);
    detail_->setEnabled(stationSelector_->count() > 0);
    setSource(mockMode_
                  ? QStringLiteral("离线演示模式 · 模拟站点数据")
                  : QStringLiteral("当前位置：广州天河（基础定位）· 站点查询已更新"));
    setPageState(result.stations.isEmpty() ? Ui::PageState::Empty
                                           : Ui::PageState::Normal);
    if (!result.stations.isEmpty()) {
        auto *stateMessage = stateWidget()->findChild<QLabel *>(
            QStringLiteral("stateMessage"));
        if (stateMessage && !mockMode_) {
            stateMessage->setText(QStringLiteral("◇ 已加载服务端站点数据"));
        }
    }
}

void NearbyStationsPage::renderStations(const QList<StationSummary> &stations)
{
    while (QLayoutItem *item = stationLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const StationSummary &station : stations) {
        const QString body = QStringLiteral(
            "%1 km · 可用 %2 / 共 %3 桩\n%4 元/kWh\n地址：%5\n坐标：%6, %7")
            .arg(QString::number(station.distanceKm, 'f', 2))
            .arg(station.availablePiles)
            .arg(station.totalPiles)
            .arg(station.priceYuanPerKwh, station.address)
            .arg(QString::number(station.latitude, 'f', 6),
                 QString::number(station.longitude, 'f', 6));
        QFrame *card = createCard(station.name, body, false, this);
        card->setObjectName(QStringLiteral("stationCard"));
        card->setProperty("stationId", station.stationId);
        auto *navigate = new QPushButton(QStringLiteral("导航到此站"), card);
        navigate->setObjectName(QStringLiteral("stationNavigationAction"));
        navigate->setMinimumHeight(Ui::Theme::TouchHeight);
        if (auto *cardLayout = qobject_cast<QVBoxLayout *>(card->layout()))
            cardLayout->addWidget(navigate);
        connect(navigate, &QPushButton::clicked, this, [this, station] {
            emit navigationRequested({23.1351, 113.3245, station.latitude,
                                       station.longitude, station.name});
        });
        stationLayout_->addWidget(card);
    }
}
