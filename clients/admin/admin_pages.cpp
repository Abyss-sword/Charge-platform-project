#include "admin_pages.h"
#include "admin_provider.h"
#include "theme.h"
#include "widgets/enum_labels.h"
#include <QDateEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QComboBox>
#include <QDateTime>
#include <QToolTip>
#include <QVBoxLayout>
#include <utility>
#ifdef ADMIN_HAS_CHARTS
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QPieSeries>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QBarCategoryAxis>
#endif
static void applyRows(QTableWidget *table, const QList<QStringList> &rows) { table->setRowCount(rows.size()); for(int r=0;r<rows.size();++r) for(int c=0;c<rows[r].size();++c) table->setItem(r,c,new QTableWidgetItem(rows[r][c])); }
AdminPageWidget::AdminPageWidget(const AdminPreview &p, QWidget *parent) : Ui::PageScaffold(p.title,p.subtitle,parent) { setSource(p.source); if(p.source.isEmpty())setNormalStateMessage(QStringLiteral("服务已连接，数据状态正常")); for (const auto &c:p.cards) addCard(c, QStringLiteral("SIMULATED · 仅用于界面预览")); setPageState(p.state); refreshTimer_.setInterval(3000); connect(&refreshTimer_,&QTimer::timeout,this,[this]{ if (isVisible()) refresh(); }); }
void AdminPageWidget::refresh() {}
void AdminPageWidget::showEvent(QShowEvent *event) { Ui::PageScaffold::showEvent(event); refreshTimer_.start(); }
void AdminPageWidget::hideEvent(QHideEvent *event) { Ui::PageScaffold::hideEvent(event); refreshTimer_.stop(); }
namespace {
// 总览/营收页共用：从状态分布构造图表短标签（中文）。
QString pileStatusLabel(const QString &value) { return Ui::labelFor(Ui::pileStatusOptions(), value); }
QString orderStatusLabel(const QString &value)
{
    static const QList<std::pair<QString, QString>> labels = {
        {QStringLiteral("RESERVED"), QStringLiteral("已预约")},
        {QStringLiteral("CHARGING"), QStringLiteral("充电中")},
        {QStringLiteral("PENDING_PAYMENT"), QStringLiteral("待支付")},
        {QStringLiteral("FINISHED"), QStringLiteral("已完成")},
        {QStringLiteral("CANCELED"), QStringLiteral("已取消")},
        {QStringLiteral("EXPIRED"), QStringLiteral("已过期")}};
    for (const auto &label : labels) if (label.first == value) return label.second;
    return value;
}
}
// 总览页：顶部指标卡（口径/时间/快照版本）+ 桩状态环图 + 24h 负荷折线 + 订单柱图
// + 最新告警/待处理工单/异常桩/最近订单清单；单请求快照，版本回退不覆盖已显示数据。
OverviewPage::OverviewPage(const AdminPreview &p,QWidget *parent):AdminPageWidget(p,parent){setObjectName("OverviewPage");}
OverviewPage::OverviewPage(const AdminPreview &p,IAdminProvider *provider,QString token,QWidget *parent)
    :AdminPageWidget({p.title,QStringLiteral("正式运营总览 · 每3秒自动刷新"),{},{},Ui::PageState::Normal},parent),provider_(provider),token_(std::move(token)){
    setObjectName("OverviewPage"); setNormalStateMessage(QStringLiteral("服务已连接，数据状态正常")); setSource(QStringLiteral("正式运营服务 · ADMIN_OVERVIEW"));
    snapshotLine_=new QLabel(QStringLiteral("口径：UTC 统计 · 快照版本：-- · 服务器时间：--"),this); snapshotLine_->setObjectName(QStringLiteral("overviewSnapshotLine")); snapshotLine_->setStyleSheet(QStringLiteral("color:#6b7280;font-size:11px;")); addContent(snapshotLine_);
    auto *cards=new QWidget(this); auto *grid=new QHBoxLayout(cards); grid->setContentsMargins(0,0,0,0); grid->setSpacing(8);
    todayRevenue_=new Ui::MetricCard(QStringLiteral("今日营收"),cards); todayRevenue_->setObjectName(QStringLiteral("overviewTodayRevenue"));
    monthRevenue_=new Ui::MetricCard(QStringLiteral("本月营收"),cards); monthRevenue_->setObjectName(QStringLiteral("overviewMonthRevenue"));
    serviceCount_=new Ui::MetricCard(QStringLiteral("服务次数"),cards); serviceCount_->setObjectName(QStringLiteral("overviewServiceCount"));
    orderCount_=new Ui::MetricCard(QStringLiteral("订单数"),cards); orderCount_->setObjectName(QStringLiteral("overviewOrderCount"));
    onlinePiles_=new Ui::MetricCard(QStringLiteral("在线桩"),cards); onlinePiles_->setObjectName(QStringLiteral("overviewOnlinePiles"));
    availableRate_=new Ui::MetricCard(QStringLiteral("可用桩占比"),cards); availableRate_->setObjectName(QStringLiteral("overviewAvailableRate"));
    pendingAlarms_=new Ui::MetricCard(QStringLiteral("待处理告警"),cards); pendingAlarms_->setObjectName(QStringLiteral("overviewPendingAlarms"));
    for (auto *card:{todayRevenue_,monthRevenue_,serviceCount_,orderCount_,onlinePiles_,availableRate_,pendingAlarms_}) grid->addWidget(card,1);
    addContent(cards);
#ifdef ADMIN_HAS_CHARTS
    auto *charts=new QWidget(this); auto *columns=new QHBoxLayout(charts); columns->setContentsMargins(0,0,0,0); columns->setSpacing(8);
    auto makeView=[this,charts,columns](const QString &name){ auto *view=new QChartView(new QChart(),charts); view->setObjectName(name); view->setRenderHint(QPainter::Antialiasing); view->setMinimumHeight(200); columns->addWidget(view,1); return view; };
    pileRingView_=makeView(QStringLiteral("overviewPileRing"));
    loadLineView_=makeView(QStringLiteral("overviewLoadLine"));
    orderBarView_=makeView(QStringLiteral("overviewOrderBar"));
    addContent(charts);
#else
    addContent(new QLabel(QStringLiteral("图表组件不可用，当前仅显示指标与清单。"),this));
#endif
    auto *lists=new QWidget(this); auto *rows=new QHBoxLayout(lists); rows->setContentsMargins(0,0,0,0); rows->setSpacing(8);
    auto makeTable=[this,lists,rows](const QString &name,const QStringList &headers){ auto *t=new QTableWidget(0,headers.size(),lists); t->setObjectName(name); t->setHorizontalHeaderLabels(headers); t->horizontalHeader()->setStretchLastSection(true); t->setMinimumHeight(150); t->setEditTriggers(QAbstractItemView::NoEditTriggers); rows->addWidget(t,1); return t; };
    alarmList_=makeTable(QStringLiteral("overviewLatestAlarms"),{QStringLiteral("告警"),QStringLiteral("类型"),QStringLiteral("严重度"),QStringLiteral("状态")});
    ticketList_=makeTable(QStringLiteral("overviewOpenTickets"),{QStringLiteral("工单"),QStringLiteral("类型"),QStringLiteral("状态"),QStringLiteral("更新时间")});
    abnormalList_=makeTable(QStringLiteral("overviewAbnormalPiles"),{QStringLiteral("桩"),QStringLiteral("站点"),QStringLiteral("状态"),QStringLiteral("心跳")});
    recentOrders_=makeTable(QStringLiteral("overviewRecentOrders"),{QStringLiteral("订单"),QStringLiteral("状态"),QStringLiteral("金额")});
    addContent(lists);
    connect(alarmList_,&QTableWidget::cellClicked,this,[this](int,int){ emit pageRequested(6); });   // AdminPage::AlertsTickets
    connect(ticketList_,&QTableWidget::cellClicked,this,[this](int,int){ emit pageRequested(6); });
    connect(abnormalList_,&QTableWidget::cellClicked,this,[this](int,int){ emit pageRequested(3); }); // AdminPage::PileDevices
    connect(recentOrders_,&QTableWidget::cellClicked,this,[this](int,int){ emit pageRequested(4); }); // AdminPage::Orders
    connect(provider_,&IAdminProvider::overviewFinished,this,[this](const AdminOverviewResult &r){
        if (r.kind!=AdminResultKind::Success){setSource(r.message);setPageState(r.kind==AdminResultKind::Offline?Ui::PageState::Offline:Ui::PageState::Error);return;}
        // 旧快照丢弃：仅接受比当前更大的快照版本（S1-A 联动契约）。
        if (lastSnapshotVersion_>=0 && r.snapshotVersion<lastSnapshotVersion_){ setPageState(Ui::PageState::Stale); return; }
        applySnapshot(r); lastSnapshotVersion_=r.snapshotVersion;
    });
    connect(provider_,&IAdminProvider::connectivityLost,this,[this]{setSource(QStringLiteral("连接已断开，当前总览数据可能已过期。"));setPageState(Ui::PageState::Offline);});connect(provider_,&IAdminProvider::connectivityRestored,this,[this]{if(isVisible())refresh();});
}
// 把一次 ADMIN_OVERVIEW 快照渲染到指标卡/图表/清单；数据为空走 Empty 态。
void OverviewPage::applySnapshot(const AdminOverviewResult &r)
{
    // 指标卡：口径行固定“UTC 统计 · 已支付口径 · 快照版本”。
    const QString caption = QStringLiteral("UTC 统计 · 已支付口径 · 快照 v%1 · %2")
                                .arg(r.snapshotVersion).arg(r.serverTimeUtc);
    todayRevenue_->setValue(r.todayRevenueYuan, QStringLiteral("元"));
    monthRevenue_->setValue(r.monthRevenueYuan, QStringLiteral("元"));
    serviceCount_->setValue(QString::number(r.todayServiceCount), QStringLiteral("次"));
    orderCount_->setValue(QString::number(r.todayOrderCount), QStringLiteral("单"));
    pendingAlarms_->setValue(QString::number(r.pendingAlarmCount), QStringLiteral("条"));
    // 在线桩/可用桩占比由状态分布推导：在线=总数-离线；可用=空闲/总数。
    int totalPiles = 0; int offlinePiles = 0; int idlePiles = 0;
    for (auto it = r.pileStatusCounts.constBegin(); it != r.pileStatusCounts.constEnd(); ++it) {
        const int count = it.value().toInt();
        totalPiles += count;
        if (it.key() == QStringLiteral("OFFLINE")) offlinePiles += count;
        if (it.key() == QStringLiteral("IDLE")) idlePiles += count;
    }
    onlinePiles_->setValue(QString::number(totalPiles - offlinePiles),
                           QStringLiteral("/%1 台").arg(totalPiles));
    availableRate_->setValue(totalPiles
                                 ? QStringLiteral("%1%").arg(100.0 * idlePiles / totalPiles, 0, 'f', 1)
                                 : QStringLiteral("--"),
                             QString());
    for (auto *card : {todayRevenue_, monthRevenue_, serviceCount_, orderCount_,
                       onlinePiles_, availableRate_, pendingAlarms_}) {
        card->setCaption(caption);
    }
    snapshotLine_->setText(caption);
#ifdef ADMIN_HAS_CHARTS
    // 桩状态环图。
    {
        auto *chart = new QChart();
        chart->setTitle(QStringLiteral("电桩状态分布"));
        auto *series = new QPieSeries(chart);
        for (auto it = r.pileStatusCounts.constBegin(); it != r.pileStatusCounts.constEnd(); ++it) {
            series->append(pileStatusLabel(it.key()), it.value().toDouble());
        }
        if (series->count() > 0) {
            chart->addSeries(series);
            chart->legend()->setAlignment(Qt::AlignRight);
        } else {
            delete series;
        }
        pileRingView_->setChart(chart);
    }
    // 24h 负荷折线。
    {
        auto *chart = new QChart();
        chart->setTitle(QStringLiteral("近24小时负荷（kWh/小时，UTC）"));
        auto *series = new QLineSeries(chart);
        double maxKwh = 0;
        int index = 0;
        for (const auto &point : r.loadSeries24h) {
            const QVariantMap row = point.toMap();
            const double kwh = row.value(QStringLiteral("kwh")).toString().toDouble();
            series->append(index, kwh);
            if (kwh > maxKwh) maxKwh = kwh;
            ++index;
        }
        auto *xAxis = new QValueAxis(chart);
        xAxis->setRange(0, qMax(23, r.loadSeries24h.size() - 1));
        xAxis->setTitleText(QStringLiteral("小时序"));
        auto *yAxis = new QValueAxis(chart);
        yAxis->setTitleText(QStringLiteral("电量（kWh）"));
        yAxis->setLabelFormat(QStringLiteral("%.2f"));
        yAxis->setRange(0, qMax(1.0, maxKwh * 1.2));
        chart->addSeries(series);
        chart->addAxis(xAxis, Qt::AlignBottom);
        chart->addAxis(yAxis, Qt::AlignLeft);
        series->attachAxis(xAxis);
        series->attachAxis(yAxis);
        chart->legend()->hide();
        loadLineView_->setChart(chart);
    }
    // 订单状态柱图。
    {
        auto *chart = new QChart();
        chart->setTitle(QStringLiteral("订单状态分布"));
        auto *set = new QBarSet(QStringLiteral("订单数"), chart);
        QStringList categories;
        for (auto it = r.orderStatusCounts.constBegin(); it != r.orderStatusCounts.constEnd(); ++it) {
            categories.append(orderStatusLabel(it.key()));
            set->append(it.value().toDouble());
        }
        auto *series = new QBarSeries(chart);
        series->append(set);
        auto *axis = new QBarCategoryAxis(chart);
        axis->append(categories);
        chart->addSeries(series);
        chart->setAxisX(axis, series);
        chart->legend()->hide();
        orderBarView_->setChart(chart);
    }
#endif
    // 清单：告警 / 工单 / 异常桩 / 最近订单（行点击跳转由构造时的信号连接处理）。
    {
        QList<QStringList> rows;
        for (const auto &value : r.latestAlarms) {
            const QVariantMap row = value.toMap();
            rows.append({row.value(QStringLiteral("alarmId")).toString(),
                         row.value(QStringLiteral("alarmType")).toString(),
                         row.value(QStringLiteral("severity")).toString(),
                         row.value(QStringLiteral("status")).toString()});
        }
        applyRows(alarmList_, rows);
    }
    {
        QList<QStringList> rows;
        for (const auto &value : r.openTickets) {
            const QVariantMap row = value.toMap();
            rows.append({row.value(QStringLiteral("ticketId")).toString(),
                         row.value(QStringLiteral("alarmType")).toString(),
                         row.value(QStringLiteral("status")).toString(),
                         row.value(QStringLiteral("updatedAt")).toString()});
        }
        applyRows(ticketList_, rows);
    }
    {
        QList<QStringList> rows;
        for (const auto &value : r.abnormalPiles) {
            const QVariantMap row = value.toMap();
            rows.append({row.value(QStringLiteral("pileSn")).toString(),
                         row.value(QStringLiteral("stationName")).toString(),
                         row.value(QStringLiteral("status")).toString(),
                         row.value(QStringLiteral("lastHeartbeatAt")).toString()});
        }
        applyRows(abnormalList_, rows);
    }
    {
        QList<QStringList> rows;
        for (const auto &value : r.recentOrders) {
            const QVariantMap row = value.toMap();
            rows.append({row.value(QStringLiteral("orderId")).toString(),
                         orderStatusLabel(row.value(QStringLiteral("status")).toString()),
                         row.value(QStringLiteral("amountYuan")).toString()});
        }
        applyRows(recentOrders_, rows);
    }
    const bool empty = r.todayServiceCount == 0 && r.todayOrderCount == 0
        && r.latestAlarms.isEmpty() && r.recentOrders.isEmpty();
    setPageState(empty ? Ui::PageState::Empty : Ui::PageState::Normal);
}
void OverviewPage::refresh(){if(!provider_)return;provider_->queryOverview(token_);}
RevenuePage::RevenuePage(const AdminPreview &p,QWidget *parent):AdminPageWidget(p,parent){setObjectName("RevenuePage");}
RevenuePage::RevenuePage(const AdminPreview &p,IAdminProvider *provider,QString token,QWidget *parent)
    :AdminPageWidget({p.title,QStringLiteral("正式营收分析 · 已支付口径（UTC）"),{},{},Ui::PageState::Normal},parent),provider_(provider),token_(std::move(token)){
    setObjectName("RevenuePage");setNormalStateMessage(QStringLiteral("服务已连接，数据状态正常"));setSource(QStringLiteral("正式运营服务 · ADMIN_REVENUE"));
    value_=new QLabel(QStringLiteral("总营收：--"),this); value_->setObjectName(QStringLiteral("totalRevenueMetric")); value_->setWordWrap(true); addContent(value_);
    today_=new QLabel(QStringLiteral("今日营收：--"),this); today_->setObjectName(QStringLiteral("todayRevenueMetric"));
    month_=new QLabel(QStringLiteral("本月营收：--"),this); month_->setObjectName(QStringLiteral("monthRevenueMetric")); addContent(today_); addContent(month_);
    // S1-B 区间候选栏：今日/近7日/近30日/自定义（自定义显示两个日期控件 + 应用按钮）。
    summaryRange_=new QComboBox(this); summaryRange_->setObjectName(QStringLiteral("revenueSummaryRange"));
    summaryRange_->addItem(QStringLiteral("今日"),1); summaryRange_->addItem(QStringLiteral("近7日"),7);
    summaryRange_->addItem(QStringLiteral("近30日"),30); summaryRange_->addItem(QStringLiteral("自定义"),0);
    addContent(summaryRange_);
    fromDate_=new QDateEdit(QDateTime::currentDateTimeUtc().date().addDays(-6),this); fromDate_->setObjectName(QStringLiteral("revenueFromDate"));
    fromDate_->setCalendarPopup(true); fromDate_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    toDate_=new QDateEdit(QDateTime::currentDateTimeUtc().date(),this); toDate_->setObjectName(QStringLiteral("revenueToDate"));
    toDate_->setCalendarPopup(true); toDate_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    fromDate_->hide(); toDate_->hide();
    addContent(fromDate_); addContent(toDate_);
    summaryApply_=new QPushButton(QStringLiteral("查询汇总"),this); summaryApply_->setObjectName(QStringLiteral("revenueSummaryApply"));
    summaryApply_->hide(); addContent(summaryApply_);
    auto *cardsRow=new QWidget(this); auto *cardsLayout=new QHBoxLayout(cardsRow); cardsLayout->setContentsMargins(0,0,0,0); cardsLayout->setSpacing(8);
    const QStringList cardTitles{QStringLiteral("区间营收"),QStringLiteral("订单数"),QStringLiteral("平均客单价"),QStringLiteral("总电量"),QStringLiteral("平均单价")};
    for (const auto &title:cardTitles){ auto *card=new Ui::MetricCard(title,cardsRow); card->setObjectName(QStringLiteral("revenueSummaryCard_%1").arg(cardTitles.indexOf(title))); summaryCards_.append(card); cardsLayout->addWidget(card,1); }
    addContent(cardsRow);
    summaryCaption_=new QLabel(QStringLiteral("口径：UTC 日 · 已支付（FINISHED） · 两位小数"),this); summaryCaption_->setObjectName(QStringLiteral("revenueSummaryCaption")); summaryCaption_->setStyleSheet(QStringLiteral("color:#6b7280;font-size:11px;")); addContent(summaryCaption_);
    stationRanking_=new QTableWidget(0,3,this); stationRanking_->setObjectName(QStringLiteral("revenueStationRanking"));
    stationRanking_->setHorizontalHeaderLabels({QStringLiteral("站点"),QStringLiteral("营收（元）"),QStringLiteral("订单数")});
    stationRanking_->horizontalHeader()->setStretchLastSection(true); stationRanking_->setMinimumHeight(120);
    stationRanking_->setEditTriggers(QAbstractItemView::NoEditTriggers); addContent(stationRanking_);
    hourHeat_=new QTableWidget(0,2,this); hourHeat_->setObjectName(QStringLiteral("revenueHourHeat"));
    hourHeat_->setHorizontalHeaderLabels({QStringLiteral("UTC小时"),QStringLiteral("营收（元）")});
    hourHeat_->horizontalHeader()->setStretchLastSection(true); hourHeat_->setMinimumHeight(150);
    hourHeat_->setEditTriggers(QAbstractItemView::NoEditTriggers); addContent(hourHeat_);
    range_=new QComboBox(this); range_->setObjectName(QStringLiteral("revenueTrendRange")); range_->addItem(QStringLiteral("近7日"),7); range_->addItem(QStringLiteral("近30日"),30); addContent(range_);
#ifdef ADMIN_HAS_CHARTS
    chartView_=new QChartView(new QChart(),this); chartView_->setObjectName(QStringLiteral("revenueTrendChart")); chartView_->setRenderHint(QPainter::Antialiasing); chartView_->setMinimumHeight(260); addContent(chartView_);
    hourHeatView_=new QChartView(new QChart(),this); hourHeatView_->setObjectName(QStringLiteral("revenueHourChart")); hourHeatView_->setRenderHint(QPainter::Antialiasing); hourHeatView_->setMinimumHeight(200); addContent(hourHeatView_);
#else
    addContent(new QLabel(QStringLiteral("趋势图组件不可用，当前仅显示营收指标。"),this));
#endif
    connect(range_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{
        if (!provider_ || !range_->isEnabled()) return;
        trendRequestDays_=range_->currentData().toInt(); range_->setEnabled(false);
        provider_->queryRevenueTrend(token_,trendRequestDays_);
    });
    // 汇总请求：切换范围或点应用按钮 → 以请求序号防旧覆盖。
    connect(summaryRange_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{
        const bool custom=summaryRange_->currentData().toInt()==0;
        fromDate_->setVisible(custom); toDate_->setVisible(custom); summaryApply_->setVisible(custom);
        requestSummary();
    });
    connect(summaryApply_,&QPushButton::clicked,this,[this]{ requestSummary(); });
    connect(provider_,&IAdminProvider::revenueSummaryFinished,this,[this](const AdminRevenueSummaryResult &r){
        // 旧响应丢弃：仅处理与最新请求序号一致的汇总（S1-B 防旧覆盖新）。
        if (r.kind!=AdminResultKind::Success){ setSource(r.message); return; }
        if (pendingSummarySeq_!=summaryRequestSeq_){ return; }
        if (summaryCards_.size()==5){
            summaryCards_[0]->setValue(r.totalRevenueYuan,QStringLiteral("元"));
            summaryCards_[1]->setValue(QString::number(r.orderCount),QStringLiteral("单"));
            summaryCards_[2]->setValue(r.avgOrderYuan,QStringLiteral("元/单"));
            summaryCards_[3]->setValue(r.totalEnergyKwh,QStringLiteral("kWh"));
            summaryCards_[4]->setValue(r.avgPriceYuan,QStringLiteral("元/kWh"));
        }
        QList<QStringList> rankingRows;
        for (const auto &value:r.stationRanking){ const QVariantMap row=value.toMap();
            rankingRows.append({row.value(QStringLiteral("stationName")).toString(),row.value(QStringLiteral("revenueYuan")).toString(),row.value(QStringLiteral("orderCount")).toString()}); }
        applyRows(stationRanking_,rankingRows);
        QList<QStringList> heatRows;
        QVariantList heatPoints;
        for (const auto &value:r.hourHistogram){ const QVariantMap row=value.toMap();
            const QString hour=row.value(QStringLiteral("hourUtc")).toString();
            const QString revenue=row.value(QStringLiteral("revenueYuan")).toString();
            heatRows.append({hour,revenue}); heatPoints.append(revenue.toDouble()); }
        applyRows(hourHeat_,heatRows);
#ifdef ADMIN_HAS_CHARTS
        auto *chart=new QChart(); chart->setTitle(QStringLiteral("分时段营收（UTC 小时）"));
        auto *set=new QBarSet(QStringLiteral("营收（元）"),chart);
        QStringList categories;
        for (const auto &point:heatPoints){ categories.append(QString::number(heatPoints.indexOf(point))); set->append(point.toDouble()); }
        auto *series=new QBarSeries(chart); series->append(set);
        auto *axis=new QBarCategoryAxis(chart); axis->append(categories);
        chart->addSeries(series); chart->setAxisX(axis,series); chart->legend()->hide();
        hourHeatView_->setChart(chart);
#endif
    });
    connect(provider_,&IAdminProvider::revenueFinished,this,[this](const AdminRevenueResult&r){if(r.kind!=AdminResultKind::Success){setSource(r.message);return;} total_=r.totalRevenueYuan; value_->setText(QStringLiteral("总营收：%1 元").arg(total_)); provider_->queryOverview(token_);});
    connect(provider_,&IAdminProvider::overviewFinished,this,[this](const AdminOverviewResult&r){if(r.kind!=AdminResultKind::Success){setSource(r.message);return;} todayValue_=r.todayRevenueYuan; today_->setText(QStringLiteral("今日营收：%1 元").arg(todayValue_)); month_->setText(QStringLiteral("本月营收：%1 元").arg(r.monthRevenueYuan)); if (!initialTrendLoad_){ initialTrendLoad_=true; trendRequestDays_=range_->currentData().toInt(); provider_->queryRevenueTrend(token_,trendRequestDays_);}});
    connect(provider_,&IAdminProvider::revenueTrendFinished,this,[this](const AdminRevenueTrendResult&r){
        if(r.kind!=AdminResultKind::Success){setSource(r.message);return;}
        if (trendRequestDays_==30) { double month=0.0;for(const auto&p:r.points)month+=p.revenueYuan.toDouble();month_->setText(QStringLiteral("本月营收：%1 元").arg(month,0,'f',2)); }
        initialTrendLoad_=false;trend_=r.points;renderTrend();range_->setEnabled(true);setPageState(trend_.isEmpty()?Ui::PageState::Empty:Ui::PageState::Normal);
        requestSummary();  // 链末环：趋势就绪后请求区间汇总。
    });
    connect(provider_,&IAdminProvider::connectivityLost,this,[this]{setSource(QStringLiteral("连接已断开，当前营收数据可能已过期。"));setPageState(Ui::PageState::Offline);});connect(provider_,&IAdminProvider::connectivityRestored,this,[this]{if(isVisible())refresh();});
}
void RevenuePage::renderTrend(){
#ifdef ADMIN_HAS_CHARTS
    auto *chart=new QChart(); auto *series=new QLineSeries(chart);
    for(const auto&p:trend_) series->append(QDateTime::fromString(p.date,Qt::ISODate).toMSecsSinceEpoch(),p.revenueYuan.toDouble());
    auto *dateAxis=new QDateTimeAxis(chart); dateAxis->setFormat(QStringLiteral("MM-dd")); dateAxis->setTitleText(QStringLiteral("日期")); dateAxis->setTickCount(trend_.size()>10?7:qMax(2,trend_.size())); dateAxis->setLabelsAngle(trend_.size()>10?-45:0);
    auto *valueAxis=new QValueAxis(chart); valueAxis->setTitleText(QStringLiteral("营收（元）")); valueAxis->setLabelFormat(QStringLiteral("%.2f")); valueAxis->setMin(0.0);
    chart->addSeries(series); chart->addAxis(dateAxis,Qt::AlignBottom); chart->addAxis(valueAxis,Qt::AlignLeft); series->attachAxis(dateAxis); series->attachAxis(valueAxis); chart->setTitle(range_->currentText()+QStringLiteral("营收趋势")); chart->legend()->hide();
    connect(series,&QLineSeries::hovered,chartView_,[this](const QPointF &point,bool state){if(!state){QToolTip::hideText();return;}const QString date=QDateTime::fromMSecsSinceEpoch(qint64(point.x())).date().toString(QStringLiteral("MM-dd"));QToolTip::showText(QCursor::pos(),QStringLiteral("%1\n%2 元").arg(date).arg(point.y(),0,'f',2),chartView_);});
    chartView_->setChart(chart);
#endif
}
// S1-B：刷新 = 既有总营收/趋势链路 + 区间汇总重查（自动刷新与手动刷新同一路径）。
void RevenuePage::refresh(){
    // S1-B：链式请求（营收→总览→趋势→汇总）。Provider 单在途请求串行（rejectBusy），
    // 背靠背并发会被拒，因此由各环 finished 处理器接续发起；本方法只启动第一环。
    if(!provider_)return;
    range_->setEnabled(false);setPageState(Ui::PageState::Loading);provider_->queryRevenue(token_);
}

