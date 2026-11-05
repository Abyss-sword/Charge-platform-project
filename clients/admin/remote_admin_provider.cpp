#include "remote_admin_provider.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include <limits>
#include <utility>

namespace {

QString failureMessage(const QJsonObject &response, const QString &fallback)
{
    const QString inner = response.value(QStringLiteral("error")).toObject()
                              .value(QStringLiteral("message")).toString();
    if (!inner.isEmpty()) return inner;
    const QString outer = response.value(QStringLiteral("message")).toString();
    return outer.isEmpty() ? fallback : outer;
}

bool readInteger(const QJsonValue &value, qint64 *result)
{
    if (!value.isDouble()) return false;
    constexpr qint64 invalid = std::numeric_limits<qint64>::min();
    const qint64 integer = value.toInteger(invalid);
    if ((integer == invalid && value != QJsonValue(invalid))
        || value != QJsonValue(integer)) return false;
    *result = integer;
    return true;
}

bool readString(const QJsonObject &object, const QString &name, QString *result,
                bool allowEmpty = false)
{
    const QJsonValue value = object.value(name);
    if (!value.isString() || (!allowEmpty && value.toString().isEmpty())) return false;
    *result = value.toString();
    return true;
}

bool readOptionalString(const QJsonObject &object, const QString &name, QString *result)
{
    const QJsonValue value = object.value(name);
    if (value.isNull()) {
        result->clear();
        return true;
    }
    return readString(object, name, result, true);
}

bool readDecimal(const QJsonObject &object, const QString &name, QString *result)
{
    static const QRegularExpression decimal(QStringLiteral("^(0|[1-9]\\d*)\\.\\d{2}$"));
    return readString(object, name, result)
        && decimal.match(*result).hasMatch();
}

} // namespace

RemoteAdminProvider::RemoteAdminProvider(QString host, quint16 port, QObject *parent)
    : IAdminProvider(parent), client_(new ServiceClient(std::move(host), port, this))
{
    connectClient();
}

RemoteAdminProvider::RemoteAdminProvider(ServiceClient *client, QObject *parent)
    : IAdminProvider(parent), client_(client)
{
    connectClient();
}

void RemoteAdminProvider::login(const QString &username, const QString &password)
{
    if (!client_ || pending_ != Pending::None || client_->hasPendingRequest()) {
        rejectBusy(Pending::Login);
        return;
    }
    pending_ = Pending::Login;
    if (client_->sendRequest(QStringLiteral("ADMIN_LOGIN"),
            {{QStringLiteral("username"), username},
             {QStringLiteral("password"), password}}).isEmpty()) {
        pending_ = Pending::None;
        rejectBusy(Pending::Login);
    }
}

void RemoteAdminProvider::queryOrders(const QString &adminToken, const QString &status,
                                      int limit, int offset)
{
    if (!client_ || pending_ != Pending::None || client_->hasPendingRequest()) {
        rejectBusy(Pending::Orders);
        return;
    }
    pending_ = Pending::Orders;
    QJsonObject payload{{QStringLiteral("adminToken"), adminToken},
                        {QStringLiteral("limit"), limit},
                        {QStringLiteral("offset"), offset}};
    if (!status.isEmpty()) payload.insert(QStringLiteral("status"), status);
    if (client_->sendRequest(QStringLiteral("ADMIN_ORDERS"), payload).isEmpty()) {
        pending_ = Pending::None;
        rejectBusy(Pending::Orders);
    }
}

void RemoteAdminProvider::queryLogs(const QString &adminToken, const QString &action,
                                    int limit, int offset)
{
    if (!client_ || pending_ != Pending::None || client_->hasPendingRequest()) {
        rejectBusy(Pending::Logs);
        return;
    }
    pending_ = Pending::Logs;
    QJsonObject payload{{QStringLiteral("adminToken"), adminToken},
                        {QStringLiteral("limit"), limit},
                        {QStringLiteral("offset"), offset}};
    if (!action.isEmpty()) payload.insert(QStringLiteral("action"), action);
    if (client_->sendRequest(QStringLiteral("ADMIN_LOGS"), payload).isEmpty()) {
        pending_ = Pending::None;
        rejectBusy(Pending::Logs);
    }
}

