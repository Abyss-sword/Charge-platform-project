#pragma once

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

enum class AdminResultKind
{
    Success,
    Error,
    Offline
};

struct AdminLoginResult
{
    AdminResultKind kind = AdminResultKind::Error;
    QString message;
    QString adminToken;
    QString errorCode;
};

struct AdminOrderSummary
{
    QString orderId;
    qint64 userId = 0;
    qint64 pileId = 0;
    QString status;
    QString energyKwh;
    QString amountYuan;
    QString createdAt;
    QString endedAt;
};

struct AdminOrderQueryResult
{
    AdminResultKind kind = AdminResultKind::Error;
    QString message;
    QList<AdminOrderSummary> orders;
};

struct AdminLogSummary
{
    qint64 logId = 0;
    QString actorType;
    QString actorId;
    QString action;
    QString targetType;
    QString targetId;
    QString result;
    QString createdAt;
};

struct AdminLogQueryResult
{
    AdminResultKind kind = AdminResultKind::Error;
    QString message;
    QList<AdminLogSummary> logs;
};

struct AdminOverviewResult
{
    AdminResultKind kind = AdminResultKind::Error;
    QString message;
    QString todayRevenueYuan;
    int todayServiceCount = 0;
    double utilizationRate = 0.0;
    double onlineRate = 0.0;
    int pendingAlarmCount = 0;
    // S1-B 总览页扩展字段（服务端 ADMIN_OVERVIEW 提供的具体口径见 01 规格）。
    QString monthRevenueYuan = QStringLiteral("0.00");
    int todayOrderCount = 0;
    long long snapshotVersion = 0;
    QString serverTimeUtc;
    QVariantMap pileStatusCounts;
    QVariantMap orderStatusCounts;
    QVariantList loadSeries24h;
    QVariantList latestAlarms;
    QVariantList openTickets;
    QVariantList abnormalPiles;
    QVariantList recentOrders;
};

// S1-B 营收页区间汇总（已支付口径，两位小数；见 ADMIN_REVENUE_SUMMARY）。
struct AdminRevenueSummaryResult
{
    AdminResultKind kind = AdminResultKind::Error;
    QString message;
    QString fromDate;
    QString toDate;
    QString totalRevenueYuan;
    int orderCount = 0;
    QString totalEnergyKwh;
    QString avgOrderYuan;
    QString avgPriceYuan;
    QVariantList stationRanking;
    QVariantList hourHistogram;
};

struct AdminRevenueResult
{
    AdminResultKind kind = AdminResultKind::Error;
    QString message;
    QString totalRevenueYuan;
};
struct AdminRevenuePoint { QString date; QString revenueYuan; };
struct AdminRevenueTrendResult { AdminResultKind kind=AdminResultKind::Error; QString message; QList<AdminRevenuePoint> points; };

struct AdminStationSummary
{
    qint64 stationId = 0;
    QString name;
    QString address;
    QString status;
    QString priceYuanPerKwh;
    int totalPiles = 0;
    int availablePiles = 0;
};
struct AdminStationQueryResult { AdminResultKind kind=AdminResultKind::Error; QString message; QList<AdminStationSummary> stations; };

struct AdminPileSummary
{
    qint64 pileId = 0;
    qint64 stationId = 0;
    QString serial;
    QString chargeType;
    double ratedPowerKw = 0.0;
    QString status;
    QString lastHeartbeatAt;
};
struct AdminPileQueryResult { AdminResultKind kind=AdminResultKind::Error; QString message; QList<AdminPileSummary> piles; };

struct AdminUserSummary
{
    qint64 userId = 0;
    QString phone;
    QString nickname;
    QString balanceYuan;
    QString status;
    QString createdAt;
};
struct AdminUserQueryResult { AdminResultKind kind=AdminResultKind::Error; QString message; QList<AdminUserSummary> users; };