// 链末环：区间汇总；自定义范围非法时仅提示不发起。
void RevenuePage::requestSummary(){
    if(!provider_)return;
    const int range=summaryRange_->currentData().toInt();
    QDate from=QDateTime::currentDateTimeUtc().date(), to=from;
    if (range==1){ to=from; }
    else if (range>0){ from=from.addDays(1-range); }
    else { from=fromDate_->date(); to=toDate_->date(); if (from>to){ summaryCaption_->setText(QStringLiteral("自定义区间无效：起始晚于结束（UTC）。")); return; } }
    pendingSummarySeq_=++summaryRequestSeq_;
    provider_->queryRevenueSummary(token_,from.toString(QStringLiteral("yyyy-MM-dd")),to.toString(QStringLiteral("yyyy-MM-dd")));
}
StationAssetsPage::StationAssetsPage(const AdminPreview&p,QWidget*parent):AdminPageWidget(p,parent){setObjectName("StationAssetsPage");}
StationAssetsPage::StationAssetsPage(const AdminPreview&p,IAdminProvider*provider,QString token,QWidget*parent)
    :AdminPageWidget({p.title,QStringLiteral("正式站点查询与新增"),{},{},Ui::PageState::Normal},parent),provider_(provider),token_(std::move(token)){
    setObjectName("StationAssetsPage");setSource(QStringLiteral("正式运营服务 · ADMIN_STATIONS"));table_=new QTableWidget(0,6,this);table_->setObjectName("adminStationsTable");table_->setHorizontalHeaderLabels({"ID","名称","地址","状态","单价","可用/总数"});table_->horizontalHeader()->setStretchLastSection(true);addContent(table_);
    // S1-B 候选栏改造：固定枚举一律候选选择，中文显示、协议值提交；自由字段仍用输入框。
    const QStringList hints{"站点名称","地址","纬度","经度","单价（两位小数）","首个桩序列号"};for(const auto&hint:hints){auto*e=new QLineEdit(this);e->setPlaceholderText(hint);createFields_.append(e);addContent(e);}
    chargeType_=new QComboBox(this);chargeType_->setObjectName(QStringLiteral("stationChargeType"));Ui::fillCombo(chargeType_,Ui::chargeTypeOptions());addContent(chargeType_);
    auto*power=new QLineEdit(this);power->setPlaceholderText(QStringLiteral("额定功率 kW"));createFields_.append(power);addContent(power);
    stationStatus_=new QComboBox(this);stationStatus_->setObjectName(QStringLiteral("stationStatusSelector"));Ui::fillCombo(stationStatus_,Ui::stationStatusOptions());addContent(stationStatus_);
    create_=new QPushButton(QStringLiteral("新增正式站点"),this);create_->setObjectName("createStationAction");addContent(create_);update_=new QPushButton(QStringLiteral("修改选中站点"),this);update_->setObjectName("updateStationAction");addContent(update_);
    connect(create_,&QPushButton::clicked,this,[this]{bool latOk=false,lonOk=false,powerOk=false;AdminNewStation s; s.name=createFields_[0]->text();s.address=createFields_[1]->text();s.latitude=createFields_[2]->text().toDouble(&latOk);s.longitude=createFields_[3]->text().toDouble(&lonOk);s.priceYuanPerKwh=createFields_[4]->text();const double power=createFields_[5]->text().toDouble(&powerOk);if(!latOk||!lonOk||!powerOk){setSource(QStringLiteral("请输入有效站点坐标和功率。"));return;}s.piles.append({createFields_.at(5)->text(),chargeType_->currentData().toString(),power});setPageState(Ui::PageState::Loading);create_->setEnabled(false);provider_->createStation(token_,s);});
    connect(table_,&QTableWidget::cellClicked,this,[this](int row,int){if(row<0||!table_->item(row,0))return;createFields_[0]->setText(table_->item(row,1)->text());createFields_[1]->setText(table_->item(row,2)->text());createFields_[4]->setText(table_->item(row,4)->text());const int statusIndex=stationStatus_->findData(table_->item(row,3)->text());if(statusIndex>=0)stationStatus_->setCurrentIndex(statusIndex);});
    connect(update_,&QPushButton::clicked,this,[this]{const int row=table_->currentRow();if(row<0||!table_->item(row,0))return;AdminNewStation s;s.name=createFields_[0]->text();s.address=createFields_[1]->text();s.latitude=createFields_[2]->text().toDouble();s.longitude=createFields_[3]->text().toDouble();s.priceYuanPerKwh=createFields_[4]->text();provider_->updateStation(token_,table_->item(row,0)->text().toLongLong(),s,stationStatus_->currentData().toString());setPageState(Ui::PageState::Loading);create_->setEnabled(false);update_->setEnabled(false);});
    connect(provider_,&IAdminProvider::stationsFinished,this,[this](const AdminStationQueryResult&r){if(r.kind!=AdminResultKind::Success){setSource(r.message);setPageState(r.kind==AdminResultKind::Offline?Ui::PageState::Offline:Ui::PageState::Error);create_->setEnabled(false);update_->setEnabled(false);return;}QList<QStringList> rows;for(const auto&s:r.stations)rows.append({QString::number(s.stationId),s.name,s.address,s.status,s.priceYuanPerKwh,QStringLiteral("%1/%2").arg(s.availablePiles).arg(s.totalPiles)});applyRows(table_,rows);setPageState(rows.isEmpty()?Ui::PageState::Empty:Ui::PageState::Normal);create_->setEnabled(true);update_->setEnabled(!rows.isEmpty());});
    connect(provider_,&IAdminProvider::stationCreated,this,[this](const AdminActionResult&r){if(r.kind==AdminResultKind::Success)refresh();else{setSource(r.message);setPageState(r.kind==AdminResultKind::Offline?Ui::PageState::Offline:Ui::PageState::Error);create_->setEnabled(false);}});connect(provider_,&IAdminProvider::stationUpdated,this,[this](const AdminActionResult&r){if(r.kind==AdminResultKind::Success)refresh();else setSource(r.message);});connect(provider_,&IAdminProvider::connectivityLost,this,[this]{setSource(QStringLiteral("连接已断开，当前站点数据可能已过期。"));setPageState(Ui::PageState::Offline);create_->setEnabled(false);update_->setEnabled(false);});connect(provider_,&IAdminProvider::connectivityRestored,this,[this]{if(isVisible())refresh();});
}
void StationAssetsPage::refresh(){if(!provider_)return;setPageState(Ui::PageState::Loading);provider_->queryStations(token_);}
PileDevicesPage::PileDevicesPage(const AdminPreview &p, QWidget *parent):AdminPageWidget(p,parent){setObjectName("PileDevicesPage");table_=new QTableWidget(0,3,this);table_->setObjectName("adminTable");table_->setHorizontalHeaderLabels({"设备标识","所属站点","状态"});const QList<QStringList>rows{{"PILE-001","天河示范站","ONLINE · 在线"},{"PILE-002","城市公园站","OFFLINE · 离线"}};applyRows(table_,rows);table_->horizontalHeader()->setStretchLastSection(true);table_->setMinimumHeight(160);addContent(table_);connect(stateWidget(),&Ui::PageStateWidget::stateChanged,table_,[this,rows](Ui::PageState s){if(s==Ui::PageState::Empty)table_->setRowCount(0);else if(s==Ui::PageState::Normal)applyRows(table_,rows);});}
PileDevicesPage::PileDevicesPage(const AdminPreview&p,IAdminProvider*provider,QString token,QWidget*parent):AdminPageWidget({p.title,QStringLiteral("正式电桩查询"),{},{},Ui::PageState::Normal},parent),provider_(provider),token_(std::move(token)){setObjectName("PileDevicesPage");setSource(QStringLiteral("正式运营服务 · ADMIN_PILES"));summary_=new QLabel(QStringLiteral("状态统计（客户端统计）：--"),this);summary_->setWordWrap(true);addContent(summary_);table_=new QTableWidget(0,5,this);table_->setHorizontalHeaderLabels({"桩ID","站点ID","序列号","类型","状态"});addContent(table_);auto*target=new QComboBox(this);target->setObjectName("pileStatusSelector");Ui::fillCombo(target,Ui::pileStatusOptions());addContent(target);status_=new QPushButton(QStringLiteral("修改电桩状态"),this);restart_=new QPushButton(QStringLiteral("重启电桩"),this);addContent(status_);addContent(restart_);connect(status_,&QPushButton::clicked,this,[this,target]{const int r=table_->currentRow();if(r<0)return;status_->setEnabled(false);restart_->setEnabled(false);provider_->setPileStatus(token_,table_->item(r,0)->text().toLongLong(),target->currentData().toString(),QStringLiteral("运营调整"));setPageState(Ui::PageState::Loading);});connect(restart_,&QPushButton::clicked,this,[this]{const int r=table_->currentRow();if(r<0)return;status_->setEnabled(false);restart_->setEnabled(false);provider_->restartPile(token_,table_->item(r,0)->text().toLongLong());setPageState(Ui::PageState::Loading);});connect(provider_,&IAdminProvider::pilesFinished,this,[this](const AdminPileQueryResult&r){if(r.kind!=AdminResultKind::Success){setSource(r.message);setPageState(r.kind==AdminResultKind::Offline?Ui::PageState::Offline:Ui::PageState::Error);status_->setEnabled(false);restart_->setEnabled(false);return;}int active=0,idle=0,fault=0,offline=0;QList<QStringList>rows;for(const auto&p:r.piles){rows.append({QString::number(p.pileId),QString::number(p.stationId),p.serial,Ui::labelFor(Ui::chargeTypeOptions(),p.chargeType),Ui::labelFor(Ui::pileStatusOptions(),p.status)});if(p.status=="RESERVED"||p.status=="CHARGING")++active;else if(p.status=="IDLE")++idle;else if(p.status=="FAULT")++fault;else if(p.status=="OFFLINE")++offline;}const int total=rows.size();auto pct=[total](int n){return total?QString::number(100.0*n/total,'f',1)+"%":"0.0%";};summary_->setText(QStringLiteral("状态统计（客户端统计）\n在用 %1 (%2) · 闲置 %3 (%4) · 故障 %5 (%6) · 离线 %7 (%8)\n健康：%9 台；异常：%10 台").arg(active).arg(pct(active)).arg(idle).arg(pct(idle)).arg(fault).arg(pct(fault)).arg(offline).arg(pct(offline)).arg(active+idle).arg(fault+offline));applyRows(table_,rows);setPageState(rows.isEmpty()?Ui::PageState::Empty:Ui::PageState::Normal);status_->setEnabled(!rows.isEmpty());restart_->setEnabled(!rows.isEmpty());});connect(provider_,&IAdminProvider::pileStatusFinished,this,[this](const AdminActionResult&r){if(r.kind==AdminResultKind::Success)refresh();else setSource(r.message);});connect(provider_,&IAdminProvider::pileRestartFinished,this,[this](const AdminActionResult&r){if(r.kind==AdminResultKind::Success)refresh();else setSource(r.message);});connect(provider_,&IAdminProvider::connectivityLost,this,[this]{setSource(QStringLiteral("连接已断开，当前电桩数据可能已过期。"));setPageState(Ui::PageState::Offline);status_->setEnabled(false);restart_->setEnabled(false);});connect(provider_,&IAdminProvider::connectivityRestored,this,[this]{if(isVisible())refresh();});}
void PileDevicesPage::refresh(){if(!provider_)return;setPageState(Ui::PageState::Loading);provider_->queryPiles(token_);}
OrdersPage::OrdersPage(const AdminPreview &p, QWidget *parent)
    : AdminPageWidget(p,parent)
{
    setObjectName("OrdersPage");
    table_=new QTableWidget(0,3,this);
    table_->setObjectName("adminTable");
    table_->setHorizontalHeaderLabels({"订单号","站点","状态"});
    table_->setMinimumHeight(160);
    const QList<QStringList> rows{{"MOCK-ORDER-01","天河示范站","CHARGING · 充电中"}};
    applyRows(table_,rows);
    table_->horizontalHeader()->setStretchLastSection(true);
    addContent(table_);
    connect(stateWidget(),&Ui::PageStateWidget::stateChanged,table_,[this,rows](Ui::PageState s){if(s==Ui::PageState::Empty)table_->setRowCount(0);else if(s==Ui::PageState::Normal)applyRows(table_,rows);});
}