void RemoteAdminProvider::queryOverview(const QString &token)
{ send(Pending::Overview, QStringLiteral("ADMIN_OVERVIEW"), {{QStringLiteral("adminToken"), token}}); }
void RemoteAdminProvider::queryRevenue(const QString &token)
{ send(Pending::Revenue, QStringLiteral("ADMIN_REVENUE"), {{QStringLiteral("adminToken"), token}}); }
void RemoteAdminProvider::queryRevenueTrend(const QString &token, int days)
{ send(Pending::RevenueTrend, QStringLiteral("ADMIN_REVENUE_TREND"), {{QStringLiteral("adminToken"), token}, {QStringLiteral("days"), days}}); }

// S1-B：营收区间汇总（fromDate/toDate 为 UTC 日期 "YYYY-MM-DD"）。
void RemoteAdminProvider::queryRevenueSummary(const QString &token, const QString &fromDate,
                                              const QString &toDate)
{
    send(Pending::RevenueSummary, QStringLiteral("ADMIN_REVENUE_SUMMARY"),
         {{QStringLiteral("adminToken"), token},
          {QStringLiteral("fromDate"), fromDate},
          {QStringLiteral("toDate"), toDate}});
}
void RemoteAdminProvider::queryStations(const QString &token, const QString &search, int limit, int offset)
{
    QJsonObject p{{QStringLiteral("adminToken"),token},{QStringLiteral("limit"),limit},{QStringLiteral("offset"),offset}};
    if (!search.isEmpty()) p.insert(QStringLiteral("search"),search);
    send(Pending::Stations, QStringLiteral("ADMIN_STATIONS"), p);
}
void RemoteAdminProvider::queryPiles(const QString &token, qint64 stationId, const QString &status, int limit, int offset)
{
    QJsonObject p{{QStringLiteral("adminToken"),token},{QStringLiteral("limit"),limit},{QStringLiteral("offset"),offset}};
    if (stationId>0) p.insert(QStringLiteral("stationId"),stationId);
    if (!status.isEmpty()) p.insert(QStringLiteral("status"),status);
    send(Pending::Piles, QStringLiteral("ADMIN_PILES"), p);
}
void RemoteAdminProvider::queryUsers(const QString &token, const QString &status, const QString &search, int limit, int offset)
{
    QJsonObject p{{QStringLiteral("adminToken"),token},{QStringLiteral("limit"),limit},{QStringLiteral("offset"),offset}};
    if (!status.isEmpty()) p.insert(QStringLiteral("status"),status);
    if (!search.isEmpty()) p.insert(QStringLiteral("search"),search);
    send(Pending::Users, QStringLiteral("ADMIN_USERS"), p);
}
void RemoteAdminProvider::createStation(const QString &token, const AdminNewStation &station)
{
    QJsonArray piles;
    for (const auto &pile : station.piles) piles.append(QJsonObject{
        {QStringLiteral("serial"),pile.serial},{QStringLiteral("chargeType"),pile.chargeType},
        {QStringLiteral("ratedPowerKw"),pile.ratedPowerKw}});
    send(Pending::CreateStation, QStringLiteral("ADMIN_STATION_CREATE"), {
        {QStringLiteral("adminToken"),token},{QStringLiteral("name"),station.name},
        {QStringLiteral("address"),station.address},{QStringLiteral("latitude"),station.latitude},
        {QStringLiteral("longitude"),station.longitude},{QStringLiteral("priceYuanPerKwh"),station.priceYuanPerKwh},
        {QStringLiteral("piles"),piles}});
}
void RemoteAdminProvider::setUserFrozen(const QString &token, qint64 userId, bool frozen, const QString &reason)
{ send(Pending::FreezeUser, QStringLiteral("ADMIN_USER_FREEZE"), {{QStringLiteral("adminToken"),token},{QStringLiteral("userId"),userId},{QStringLiteral("frozen"),frozen},{QStringLiteral("reason"),reason}}); }
void RemoteAdminProvider::updateStation(const QString &token, qint64 id, const AdminNewStation &s, const QString &status)
{ send(Pending::UpdateStation, QStringLiteral("ADMIN_STATION_UPDATE"), {{"adminToken",token},{"stationId",id},{"name",s.name},{"address",s.address},{"latitude",s.latitude},{"longitude",s.longitude},{"priceYuanPerKwh",s.priceYuanPerKwh},{"status",status}}); }
void RemoteAdminProvider::setPileStatus(const QString &token, qint64 id, const QString &status, const QString &reason)
{ send(Pending::PileStatus, QStringLiteral("ADMIN_PILE_SET_STATUS"), {{"adminToken",token},{"pileId",id},{"status",status},{"reason",reason}}); }
void RemoteAdminProvider::restartPile(const QString &token, qint64 id)
{ send(Pending::PileRestart, QStringLiteral("ADMIN_PILE_RESTART"), {{"adminToken",token},{"pileId",id}}); }
void RemoteAdminProvider::queryAlarms(const QString &token, const QString &status, const QString &severity, int limit, int offset)
{ QJsonObject p{{"adminToken",token},{"limit",limit},{"offset",offset}}; if(!status.isEmpty())p.insert("status",status); if(!severity.isEmpty())p.insert("severity",severity); send(Pending::Alarms,"ADMIN_ALARMS",p); }
void RemoteAdminProvider::acknowledgeAlarm(const QString &token, qint64 id)
{ send(Pending::AlarmAck,"ADMIN_ALARM_ACK",{{"adminToken",token},{"alarmId",id}}); }
void RemoteAdminProvider::queryTickets(const QString &token, const QString &status, int limit, int offset)
{ QJsonObject p{{"adminToken",token},{"limit",limit},{"offset",offset}}; if(!status.isEmpty())p.insert("status",status); send(Pending::Tickets,"ADMIN_TICKETS",p); }
void RemoteAdminProvider::startTicket(const QString &token, qint64 id, const QString &assignee)
{ send(Pending::TicketAction,"ADMIN_TICKET_START",{{"adminToken",token},{"ticketId",id},{"assignee",assignee}}); }
void RemoteAdminProvider::resolveTicket(const QString &token, qint64 id, const QString &notes)
{ send(Pending::TicketAction,"ADMIN_TICKET_RESOLVE",{{"adminToken",token},{"ticketId",id},{"notes",notes}}); }
void RemoteAdminProvider::closeTicket(const QString &token, qint64 id)
{ send(Pending::TicketAction,"ADMIN_TICKET_CLOSE",{{"adminToken",token},{"ticketId",id}}); }