struct AdminNewPile { QString serial; QString chargeType; double ratedPowerKw=0.0; };
struct AdminNewStation
{
    QString name;
    QString address;
    double latitude = 0.0;
    double longitude = 0.0;
    QString priceYuanPerKwh;
    QList<AdminNewPile> piles;
};
struct AdminActionResult { AdminResultKind kind=AdminResultKind::Error; QString message; qint64 stationId=0; QString errorCode; };
struct AdminAlarmSummary { qint64 alarmId=0; qint64 pileId=0; qint64 orderId=0; QString severity; QString alarmType; QString content; QString status; QString occurredAt; };
struct AdminTicketSummary { qint64 ticketId=0; qint64 alarmId=0; qint64 pileId=0; QString assignee; QString status; QString notes; QString createdAt; QString updatedAt; };

class IAdminProvider : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~IAdminProvider() override = default;

    virtual void login(const QString &username, const QString &password) = 0;
    virtual void queryOrders(const QString &adminToken, const QString &status = {},
                             int limit = 100, int offset = 0) = 0;
    virtual void queryLogs(const QString &adminToken, const QString &action = {},
                           int limit = 100, int offset = 0) = 0;
    virtual void queryOverview(const QString &) {}
    virtual void queryRevenue(const QString &) {}
    virtual void queryRevenueTrend(const QString &, int) {}
    virtual void queryRevenueSummary(const QString &, const QString &, const QString &) {}
    virtual void queryStations(const QString &, const QString & = {}, int=100, int=0) {}
    virtual void queryPiles(const QString &, qint64=0, const QString & = {}, int=100, int=0) {}
    virtual void queryUsers(const QString &, const QString & = {}, const QString & = {}, int=100, int=0) {}
    virtual void createStation(const QString &, const AdminNewStation &) {}
    virtual void setUserFrozen(const QString &, qint64, bool, const QString &) {}
    virtual void updateStation(const QString &, qint64, const AdminNewStation &, const QString &) {}
    virtual void setPileStatus(const QString &, qint64, const QString &, const QString &) {}
    virtual void restartPile(const QString &, qint64) {}
    virtual void queryAlarms(const QString &, const QString & = {}, const QString & = {}, int=100, int=0) {}
    virtual void acknowledgeAlarm(const QString &, qint64) {}
    virtual void queryTickets(const QString &, const QString & = {}, int=100, int=0) {}
    virtual void startTicket(const QString &, qint64, const QString &) {}
    virtual void resolveTicket(const QString &, qint64, const QString &) {}
    virtual void closeTicket(const QString &, qint64) {}

signals:
    void loginFinished(const AdminLoginResult &result);
    void ordersFinished(const AdminOrderQueryResult &result);
    void logsFinished(const AdminLogQueryResult &result);
    void overviewFinished(const AdminOverviewResult &result);
    void revenueFinished(const AdminRevenueResult &result);
    void revenueTrendFinished(const AdminRevenueTrendResult &result);
    void revenueSummaryFinished(const AdminRevenueSummaryResult &result);
    void stationsFinished(const AdminStationQueryResult &result);
    void pilesFinished(const AdminPileQueryResult &result);
    void usersFinished(const AdminUserQueryResult &result);
    void stationCreated(const AdminActionResult &result);
    void userFreezeFinished(const AdminActionResult &result);
    void stationUpdated(const AdminActionResult &result);
    void pileStatusFinished(const AdminActionResult &result);
    void pileRestartFinished(const AdminActionResult &result);
    void alarmsFinished(AdminResultKind kind, const QString &message, const QList<AdminAlarmSummary> &rows);
    void alarmAcknowledged(const AdminActionResult &result);
    void ticketsFinished(AdminResultKind kind, const QString &message, const QList<AdminTicketSummary> &rows);
    void ticketActionFinished(const AdminActionResult &result);
    void connectivityRestored();
    void connectivityLost();
};

Q_DECLARE_METATYPE(AdminLoginResult)
Q_DECLARE_METATYPE(AdminOrderQueryResult)
Q_DECLARE_METATYPE(AdminLogQueryResult)
Q_DECLARE_METATYPE(AdminOverviewResult)
Q_DECLARE_METATYPE(AdminRevenueResult)
Q_DECLARE_METATYPE(AdminRevenueTrendResult)
Q_DECLARE_METATYPE(AdminRevenueSummaryResult)
Q_DECLARE_METATYPE(AdminStationQueryResult)
Q_DECLARE_METATYPE(AdminPileQueryResult)
Q_DECLARE_METATYPE(AdminUserQueryResult)
Q_DECLARE_METATYPE(AdminActionResult)