OrdersPage::OrdersPage(const AdminPreview &p, IAdminProvider *provider,
                       QString adminToken, QWidget *parent)
    : AdminPageWidget({p.title, QStringLiteral("正式订单查询"), {}, {},
                       Ui::PageState::Normal}, parent)
    , provider_(provider), adminToken_(std::move(adminToken))
{
    setObjectName("OrdersPage");
    setSource(QStringLiteral("正式运营服务 · ADMIN_ORDERS"));
    table_=new QTableWidget(0,5,this);
    table_->setObjectName("adminTable");
    table_->setHorizontalHeaderLabels({"订单号","用户 ID","电桩 ID","状态","金额（元）"});
    table_->horizontalHeader()->setStretchLastSection(true);
    addContent(table_);
    connect(provider_, &IAdminProvider::ordersFinished, this,
            [this](const AdminOrderQueryResult &result) {
        if (result.kind != AdminResultKind::Success) {
            setSource(result.message);
            setPageState(result.kind == AdminResultKind::Offline
                             ? Ui::PageState::Offline : Ui::PageState::Error);
            return;
        }
        QList<QStringList> rows;
        for (const AdminOrderSummary &order : result.orders) {
            rows.append({order.orderId, QString::number(order.userId),
                         QString::number(order.pileId), order.status,
                         order.amountYuan});
        }
        applyRows(table_, rows);
        setSource(QStringLiteral("正式运营服务 · 已同步 %1 笔订单").arg(rows.size()));
        setPageState(rows.isEmpty() ? Ui::PageState::Empty : Ui::PageState::Normal);
    });
    connect(provider_, &IAdminProvider::connectivityLost, this, [this] {
        setSource(QStringLiteral("连接已断开，当前订单数据可能已过期。"));
        setPageState(Ui::PageState::Offline);
    });
    connect(provider_, &IAdminProvider::connectivityRestored, this, [this] {
        if (isVisible()) refresh();
    });
}

