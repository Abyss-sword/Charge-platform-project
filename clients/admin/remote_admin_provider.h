#pragma once

#include "admin_provider.h"
#include "network/service_client.h"

#include <QPointer>

class RemoteAdminProvider final : public IAdminProvider
{
    Q_OBJECT
public:
    explicit RemoteAdminProvider(QString host, quint16 port, QObject *parent = nullptr);
    explicit RemoteAdminProvider(ServiceClient *client, QObject *parent = nullptr);

    void login(const QString &username, const QString &password) override;
    void queryOrders(const QString &adminToken, const QString &status = {},
                     int limit = 100, int offset = 0) override;
    void queryLogs(const QString &adminToken, const QString &action = {},
                   int limit = 100, int offset = 0) override;
    void queryOverview(const QString &adminToken) override;
    void queryRevenue(const QString &adminToken) override;
    void queryRevenueTrend(const QString &adminToken, int days) override;
    void queryRevenueSummary(const QString &adminToken, const QString &fromDate,
                             const QString &toDate) override;
    void queryStations(const QString &adminToken, const QString &search={}, int limit=100, int offset=0) override;
    void queryPiles(const QString &adminToken, qint64 stationId=0, const QString &status={}, int limit=100, int offset=0) override;
    void queryUsers(const QString &adminToken, const QString &status={}, const QString &search={}, int limit=100, int offset=0) override;
    void createStation(const QString &adminToken, const AdminNewStation &station) override;
    void setUserFrozen(const QString &adminToken, qint64 userId, bool frozen, const QString &reason) override;
    void updateStation(const QString &, qint64, const AdminNewStation &, const QString &) override;
    void setPileStatus(const QString &, qint64, const QString &, const QString &) override;
    void restartPile(const QString &, qint64) override;
    void queryAlarms(const QString &, const QString & = {}, const QString & = {}, int=100, int=0) override;
    void acknowledgeAlarm(const QString &, qint64) override;
    void queryTickets(const QString &, const QString & = {}, int=100, int=0) override;
    void startTicket(const QString &, qint64, const QString &) override;
    void resolveTicket(const QString &, qint64, const QString &) override;
    void closeTicket(const QString &, qint64) override;

private:
    enum class Pending { None, Login, Overview, Revenue, RevenueTrend, RevenueSummary, Stations, Piles, Orders, Users, Logs, CreateStation, FreezeUser, UpdateStation, PileStatus, PileRestart, Alarms, AlarmAck, Tickets, TicketAction };
    void connectClient();
    void handleResponse(const QJsonObject &response);
    void handleFailure(ServiceClientError error, const QString &message);
    void rejectBusy(Pending requested);
    void send(Pending pending, const QString &action, QJsonObject payload);
    void emitFailure(Pending pending, AdminResultKind kind, const QString &message,
                     const QString &errorCode = {});

    QPointer<ServiceClient> client_;
    Pending pending_ = Pending::None;
};