void RemoteAdminProvider::send(Pending requested, const QString &action, QJsonObject payload)
{
    if (!client_ || pending_ != Pending::None || client_->hasPendingRequest()) { rejectBusy(requested); return; }
    pending_=requested;
    if (client_->sendRequest(action,payload).isEmpty()) { pending_=Pending::None; rejectBusy(requested); }
}

void RemoteAdminProvider::connectClient()
{
    if (!client_) return;
    connect(client_, &ServiceClient::connectionStateChanged, this,
            [this](bool connected) {
        if (connected) emit connectivityRestored();
        else emit connectivityLost();
    });
    connect(client_, &ServiceClient::responseReceived, this,
            [this](const QJsonObject &response) {
        if (pending_ == Pending::None) return;
        handleResponse(response);
    });
    connect(client_, &ServiceClient::requestFailed, this,
            [this](ServiceClientError error, const QString &message) {
        if (pending_ == Pending::None) return;
        handleFailure(error, message);
    });
    connect(client_, &QObject::destroyed, this, [this] {
        client_.clear();
        if (pending_ != Pending::None) {
            const Pending failed = pending_;
            pending_ = Pending::None;
            emitFailure(failed, AdminResultKind::Error, QStringLiteral("运营服务不可用。"));
        }
    });
}