void OrdersPage::refresh()
{
    if (!provider_) return;
    if (adminToken_.isEmpty()) {
        setSource(QStringLiteral("运营订单服务不可用。"));
        setPageState(Ui::PageState::Error);
        return;
    }
    setPageState(Ui::PageState::Loading);
    provider_->queryOrders(adminToken_);
}
UserManagementPage::UserManagementPage(const AdminPreview&p,QWidget*parent):AdminPageWidget(p,parent){setObjectName("UserManagementPage");}
UserManagementPage::UserManagementPage(const AdminPreview&p,IAdminProvider*provider,QString token,QWidget*parent):AdminPageWidget({p.title,QStringLiteral("正式用户查询与冻结管理"),{},{},Ui::PageState::Normal},parent),provider_(provider),token_(std::move(token)){
    setObjectName("UserManagementPage");setSource(QStringLiteral("正式运营服务 · ADMIN_USERS"));table_=new QTableWidget(0,5,this);table_->setObjectName("adminUsersTable");table_->setHorizontalHeaderLabels({"用户ID","手机号","昵称","余额","状态"});table_->setSelectionBehavior(QAbstractItemView::SelectRows);addContent(table_);reason_=new QLineEdit(this);reason_->setPlaceholderText(QStringLiteral("冻结/解冻原因（必填）"));addContent(reason_);freeze_=new QPushButton(QStringLiteral("冻结选中用户"),this);unfreeze_=new QPushButton(QStringLiteral("解冻选中用户"),this);freeze_->setObjectName("freezeUserAction");unfreeze_->setObjectName("unfreezeUserAction");addContent(freeze_);addContent(unfreeze_);
    const auto submit=[this](bool frozen){const int row=table_->currentRow();bool ok=false;const qint64 id=row>=0?table_->item(row,0)->text().toLongLong(&ok):0;if(!ok||id<=0||reason_->text().trimmed().isEmpty()){setSource(QStringLiteral("请选择用户并填写操作原因。"));return;}setPageState(Ui::PageState::Loading);freeze_->setEnabled(false);unfreeze_->setEnabled(false);provider_->setUserFrozen(token_,id,frozen,reason_->text().trimmed());};connect(freeze_,&QPushButton::clicked,this,[submit]{submit(true);});connect(unfreeze_,&QPushButton::clicked,this,[submit]{submit(false);});
    connect(provider_,&IAdminProvider::usersFinished,this,[this](const AdminUserQueryResult&r){if(r.kind!=AdminResultKind::Success){setSource(r.message);setPageState(r.kind==AdminResultKind::Offline?Ui::PageState::Offline:Ui::PageState::Error);freeze_->setEnabled(false);unfreeze_->setEnabled(false);return;}QList<QStringList>rows;for(const auto&u:r.users)rows.append({QString::number(u.userId),u.phone,u.nickname,u.balanceYuan,u.status});applyRows(table_,rows);setPageState(rows.isEmpty()?Ui::PageState::Empty:Ui::PageState::Normal);freeze_->setEnabled(!rows.isEmpty());unfreeze_->setEnabled(!rows.isEmpty());});
    connect(provider_,&IAdminProvider::userFreezeFinished,this,[this](const AdminActionResult&r){if(r.kind==AdminResultKind::Success)refresh();else{setSource(r.message);setPageState(r.kind==AdminResultKind::Offline?Ui::PageState::Offline:Ui::PageState::Error);freeze_->setEnabled(false);unfreeze_->setEnabled(false);}});connect(provider_,&IAdminProvider::connectivityLost,this,[this]{setSource(QStringLiteral("连接已断开，当前用户数据可能已过期。"));setPageState(Ui::PageState::Offline);freeze_->setEnabled(false);unfreeze_->setEnabled(false);});connect(provider_,&IAdminProvider::connectivityRestored,this,[this]{if(isVisible())refresh();});
}
void UserManagementPage::refresh(){if(!provider_)return;setPageState(Ui::PageState::Loading);provider_->queryUsers(token_);}
AlertsTicketsPage::AlertsTicketsPage(const AdminPreview&p,QWidget*parent):AdminPageWidget(p,parent){setObjectName("AlertsTicketsPage");}
AlertsTicketsPage::AlertsTicketsPage(const AdminPreview&p,IAdminProvider*provider,QString token,QWidget*parent)
    : AdminPageWidget({p.title,QStringLiteral("正式告警与工单"),{},{},Ui::PageState::Normal},parent),provider_(provider),token_(std::move(token))
{
    setObjectName("AlertsTicketsPage"); setSource(QStringLiteral("正式运营服务 · ADMIN_ALARMS / ADMIN_TICKETS"));
    alarms_=new QTableWidget(0,5,this); alarms_->setObjectName("adminAlarmsTable"); alarms_->setHorizontalHeaderLabels({"告警ID","电桩ID","类型","状态","发生时间"}); addContent(alarms_);
    ack_=new QPushButton(QStringLiteral("确认选中告警"),this); addContent(ack_);
    tickets_=new QTableWidget(0,5,this); tickets_->setObjectName("adminTicketsTable"); tickets_->setHorizontalHeaderLabels({"工单ID","告警ID","状态","创建时间","处理人"}); addContent(tickets_);
    start_=new QPushButton(QStringLiteral("开始工单"),this); resolve_=new QPushButton(QStringLiteral("解决工单"),this); close_=new QPushButton(QStringLiteral("关闭工单"),this); addContent(start_); addContent(resolve_); addContent(close_);
    connect(ack_,&QPushButton::clicked,this,[this]{auto*i=alarms_->currentItem(); if(!i)return; ack_->setEnabled(false); provider_->acknowledgeAlarm(token_,alarms_->item(i->row(),0)->text().toLongLong()); setPageState(Ui::PageState::Loading);});
    auto ticketId=[this]{auto*i=tickets_->currentItem(); return i?tickets_->item(i->row(),0)->text().toLongLong():0;};
    connect(start_,&QPushButton::clicked,this,[this,ticketId]{if(!ticketId())return;start_->setEnabled(false);resolve_->setEnabled(false);close_->setEnabled(false);provider_->startTicket(token_,ticketId(),QStringLiteral("operator"));setPageState(Ui::PageState::Loading);});
    connect(resolve_,&QPushButton::clicked,this,[this,ticketId]{if(!ticketId())return;start_->setEnabled(false);resolve_->setEnabled(false);close_->setEnabled(false);provider_->resolveTicket(token_,ticketId(),QStringLiteral("已处理"));setPageState(Ui::PageState::Loading);});
    connect(close_,&QPushButton::clicked,this,[this,ticketId]{if(!ticketId())return;start_->setEnabled(false);resolve_->setEnabled(false);close_->setEnabled(false);provider_->closeTicket(token_,ticketId());setPageState(Ui::PageState::Loading);});
    const auto reenableActions=[this]{ack_->setEnabled(true);start_->setEnabled(true);resolve_->setEnabled(true);close_->setEnabled(true);};
    connect(provider_,&IAdminProvider::alarmAcknowledged,this,[this,reenableActions](const AdminActionResult&r){reenableActions();if(r.kind==AdminResultKind::Success)refreshAlarms();else setSource(r.message);}); connect(provider_,&IAdminProvider::ticketActionFinished,this,[this,reenableActions](const AdminActionResult&r){reenableActions();if(r.kind==AdminResultKind::Success)refreshTickets();else setSource(r.message);});
    connect(provider_,&IAdminProvider::alarmsFinished,this,[this,reenableActions](AdminResultKind k,const QString&m,const QList<AdminAlarmSummary>&rows){reenableActions();if(k!=AdminResultKind::Success){setSource(m);setPageState(k==AdminResultKind::Offline?Ui::PageState::Offline:Ui::PageState::Error);return;}alarms_->setRowCount(rows.size());for(int r=0;r<rows.size();++r){const auto&a=rows[r];alarms_->setItem(r,0,new QTableWidgetItem(QString::number(a.alarmId)));alarms_->setItem(r,1,new QTableWidgetItem(QString::number(a.pileId)));alarms_->setItem(r,2,new QTableWidgetItem(a.alarmType));alarms_->setItem(r,3,new QTableWidgetItem(a.status));alarms_->setItem(r,4,new QTableWidgetItem(a.occurredAt));}setPageState(rows.isEmpty()?Ui::PageState::Empty:Ui::PageState::Normal);});
    connect(provider_,&IAdminProvider::ticketsFinished,this,[this,reenableActions](AdminResultKind k,const QString&m,const QList<AdminTicketSummary>&rows){reenableActions();if(k!=AdminResultKind::Success){setSource(m);setPageState(k==AdminResultKind::Offline?Ui::PageState::Offline:Ui::PageState::Error);return;}tickets_->setRowCount(rows.size());for(int r=0;r<rows.size();++r){const auto&t=rows[r];tickets_->setItem(r,0,new QTableWidgetItem(QString::number(t.ticketId)));tickets_->setItem(r,1,new QTableWidgetItem(QString::number(t.alarmId)));tickets_->setItem(r,2,new QTableWidgetItem(t.status));tickets_->setItem(r,3,new QTableWidgetItem(t.createdAt));tickets_->setItem(r,4,new QTableWidgetItem(t.assignee));}setPageState(rows.isEmpty()?Ui::PageState::Empty:Ui::PageState::Normal);});
    connect(provider_,&IAdminProvider::alarmAcknowledged,this,[this](const AdminActionResult&r){if(r.kind==AdminResultKind::Success)refreshAlarms();else setSource(r.message);}); connect(provider_,&IAdminProvider::ticketActionFinished,this,[this](const AdminActionResult&r){if(r.kind==AdminResultKind::Success)refreshTickets();else setSource(r.message);});
    connect(provider_,&IAdminProvider::connectivityLost,this,[this]{setSource(QStringLiteral("连接已断开，告警和工单数据可能已过期。"));setPageState(Ui::PageState::Offline);ack_->setEnabled(false);start_->setEnabled(false);resolve_->setEnabled(false);close_->setEnabled(false);}); connect(provider_,&IAdminProvider::connectivityRestored,this,[this]{if(isVisible()){refreshAlarms();refreshTickets();}});
}
void AlertsTicketsPage::refreshAlarms(){provider_->queryAlarms(token_);} void AlertsTicketsPage::refreshTickets(){provider_->queryTickets(token_);}
// S1-B/P1-A 修复：进入页面与 3 秒定时器都必须加载数据（此前未覆写 refresh() 且不在主窗口分发链，
// 导致告警/工单表恒为空、无法选中行操作）。
void AlertsTicketsPage::refresh(){if(!provider_)return;refreshAlarms();refreshTickets();}

