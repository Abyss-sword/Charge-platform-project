#pragma once
#include "admin_mock_data_provider.h"
#include "admin_provider.h"
#include "widgets/metric_card.h"
#include "widgets/page_scaffold.h"
#include <QPointer>
#include <QTimer>
class IAdminProvider;
class QLineEdit;
class QPushButton;
class QLabel;
class QTableWidget;
class QComboBox;
class QDateEdit;
#ifdef ADMIN_HAS_CHARTS
class QChartView;
#endif
// 运营页基类：统一“可见页每 3 秒自动刷新、离开页面停止定时器”（审查 01 规格 · 公共联动）。
// 子类覆写 refresh()；showEvent 启动、hideEvent 停止，切换页即自然生效。
class AdminPageWidget : public Ui::PageScaffold {
    Q_OBJECT
public:
    AdminPageWidget(const AdminPreview &p, QWidget *parent=nullptr);
    virtual void refresh();
protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
private:
    QTimer refreshTimer_;
};
class OverviewPage final : public AdminPageWidget {
    Q_OBJECT
public:
    OverviewPage(const AdminPreview&, QWidget *parent=nullptr);
    OverviewPage(const AdminPreview&,IAdminProvider*,QString,QWidget *parent=nullptr);
    void refresh() override;
signals:
    // 总览清单行点击 → 请求主窗口切换到对应页（保留各页既有筛选）。
    void pageRequested(int pageIndex);
private:
    void applySnapshot(const AdminOverviewResult &snapshot);
    QPointer<IAdminProvider> provider_;
    QString token_;
    Ui::MetricCard *todayRevenue_{}; Ui::MetricCard *monthRevenue_{};
    Ui::MetricCard *serviceCount_{}; Ui::MetricCard *orderCount_{};
    Ui::MetricCard *onlinePiles_{}; Ui::MetricCard *availableRate_{};
    Ui::MetricCard *pendingAlarms_{};
    QLabel *snapshotLine_{};
    long long lastSnapshotVersion_ = -1;  // 旧快照丢弃判据：仅接受更大的版本号
#ifdef ADMIN_HAS_CHARTS
    QChartView *pileRingView_{};
    QChartView *loadLineView_{};
    QChartView *orderBarView_{};
#endif
    QTableWidget *alarmList_{}; QTableWidget *ticketList_{};
    QTableWidget *abnormalList_{}; QTableWidget *recentOrders_{};
};
class RevenuePage final : public AdminPageWidget { public: RevenuePage(const AdminPreview&, QWidget *parent=nullptr); RevenuePage(const AdminPreview&,IAdminProvider*,QString,QWidget *parent=nullptr); void refresh() override; void requestSummary(); private: QPointer<IAdminProvider> provider_; QString token_; QLabel *value_{}; QLabel *today_{}; QLabel *month_{}; QComboBox *range_{}; QList<AdminRevenuePoint> trend_; QString total_; QString todayValue_; void renderTrend();
    int trendRequestDays_ = 0;
    bool initialTrendLoad_ = false;
    // S1-B：区间汇总 + 请求序号防旧覆盖 + 自定义日期范围
    QComboBox *summaryRange_{}; QDateEdit *fromDate_{}; QDateEdit *toDate_{};
    QPushButton *summaryApply_{};
    QList<Ui::MetricCard*> summaryCards_; QLabel *summaryCaption_{};
    QTableWidget *stationRanking_{}; QTableWidget *hourHeat_{};
    int summaryRequestSeq_ = 0; int pendingSummarySeq_ = 0; QString lastSummaryKey_;
#ifdef ADMIN_HAS_CHARTS
    QChartView *chartView_{};
    QChartView *hourHeatView_{};
#endif
};
class StationAssetsPage final : public AdminPageWidget { public: StationAssetsPage(const AdminPreview&, QWidget *parent=nullptr); StationAssetsPage(const AdminPreview&,IAdminProvider*,QString,QWidget *parent=nullptr); void refresh(); private: QPointer<IAdminProvider> provider_; QString token_; QTableWidget *table_{}; QList<QLineEdit*> createFields_; QComboBox *chargeType_{}; QComboBox *stationStatus_{}; QPushButton *create_{}; QPushButton *update_{}; };
class PileDevicesPage final : public AdminPageWidget { public: PileDevicesPage(const AdminPreview&, QWidget *parent=nullptr); PileDevicesPage(const AdminPreview&,IAdminProvider*,QString,QWidget *parent=nullptr); void refresh(); private: QPointer<IAdminProvider> provider_; QString token_; QTableWidget *table_{}; QLabel *summary_{}; QComboBox *pileStatus_{}; QPushButton *status_{}; QPushButton *restart_{}; };
class UserManagementPage final : public AdminPageWidget { public: UserManagementPage(const AdminPreview&, QWidget *parent=nullptr); UserManagementPage(const AdminPreview&,IAdminProvider*,QString,QWidget *parent=nullptr); void refresh(); private: QPointer<IAdminProvider> provider_; QString token_; QTableWidget *table_{}; QLineEdit *reason_{}; QPushButton *freeze_{}; QPushButton *unfreeze_{}; };
class AlertsTicketsPage final : public AdminPageWidget { public: AlertsTicketsPage(const AdminPreview &p,QWidget *parent=nullptr); AlertsTicketsPage(const AdminPreview&,IAdminProvider*,QString,QWidget *parent=nullptr); void refresh() override; private: QPointer<IAdminProvider> provider_; QString token_; QTableWidget *alarms_{}; QTableWidget *tickets_{}; QPushButton *ack_{}; QPushButton *start_{}; QPushButton *resolve_{}; QPushButton *close_{}; void refreshAlarms(); void refreshTickets(); };
class OrdersPage final : public AdminPageWidget
{
public:
    explicit OrdersPage(const AdminPreview &p, QWidget *parent=nullptr);
    OrdersPage(const AdminPreview &p, IAdminProvider *provider,
               QString adminToken, QWidget *parent=nullptr);
    void refresh();
private:
    QPointer<IAdminProvider> provider_;
    QString adminToken_;
    QTableWidget *table_ = nullptr;
};
class LogsPage final : public AdminPageWidget
{
public:
    explicit LogsPage(const AdminPreview &p, QWidget *parent=nullptr);
    LogsPage(const AdminPreview &p, IAdminProvider *provider,
             QString adminToken, QWidget *parent=nullptr);
    void refresh();
private:
    QPointer<IAdminProvider> provider_;
    QString adminToken_;
    QTableWidget *table_ = nullptr;
};
class AdminLoginPage final : public Ui::PageScaffold
{
    Q_OBJECT
public:
    explicit AdminLoginPage(QWidget *parent=nullptr);
    AdminLoginPage(IAdminProvider *provider, QWidget *parent=nullptr);
signals:
    void loginSucceeded();
    void authenticated(const QString &adminToken);
private:
    QPointer<IAdminProvider> provider_;
    QLineEdit *account_ = nullptr;
    QLineEdit *password_ = nullptr;
};