void RemoteAdminProvider::handleResponse(const QJsonObject &response)
{
    const Pending completed = pending_;
    pending_ = Pending::None;
    if (!response.value(QStringLiteral("ok")).toBool()) {
        const QString message = failureMessage(response, QStringLiteral("运营请求失败。"));
        const QString code = response.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(
            response.value(QStringLiteral("code")).toString());
        emitFailure(completed, AdminResultKind::Error, message, code);
        return;
    }
    const QJsonObject data = response.value(QStringLiteral("data")).toObject();
    if (completed == Pending::Login) {
        const QJsonValue token = data.value(QStringLiteral("adminToken"));
        if (!token.isString() || token.toString().isEmpty()) {
            emit loginFinished({AdminResultKind::Error,
                                QStringLiteral("登录响应缺少有效 adminToken。"), {}, {} });
            return;
        }
        emit loginFinished({AdminResultKind::Success,
                            response.value(QStringLiteral("message")).toString(),
                            token.toString(), {}});
        return;
    }

    if (completed == Pending::Overview) {
        AdminOverviewResult result;
        qint64 serviceCount=0, alarmCount=0;
        if (!readDecimal(data, QStringLiteral("todayRevenueYuan"), &result.todayRevenueYuan)
            || !readInteger(data.value(QStringLiteral("todayServiceCount")), &serviceCount)
            || !data.value(QStringLiteral("utilizationRate")).isDouble()
            || !data.value(QStringLiteral("onlineRate")).isDouble()
            || !readInteger(data.value(QStringLiteral("pendingAlarmCount")), &alarmCount)
            || serviceCount<0 || alarmCount<0 || serviceCount>std::numeric_limits<int>::max()
            || alarmCount>std::numeric_limits<int>::max()) {
            emit overviewFinished({AdminResultKind::Error, QStringLiteral("总览响应字段无效。"), {}, 0, 0.0, 0.0, 0}); return;
        }
        result.kind=AdminResultKind::Success; result.message=response.value(QStringLiteral("message")).toString();
        result.todayServiceCount=int(serviceCount); result.utilizationRate=data.value(QStringLiteral("utilizationRate")).toDouble();
        result.onlineRate=data.value(QStringLiteral("onlineRate")).toDouble(); result.pendingAlarmCount=int(alarmCount);
        // S1-B 扩展字段：旧服务端不提供时保持默认值，页面自行降级显示 "--"。
        result.monthRevenueYuan = data.value(QStringLiteral("monthRevenueYuan")).toString(
            QStringLiteral("0.00"));
        result.todayOrderCount = int(data.value(QStringLiteral("todayOrderCount")).toInt());
        // 查询响应带 snapshotVersion；兼容未来写路径的 changeVersion 命名。
        const QJsonValue snapshot = data.value(QStringLiteral("snapshotVersion")).isUndefined()
            ? data.value(QStringLiteral("changeVersion"))
            : data.value(QStringLiteral("snapshotVersion"));
        result.snapshotVersion = snapshot.toInteger();
        result.serverTimeUtc = data.value(QStringLiteral("serverTimeUtc")).toString();
        result.pileStatusCounts = data.value(QStringLiteral("pileStatusCounts")).toObject().toVariantMap();
        result.orderStatusCounts = data.value(QStringLiteral("orderStatusCounts")).toObject().toVariantMap();
        result.loadSeries24h = data.value(QStringLiteral("loadSeries24h")).toArray().toVariantList();
        result.latestAlarms = data.value(QStringLiteral("latestAlarms")).toArray().toVariantList();
        result.openTickets = data.value(QStringLiteral("openTickets")).toArray().toVariantList();
        result.abnormalPiles = data.value(QStringLiteral("abnormalPiles")).toArray().toVariantList();
        result.recentOrders = data.value(QStringLiteral("recentOrders")).toArray().toVariantList();
        emit overviewFinished(result); return;
    }
    if (completed == Pending::Revenue) {
        AdminRevenueResult result;
        if (!readDecimal(data, QStringLiteral("totalRevenueYuan"), &result.totalRevenueYuan)) {
            emit revenueFinished({AdminResultKind::Error, QStringLiteral("营收响应字段无效。"), {}}); return;
        }
        result.kind=AdminResultKind::Success; result.message=response.value(QStringLiteral("message")).toString();
        emit revenueFinished(result); return;
    }
    if (completed == Pending::RevenueTrend) {
        QList<AdminRevenuePoint> points;
        const QJsonValue values = data.value(QStringLiteral("points"));
        if (!values.isArray()) { emit revenueTrendFinished({AdminResultKind::Error, QStringLiteral("营收趋势响应字段无效。"), {}}); return; }
        for (const auto &entry : values.toArray()) {
            if (!entry.isObject()) { emit revenueTrendFinished({AdminResultKind::Error, QStringLiteral("营收趋势条目无效。"), {}}); return; }
            const auto row = entry.toObject(); AdminRevenuePoint point;
            if (!readString(row, QStringLiteral("date"), &point.date)
                || !readDecimal(row, QStringLiteral("revenueYuan"), &point.revenueYuan)) {
                emit revenueTrendFinished({AdminResultKind::Error, QStringLiteral("营收趋势字段无效。"), {}}); return;
            }
            points.append(point);
        }
        emit revenueTrendFinished({AdminResultKind::Success, response.value(QStringLiteral("message")).toString(), points}); return;
    }
    if (completed == Pending::RevenueSummary) {
        // S1-B：区间汇总解析；排行/直方图逐项校验关键字段。
        AdminRevenueSummaryResult result;
        if (!readString(data, QStringLiteral("fromDate"), &result.fromDate)
            || !readString(data, QStringLiteral("toDate"), &result.toDate)
            || !readDecimal(data, QStringLiteral("totalRevenueYuan"), &result.totalRevenueYuan)
            || !readString(data, QStringLiteral("totalEnergyKwh"), &result.totalEnergyKwh)
            || !readString(data, QStringLiteral("avgOrderYuan"), &result.avgOrderYuan)
            || !readString(data, QStringLiteral("avgPriceYuan"), &result.avgPriceYuan)) {
            emit revenueSummaryFinished({AdminResultKind::Error,
                                         QStringLiteral("营收汇总响应字段无效。")}); return;
        }
        result.orderCount = int(data.value(QStringLiteral("orderCount")).toInt());
        const QJsonValue ranking = data.value(QStringLiteral("stationRanking"));
        const QJsonValue histogram = data.value(QStringLiteral("hourHistogram"));
        if (!ranking.isArray() || !histogram.isArray()) {
            emit revenueSummaryFinished({AdminResultKind::Error,
                                         QStringLiteral("营收汇总响应缺少列表。")}); return;
        }
        result.stationRanking = ranking.toArray().toVariantList();
        result.hourHistogram = histogram.toArray().toVariantList();
        result.kind = AdminResultKind::Success;
        result.message = response.value(QStringLiteral("message")).toString();
        emit revenueSummaryFinished(result); return;
    }
    if (completed == Pending::CreateStation) {
        qint64 stationId=0;
        if (!readInteger(data.value(QStringLiteral("stationId")), &stationId) || stationId<=0) {
            emit stationCreated({AdminResultKind::Error, QStringLiteral("新建站点响应字段无效。"),0, {}}); return;
        }
        emit stationCreated({AdminResultKind::Success,response.value(QStringLiteral("message")).toString(),stationId, {}}); return;
    }
    if (completed == Pending::FreezeUser) {
        emit userFreezeFinished({AdminResultKind::Success,response.value(QStringLiteral("message")).toString(),0, {}}); return;
    }
    if (completed == Pending::UpdateStation) { emit stationUpdated({AdminResultKind::Success,response.value("message").toString(),data.value("stationId").toInteger(), {}}); return; }
    if (completed == Pending::PileStatus) { emit pileStatusFinished({AdminResultKind::Success,response.value("message").toString(),data.value("pileId").toInteger(), {}}); return; }
    if (completed == Pending::PileRestart) { emit pileRestartFinished({AdminResultKind::Success,response.value("message").toString(),data.value("pileId").toInteger(), {}}); return; }
    if (completed == Pending::AlarmAck) { emit alarmAcknowledged({AdminResultKind::Success,response.value("message").toString(),0, {}}); return; }
    if (completed == Pending::TicketAction) { emit ticketActionFinished({AdminResultKind::Success,response.value("message").toString(),0, {}}); return; }

    const QJsonValue rows = data.value(QStringLiteral("rows"));
    if (!rows.isArray()) {
        emitFailure(completed, AdminResultKind::Error, QStringLiteral("响应缺少 rows 数组。"));
        return;
    }
    if (completed == Pending::Alarms) {
        QList<AdminAlarmSummary> values; for (const auto &entry: rows.toArray()) { const auto r=entry.toObject(); AdminAlarmSummary a; if(!readInteger(r.value("alarmId"),&a.alarmId)||!readInteger(r.value("pileId"),&a.pileId)||!readString(r,"status",&a.status)||!readString(r,"occurredAt",&a.occurredAt)) { emit alarmsFinished(AdminResultKind::Error,"告警响应字段无效。",{}); return;} a.orderId=r.value("orderId").toInteger(); a.severity=r.value("severity").toString(); a.alarmType=r.value("alarmType").toString(); a.content=r.value("content").toString(); values.append(a);} emit alarmsFinished(AdminResultKind::Success,response.value("message").toString(),values); return;
    }
    if (completed == Pending::Tickets) {
        QList<AdminTicketSummary> values; for (const auto &entry: rows.toArray()) { const auto r=entry.toObject(); AdminTicketSummary t; if(!readInteger(r.value("ticketId"),&t.ticketId)||!readInteger(r.value("alarmId"),&t.alarmId)||!readString(r,"status",&t.status)||!readString(r,"createdAt",&t.createdAt)) { emit ticketsFinished(AdminResultKind::Error,"工单响应字段无效。",{}); return;} t.pileId=r.value("pileId").toInteger(); t.assignee=r.value("assignee").toString(); t.notes=r.value("notes").toString(); t.updatedAt=r.value("updatedAt").toString(); values.append(t);} emit ticketsFinished(AdminResultKind::Success,response.value("message").toString(),values); return;
    }
    if (completed == Pending::Logs) {
        QList<AdminLogSummary> logs;
        for (const QJsonValue &entry : rows.toArray()) {
            if (!entry.isObject()) {
                emit logsFinished({AdminResultKind::Error,
                                   QStringLiteral("日志条目必须是对象。"), {}});
                return;
            }
            const QJsonObject row = entry.toObject();
            AdminLogSummary log;
            if (!readInteger(row.value(QStringLiteral("logId")), &log.logId)
                || log.logId <= 0
                || !readString(row, QStringLiteral("actorType"), &log.actorType)
                || !readOptionalString(row, QStringLiteral("actorId"), &log.actorId)
                || !readString(row, QStringLiteral("action"), &log.action)
                || !readOptionalString(row, QStringLiteral("targetType"), &log.targetType)
                || !readOptionalString(row, QStringLiteral("targetId"), &log.targetId)
                || !readString(row, QStringLiteral("result"), &log.result)
                || !readString(row, QStringLiteral("createdAt"), &log.createdAt)) {
                emit logsFinished({AdminResultKind::Error,
                                   QStringLiteral("日志响应字段无效。"), {}});
                return;
            }
            logs.append(log);
        }
        emit logsFinished({AdminResultKind::Success,
                           response.value(QStringLiteral("message")).toString(), logs});
        return;
    }
    if (completed == Pending::Stations) {
        QList<AdminStationSummary> values;
        for (const auto &entry : rows.toArray()) {
            AdminStationSummary value; qint64 total=0,available=0;
            if (!entry.isObject()) { emit stationsFinished({AdminResultKind::Error,QStringLiteral("站点条目必须是对象。"),{}}); return; }
            const auto row=entry.toObject();
            if (!readInteger(row.value(QStringLiteral("stationId")),&value.stationId) || value.stationId<=0
                || !readString(row,QStringLiteral("name"),&value.name) || !readString(row,QStringLiteral("address"),&value.address)
                || !readString(row,QStringLiteral("status"),&value.status) || !readDecimal(row,QStringLiteral("priceYuanPerKwh"),&value.priceYuanPerKwh)
                || !readInteger(row.value(QStringLiteral("totalPiles")),&total) || !readInteger(row.value(QStringLiteral("availablePiles")),&available)
                || total<0 || available<0 || total>std::numeric_limits<int>::max() || available>std::numeric_limits<int>::max()) {
                emit stationsFinished({AdminResultKind::Error,QStringLiteral("站点响应字段无效。"),{}}); return;
            }
            value.totalPiles=int(total); value.availablePiles=int(available); values.append(value);
        }
        emit stationsFinished({AdminResultKind::Success,response.value(QStringLiteral("message")).toString(),values}); return;
    }
    if (completed == Pending::Piles) {
        QList<AdminPileSummary> values;
        for (const auto &entry : rows.toArray()) {
            AdminPileSummary value;
            if (!entry.isObject()) { emit pilesFinished({AdminResultKind::Error,QStringLiteral("电桩条目必须是对象。"),{}}); return; }
            const auto row=entry.toObject();
            if (!readInteger(row.value(QStringLiteral("pileId")),&value.pileId) || !readInteger(row.value(QStringLiteral("stationId")),&value.stationId)
                || value.pileId<=0 || value.stationId<=0 || !readString(row,QStringLiteral("serial"),&value.serial)
                || !readString(row,QStringLiteral("chargeType"),&value.chargeType) || !row.value(QStringLiteral("ratedPowerKw")).isDouble()
                || !readString(row,QStringLiteral("status"),&value.status) || !readOptionalString(row,QStringLiteral("lastHeartbeatAt"),&value.lastHeartbeatAt)) {
                emit pilesFinished({AdminResultKind::Error,QStringLiteral("电桩响应字段无效。"),{}}); return;
            }
            value.ratedPowerKw=row.value(QStringLiteral("ratedPowerKw")).toDouble(); values.append(value);
        }
        emit pilesFinished({AdminResultKind::Success,response.value(QStringLiteral("message")).toString(),values}); return;
    }
    if (completed == Pending::Users) {
        QList<AdminUserSummary> values;
        for (const auto &entry : rows.toArray()) {
            AdminUserSummary value;
            if (!entry.isObject()) { emit usersFinished({AdminResultKind::Error,QStringLiteral("用户条目必须是对象。"),{}}); return; }
            const auto row=entry.toObject();
            if (!readInteger(row.value(QStringLiteral("userId")),&value.userId) || value.userId<=0
                || !readString(row,QStringLiteral("phone"),&value.phone) || !readString(row,QStringLiteral("nickname"),&value.nickname,true)
                || !readDecimal(row,QStringLiteral("balanceYuan"),&value.balanceYuan) || !readString(row,QStringLiteral("status"),&value.status)
                || !readString(row,QStringLiteral("createdAt"),&value.createdAt)) {
                emit usersFinished({AdminResultKind::Error,QStringLiteral("用户响应字段无效。"),{}}); return;
            }
            values.append(value);
        }
        emit usersFinished({AdminResultKind::Success,response.value(QStringLiteral("message")).toString(),values}); return;
    }
    QList<AdminOrderSummary> orders;
    for (const QJsonValue &entry : rows.toArray()) {
        if (!entry.isObject()) {
            emit ordersFinished({AdminResultKind::Error,
                                 QStringLiteral("订单条目必须是对象。"), {}});
            return;
        }
        const QJsonObject row = entry.toObject();
        AdminOrderSummary order;
        if (!readString(row, QStringLiteral("orderId"), &order.orderId)
            || !readInteger(row.value(QStringLiteral("userId")), &order.userId)
            || !readInteger(row.value(QStringLiteral("pileId")), &order.pileId)
            || order.userId <= 0 || order.pileId <= 0
            || !readString(row, QStringLiteral("status"), &order.status)
            || !readDecimal(row, QStringLiteral("energyKwh"), &order.energyKwh)
            || !readDecimal(row, QStringLiteral("amountYuan"), &order.amountYuan)
            || !readString(row, QStringLiteral("createdAt"), &order.createdAt)
            || !readOptionalString(row, QStringLiteral("endedAt"), &order.endedAt)) {
            emit ordersFinished({AdminResultKind::Error,
                                 QStringLiteral("订单响应字段无效。"), {}});
            return;
        }
        orders.append(order);
    }
    emit ordersFinished({AdminResultKind::Success,
                         response.value(QStringLiteral("message")).toString(), orders});
}