LogsPage::LogsPage(const AdminPreview &p, QWidget *parent)
    : AdminPageWidget(p, parent)
{
    setObjectName("LogsPage");
}

LogsPage::LogsPage(const AdminPreview &p, IAdminProvider *provider,
                   QString adminToken, QWidget *parent)
    : AdminPageWidget({p.title, QStringLiteral("正式操作日志查询"), {}, {},
                       Ui::PageState::Normal}, parent)
    , provider_(provider), adminToken_(std::move(adminToken))
{
    setObjectName("LogsPage");
    setSource(QStringLiteral("正式运营服务 · ADMIN_LOGS"));
    table_=new QTableWidget(0,5,this);
    table_->setObjectName("adminLogsTable");
    table_->setHorizontalHeaderLabels({"日志 ID","操作者","动作","目标","结果"});
    table_->horizontalHeader()->setStretchLastSection(true);
    addContent(table_);
    connect(provider_, &IAdminProvider::logsFinished, this,
            [this](const AdminLogQueryResult &result) {
        if (result.kind != AdminResultKind::Success) {
            setSource(result.message);
            setPageState(result.kind == AdminResultKind::Offline
                             ? Ui::PageState::Offline : Ui::PageState::Error);
            return;
        }
        QList<QStringList> rows;
        for (const AdminLogSummary &log : result.logs) {
            rows.append({QString::number(log.logId), log.actorId, log.action,
                         log.targetType + QStringLiteral(":") + log.targetId,
                         log.result});
        }
        applyRows(table_, rows);
        setSource(QStringLiteral("正式运营服务 · 已同步 %1 条日志").arg(rows.size()));
        setPageState(rows.isEmpty() ? Ui::PageState::Empty : Ui::PageState::Normal);
    });
    connect(provider_, &IAdminProvider::connectivityLost, this, [this] {
        setSource(QStringLiteral("连接已断开，当前日志数据可能已过期。"));
        setPageState(Ui::PageState::Offline);
    });
    connect(provider_, &IAdminProvider::connectivityRestored, this, [this] {
        if (isVisible()) refresh();
    });
}