void RemoteAdminProvider::handleFailure(ServiceClientError error, const QString &message)
{
    const Pending failed = pending_;
    pending_ = Pending::None;
    const bool offline = error == ServiceClientError::Offline
        || error == ServiceClientError::Disconnected;
    const AdminResultKind kind = offline ? AdminResultKind::Offline : AdminResultKind::Error;
    const QString detail = message.isEmpty()
        ? (offline ? QStringLiteral("无法连接运营服务。")
                   : QStringLiteral("运营请求失败，请稍后重试。"))
        : message;
    emitFailure(failed, kind, detail);
}

void RemoteAdminProvider::rejectBusy(Pending requested)
{
    emitFailure(requested, AdminResultKind::Error,
                client_ ? QStringLiteral("已有运营请求正在处理中。")
                        : QStringLiteral("运营服务不可用。"));
}

void RemoteAdminProvider::emitFailure(Pending pending, AdminResultKind kind,
                                      const QString &message, const QString &errorCode)
{
    switch (pending) {
    case Pending::Login: emit loginFinished({kind,message,{},errorCode}); break;
    case Pending::Overview: emit overviewFinished({kind,message,{},0,0.0,0.0,0}); break;
    case Pending::Revenue: emit revenueFinished({kind,message,{}}); break;
    case Pending::RevenueTrend: emit revenueTrendFinished({kind,message,{}}); break;
    case Pending::RevenueSummary: emit revenueSummaryFinished({kind,message}); break;
    case Pending::Stations: emit stationsFinished({kind,message,{}}); break;
    case Pending::Piles: emit pilesFinished({kind,message,{}}); break;
    case Pending::Orders: emit ordersFinished({kind,message,{}}); break;
    case Pending::Users: emit usersFinished({kind,message,{}}); break;
    case Pending::Logs: emit logsFinished({kind,message,{}}); break;
    case Pending::CreateStation: emit stationCreated({kind,message,0,errorCode}); break;
    case Pending::FreezeUser: emit userFreezeFinished({kind,message,0,errorCode}); break;
    case Pending::UpdateStation: emit stationUpdated({kind,message,0,errorCode}); break;
    case Pending::PileStatus: emit pileStatusFinished({kind,message,0,errorCode}); break;
    case Pending::PileRestart: emit pileRestartFinished({kind,message,0,errorCode}); break;
    case Pending::Alarms: emit alarmsFinished(kind,message,{}); break;
    case Pending::AlarmAck: emit alarmAcknowledged({kind,message,0,errorCode}); break;
    case Pending::Tickets: emit ticketsFinished(kind,message,{}); break;
    case Pending::TicketAction: emit ticketActionFinished({kind,message,0,errorCode}); break;
    case Pending::None: break;
    }
}