void LogsPage::refresh()
{
    if (!provider_) return;
    if (adminToken_.isEmpty()) {
        setSource(QStringLiteral("运营日志服务不可用。"));
        setPageState(Ui::PageState::Error);
        return;
    }
    setPageState(Ui::PageState::Loading);
    provider_->queryLogs(adminToken_);
}
AdminLoginPage::AdminLoginPage(QWidget *parent):Ui::PageScaffold(QStringLiteral("管理员登录"),QStringLiteral("运营工作台 · SIMULATED"),parent) {
    setWindowTitle(QStringLiteral("EV FLOW · 管理员登录 · OFFLINE_DEMO · SIMULATED"));
    setObjectName(QStringLiteral("adminLoginPage")); setSource(QStringLiteral("登录行为仅为 Mock，不发送 ADMIN_LOGIN 请求"));
    auto *account=new QLineEdit(this); account->setObjectName(QStringLiteral("adminAccount")); account->setPlaceholderText(QStringLiteral("管理员账号")); addContent(account);
    auto *password=new QLineEdit(this); password->setObjectName(QStringLiteral("adminPassword")); password->setEchoMode(QLineEdit::Password); password->setPlaceholderText(QStringLiteral("密码")); addContent(password);
    setAction(QStringLiteral("登录（模拟）")); connect(primaryAction(),&QPushButton::clicked,this,&AdminLoginPage::loginSucceeded);
}

AdminLoginPage::AdminLoginPage(IAdminProvider *provider, QWidget *parent)
    : Ui::PageScaffold(QStringLiteral("管理员登录"),
                       QStringLiteral("登录运营工作台"), parent), provider_(provider)
{
    setObjectName(QStringLiteral("adminLoginPage"));
    setSource(QStringLiteral("凭据将提交至正式服务进行身份验证"));
    account_=new QLineEdit(this);
    account_->setObjectName(QStringLiteral("adminAccount"));
    account_->setPlaceholderText(QStringLiteral("管理员账号"));
    addContent(account_);
    password_=new QLineEdit(this);
    password_->setObjectName(QStringLiteral("adminPassword"));
    password_->setEchoMode(QLineEdit::Password);
    password_->setPlaceholderText(QStringLiteral("密码"));
    addContent(password_);
    setAction(QStringLiteral("登录"));
    connect(primaryAction(), &QPushButton::clicked, this, [this] {
        if (!provider_) return;
        setPageState(Ui::PageState::Loading);
        provider_->login(account_->text(), password_->text());
    });
    connect(provider_, &IAdminProvider::loginFinished, this,
            [this](const AdminLoginResult &result) {
        if (result.kind == AdminResultKind::Success) {
            setPageState(Ui::PageState::Normal);
            emit authenticated(result.adminToken);
        } else {
            setSource(result.message);
            setPageState(Ui::PageState::Normal);
            stateWidget()->setState(result.kind == AdminResultKind::Offline
                                        ? Ui::PageState::Offline
                                        : Ui::PageState::Error);
        }
    });
}
