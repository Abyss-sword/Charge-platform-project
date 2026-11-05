#include "server/services/order_service.h"

#include "server/repositories/order_repository.h"
#include "server/services/linkage.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <utility>

namespace {

// M1-02 归属校验：requireUserId>0 且订单存在但 user_id 不匹配 → true（表示应拒）。
// 订单不存在不在此处拒绝（沿用原 NOT_FOUND/状态逻辑），不泄露他人订单存在性由
// 调用方在状态检查前先做归属判断；此处仅对“存在且不归属”给出 AUTH_INVALID。
bool orderOwnedByOther(const std::optional<OrderRecord> &order, qint64 requireUserId)
{
    return requireUserId > 0 && order && order->userId != requireUserId;
}
// 统一失败结果构造。
OrderResult failure(const QString &errorCode)
{
    return {false, errorCode, {}, {}, {}, {}};
}

// BEGIN IMMEDIATE：立即获取 SQLite 写事务资格（而非普通延迟事务）。
// 这是防“一桩两人抢”的第一道闸——同一时刻数据库只允许一个写事务，
// 后续“查活动订单→条件更新”在持锁窗口内完成，竞态窗口被大幅压缩。
bool beginImmediate(QSqlDatabase database)
{
    QSqlQuery query(database);
    return query.exec(QStringLiteral("BEGIN IMMEDIATE"));
}

// 提交失败（忙等超时等）时回滚，保证不留半笔交易。
// bump=true 时提交前把 linkage_meta.changeVersion 原子 +1（S1-A 联动契约）；
// 幂等回放分支没有业务写入，传 bump=false 避免制造虚假版本变更。
bool commitOrRollback(QSqlDatabase database, bool bump = true)
{
    if (bump && linkage::bumpVersion(database) < 0) {
        database.rollback();
        return false;
    }
    if (database.commit()) {
        return true;
    }
    database.rollback();
    return false;
}

QVariantMap orderRow(QSqlQuery &query)
{
    return {{QStringLiteral("orderId"), query.value(0)},
            {QStringLiteral("userId"), query.value(1)},
            {QStringLiteral("pileId"), query.value(2)},
            {QStringLiteral("status"), query.value(3)},
            {QStringLiteral("energyKwh"), query.value(4)},
            {QStringLiteral("unitPriceYuanPerKwh"), query.value(5)},
            {QStringLiteral("amountYuan"), query.value(6)},
            {QStringLiteral("reservedUntil"), query.value(7)},
            {QStringLiteral("startedAt"), query.value(8)},
            {QStringLiteral("endedAt"), query.value(9)},
            {QStringLiteral("createdAt"), query.value(10)}};
}

QString orderSelect()
{
    return QStringLiteral(
        "SELECT id,user_id,pile_id,status,energy_kwh,unit_price_yuan_per_kwh,"
        "amount_yuan,reserved_until,started_at,ended_at,created_at FROM charge_order ");
}
}

OrderService::OrderService(QSqlDatabase database, Clock clock)
    : database_(std::move(database))
    , clock_(std::move(clock))
{
}

// 预约主流程（答辩核心，防一桩多占）：
// 参数校验 → BEGIN IMMEDIATE → 防重回放（动作/参数不符拒 DUPLICATE_REQUEST）→
// 用户存在且未冻结 → 本人无未完成订单 → 该桩无活动订单 → 桩为 IDLE（非营业站视为不可用）→
// 15 分钟期限 → “建单 + 桩 IDLE→RESERVED + 记录防重请求”三写一事务，任一失败整体回滚。
// 最终兜底：部分唯一索引 ux_charge_order_active_pile 保证并发下同一桩只有一条活动订单。
OrderResult OrderService::reserve(qint64 userId, qint64 pileId, const QString &requestId)
{
    if (userId <= 0 || pileId <= 0 || requestId.trimmed().isEmpty()) {
        return failure(QStringLiteral("PARAM_INVALID"));
    }
    if (!database_.isOpen() || !beginImmediate(database_)) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }

    OrderRepository repository(database_);
    QString error;
    const auto replay = repository.reservationByRequest(requestId, &error);
    if (!error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    // 防重回放：同一 requestId 且动作/用户/桩一致 → 直接复放原成功结果（幂等）。
    if (replay) {
        if (!commitOrRollback(database_, /*bump=*/false)) {
            return failure(QStringLiteral("DATABASE_ERROR"));
        }
        if (replay->action != QStringLiteral("RESERVE")
            || replay->userId != userId || replay->pileId != pileId) {
            return failure(QStringLiteral("DUPLICATE_REQUEST"));
        }
        return {true, QStringLiteral("OK"), replay->orderId, {}, {}, {}};
    }

    const auto userStatus = repository.userStatus(userId, &error);
    if (!error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (!userStatus) {
        database_.rollback();
        return failure(QStringLiteral("PARAM_INVALID"));
    }
    if (*userStatus == QStringLiteral("FROZEN")) {
        database_.rollback();
        return failure(QStringLiteral("USER_FROZEN"));
    }

    if (repository.hasUnfinishedOrder(userId, &error)) {
        database_.rollback();
        return failure(QStringLiteral("UNFINISHED_ORDER"));
    }
    if (!error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }

    if (repository.hasActiveOrderForPile(pileId, &error)) {
        database_.rollback();
        return failure(QStringLiteral("PILE_UNAVAILABLE"));
    }
    if (!error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }

    const auto status = repository.pileStatus(pileId, &error);
    if (!error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (!status || *status != QStringLiteral("IDLE")) {
        database_.rollback();
        return failure(QStringLiteral("PILE_UNAVAILABLE"));
    }

    // 15 分钟预约期限：now + 15*60 秒写入 reserved_until，由后台扫描/开始时校验过期。
    const QDateTime now = clock_().toUTC();
    const QString orderId = QStringLiteral("ORD-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    // 三个写操作 + 提交必须同时成功：订单落库、桩 IDLE→RESERVED、写预约防重记录。
    if (!repository.createReservation(orderId, userId, pileId, now.addSecs(15 * 60), now,
                                      &error)
        || !repository.changePileStatus(pileId, QStringLiteral("IDLE"),
                                        QStringLiteral("RESERVED"), &error)
        || !repository.recordReservationRequest(requestId, userId, pileId, orderId, &error)
        || !commitOrRollback(database_)) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    return {true, QStringLiteral("OK"), orderId, {}, {}, {}};
}

// 过期预约回收（由后台 5 秒定时器周期调用）：
// 逐条把超时的 RESERVED 订单置 EXPIRED、桩 RESERVED→IDLE，全部写操作在一个事务内。
int OrderService::expireReservations()
{
    if (!database_.isOpen() || !beginImmediate(database_)) {
        return 0;
    }
    const QDateTime now = clock_().toUTC();
    OrderRepository repository(database_);
    QString error;
    const auto expired = repository.expiredReservations(now, &error);
    if (!error.isEmpty()) {
        database_.rollback();
        return 0;
    }
    int count = 0;
    for (const auto &reservation : expired) {
        if (!repository.expireOrder(reservation.orderId, now, &error)) {
            database_.rollback();
            return 0;
        }
        const bool released = repository.changePileStatus(
            reservation.pileId, QStringLiteral("RESERVED"), QStringLiteral("IDLE"), &error);
        if (!released && !error.isEmpty()) {
            database_.rollback();
            return 0;
        }
        ++count;
    }
    if (!commitOrRollback(database_)) {
        return 0;
    }
    return count;
}

// 用户取消预约：仅 RESERVED 可取消（否则 ORDER_STATE_CONFLICT）；释放电桩但不清理设备故障/离线态。
OrderResult OrderService::cancel(const QString &orderId, qint64 requireUserId)
{
    if (orderId.trimmed().isEmpty()) {
        return failure(QStringLiteral("PARAM_INVALID"));
    }
    if (!database_.isOpen() || !beginImmediate(database_)) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    OrderRepository repository(database_);
    QString error;
    const auto order = repository.findOrder(orderId, &error);
    if (!error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (orderOwnedByOther(order, requireUserId)) {
        database_.rollback();
        return failure(QStringLiteral("AUTH_INVALID"));
    }
    if (!order || order->status != QStringLiteral("RESERVED")) {
        database_.rollback();
        return failure(QStringLiteral("ORDER_STATE_CONFLICT"));
    }
    if (!repository.cancelOrder(orderId, clock_().toUTC(), &error)) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    // Cancel the reservation without clearing a device fault or offline state.
    repository.changePileStatus(order->pileId, QStringLiteral("RESERVED"),
                                QStringLiteral("IDLE"), &error);
    if (!error.isEmpty() || !commitOrRollback(database_)) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    return {true, QStringLiteral("OK"), orderId, order->energyKwh, {}, {}};
}

// 开始充电：RESERVED→CHARGING；开始前再次校验用户未冻结；
// 若预约已超期（reserved_until <= now），当场落 EXPIRED 并释放桩，然后拒绝开始；
// 另外校验桩状态仍为 RESERVED、锁定开始时单价快照（详见 stop() 的锁价逻辑）。
OrderResult OrderService::start(const QString &orderId, qint64 requireUserId)
{
    if (orderId.trimmed().isEmpty()) {
        return failure(QStringLiteral("PARAM_INVALID"));
    }
    if (!database_.isOpen() || !beginImmediate(database_)) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    OrderRepository repository(database_);
    QString error;
    const auto order = repository.findOrder(orderId, &error);
    if (orderOwnedByOther(order, requireUserId)) {
        database_.rollback();
        return failure(QStringLiteral("AUTH_INVALID"));
    }
    if (!error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    const QDateTime now = clock_().toUTC();
    if (!order || order->status != QStringLiteral("RESERVED")) {
        database_.rollback();
        return failure(QStringLiteral("ORDER_STATE_CONFLICT"));
    }
    const auto userStatus = repository.userStatus(order->userId, &error);
    if (!error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (!userStatus || *userStatus == QStringLiteral("FROZEN")) {
        database_.rollback();
        return failure(userStatus ? QStringLiteral("USER_FROZEN")
                                  : QStringLiteral("PARAM_INVALID"));
    }
    // 超期预约：当场置 EXPIRED 并释放桩（同样不清理设备故障/离线态），返回状态冲突。
    if (!order->reservedUntil.isValid() || order->reservedUntil <= now) {
        if (!repository.expireOrder(orderId, now, &error)) {
            database_.rollback();
            return failure(QStringLiteral("DATABASE_ERROR"));
        }
        repository.changePileStatus(order->pileId, QStringLiteral("RESERVED"),
                                    QStringLiteral("IDLE"), &error);
        if (!error.isEmpty() || !commitOrRollback(database_)) {
            database_.rollback();
            return failure(QStringLiteral("DATABASE_ERROR"));
        }
        return failure(QStringLiteral("ORDER_STATE_CONFLICT"));
    }
    const auto pileStatus = repository.pileStatus(order->pileId, &error);
    if (!error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (!pileStatus || *pileStatus != QStringLiteral("RESERVED")) {
        database_.rollback();
        return failure(QStringLiteral("PILE_UNAVAILABLE"));
    }
    // M1-03：开始前要求同桩有有效且接收时间不超过 15 秒的读数（新鲜基准）；
    // 否则 DEVICE_TIMEOUT，订单保持 RESERVED。
    QSqlQuery fresh(database_);
    fresh.prepare(QStringLiteral(
        "SELECT energy_kwh FROM pile_telemetry WHERE pile_id=? "
        "ORDER BY recorded_at DESC,id DESC LIMIT 1"));
    fresh.addBindValue(order->pileId);
    QString meterStart;
    if (!fresh.exec()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (!fresh.next()) {
        database_.rollback();
        return failure(QStringLiteral("DEVICE_TIMEOUT"));
    }
    meterStart = fresh.value(0).toString();
    QSqlQuery heartbeat(database_);
    heartbeat.prepare(QStringLiteral(
        "SELECT last_heartbeat_at FROM pile_info WHERE id=?"));
    heartbeat.addBindValue(order->pileId);
    QString lastHeartbeat;
    if (!heartbeat.exec() || !heartbeat.next()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    lastHeartbeat = heartbeat.value(0).toString();
    const QDateTime heartbeatAt =
        QDateTime::fromString(lastHeartbeat, Qt::ISODate);
    if (!heartbeatAt.isValid() || heartbeatAt.secsTo(now) > 15) {
        database_.rollback();
        return failure(QStringLiteral("DEVICE_TIMEOUT"));
    }

    // 锁价快照：开始充电时读取站点单价存入订单（startOrder），结算按该快照计费；
    // 表计基准与锁价同一事务写入（01 文档 4.2）。
    const auto price = repository.stationPriceForPile(order->pileId, &error);
    if (!price || !error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    // 开单 + 桩 RESERVED→CHARGING + 提交，同一事务。
    if (!repository.startOrder(orderId, now, *price, meterStart, &error)
        || !repository.changePileStatus(order->pileId, QStringLiteral("RESERVED"),
                                        QStringLiteral("CHARGING"), &error)
        || !commitOrRollback(database_)) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    return {true, QStringLiteral("OK"), orderId, order->energyKwh, *price, {}};
}

// 订单电量上报：仅 CHARGING 订单可报；电量不允许倒退（防止计量回退）；
// 按旧电量条件更新（CAS），并发上报时只有一个旧值匹配成功。
OrderResult OrderService::recordTelemetry(const QString &orderId,
                                          const DecimalValue &energyKwh)
{
    if (orderId.trimmed().isEmpty() || energyKwh.isNegative()) {
        return failure(QStringLiteral("PARAM_INVALID"));
    }
    if (!database_.isOpen() || !beginImmediate(database_)) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    OrderRepository repository(database_);
    QString error;
    const auto order = repository.findOrder(orderId, &error);
    if (!error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (!order || order->status != QStringLiteral("CHARGING")) {
        database_.rollback();
        return failure(QStringLiteral("ORDER_STATE_CONFLICT"));
    }
    // 电量单调性检查：新电量不能小于订单已记录电量。
    if (energyKwh < order->energyKwh) {
        database_.rollback();
        return failure(QStringLiteral("PARAM_INVALID"));
    }
    // CAS 更新：WHERE status='CHARGING' AND energy_kwh=旧值，受影响行数必须为 1。
    if (!repository.updateEnergy(orderId, order->energyKwh, energyKwh, &error)
        || !commitOrRollback(database_)) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    return {true, QStringLiteral("OK"), orderId, energyKwh, {}, {}};
}

// 停止充电并结算（答辩高频）：
// CHARGING→PENDING_PAYMENT；单价取开始充电时的锁价快照（priceLocked），
// 升级前的旧订单无快照则退回“停止时读取站点单价”的旧规则；
// 金额 = 电量 × 单价，DecimalValue 高精度相乘后量化两位（8.35×1.23 → 10.27）。
// 正常桩结算后保持占用，等支付完成才释放；故障/离线桩上的订单可结算但桩不会被错误复活。
OrderResult OrderService::stop(const QString &orderId, qint64 requireUserId)
{
    if (orderId.trimmed().isEmpty()) {
        return failure(QStringLiteral("PARAM_INVALID"));
    }
    if (!database_.isOpen() || !beginImmediate(database_)) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    OrderRepository repository(database_);
    QString error;
    const auto order = repository.findOrder(orderId, &error);
    if (!error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (orderOwnedByOther(order, requireUserId)) {
        database_.rollback();
        return failure(QStringLiteral("AUTH_INVALID"));
    }
    if (!order || order->status != QStringLiteral("CHARGING")) {
        database_.rollback();
        return failure(QStringLiteral("ORDER_STATE_CONFLICT"));
    }
    // 计费单价：优先用开始时的锁价快照；旧订单回退到停止时站点单价。
    // Pre-upgrade charging orders have no start-price snapshot: retain their old rule.
    const auto unitPrice = order->priceLocked
        ? std::optional<DecimalValue>{order->unitPriceYuanPerKwh}
        : repository.stationPriceForPile(order->pileId, &error);
    if (!unitPrice || !error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    // 电量 × 单价 → 两位量化金额；CHARGING→PENDING_PAYMENT 落库并提交。
    const DecimalValue amount = multiplyRounded(order->energyKwh, *unitPrice);
    if (!repository.stopOrder(orderId, *unitPrice, amount, clock_().toUTC(), &error)
        || !commitOrRollback(database_)) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    return {true, QStringLiteral("OK"), orderId, order->energyKwh, *unitPrice, amount};
}

// 订单详情查询（requireUserId>0 时校验归属，他人订单 AUTH_INVALID，不泄露内容）。
OrderQueryResult OrderService::detail(const QString &orderId, qint64 requireUserId) const
{
    if (orderId.trimmed().isEmpty()) return {false, QStringLiteral("PARAM_INVALID"), {}};
    QSqlQuery query(database_);
    query.prepare(orderSelect() + QStringLiteral("WHERE id=?"));
    query.addBindValue(orderId);
    if (!query.exec()) return {false, QStringLiteral("DATABASE_ERROR"), {}};
    if (!query.next()) {
        // 无此订单：NOT_FOUND（用户可感知历史里没有该单）。
        return {false, QStringLiteral("NOT_FOUND"), {}};
    }
    const QVariantMap row = orderRow(query);
    if (requireUserId > 0 && row.value(QStringLiteral("userId")).toLongLong() != requireUserId) {
        return {false, QStringLiteral("AUTH_INVALID"), {}};
    }
    return {true, QStringLiteral("OK"), row};
}

// 活动订单查询：断线重开后客户端据此恢复界面状态（无活动订单也返回成功但 order 为空）。
OrderQueryResult OrderService::active(qint64 userId) const
{
    if (userId <= 0) return {false, QStringLiteral("PARAM_INVALID"), {}};
    QSqlQuery query(database_);
    query.prepare(orderSelect() + QStringLiteral(
        "WHERE user_id=? AND status IN ('RESERVED','CHARGING','PENDING_PAYMENT') "
        "ORDER BY created_at DESC,id LIMIT 1"));
    query.addBindValue(userId);
    if (!query.exec()) return {false, QStringLiteral("DATABASE_ERROR"), {}};
    if (!query.next()) return {true, QStringLiteral("OK"), {}};
    return {true, QStringLiteral("OK"), orderRow(query)};
}

// 历史订单分页（仅终态 FINISHED/CANCELED/EXPIRED），limit 上限 100，按创建时间倒序。
OrderListResult OrderService::history(qint64 userId, int limit, int offset) const
{
    if (userId <= 0 || limit <= 0 || limit > 100 || offset < 0) {
        return {false, QStringLiteral("PARAM_INVALID"), {}};
    }
    QSqlQuery query(database_);
    query.prepare(orderSelect() + QStringLiteral(
        "WHERE user_id=? AND status IN ('FINISHED','CANCELED','EXPIRED') "
        "ORDER BY created_at DESC,id LIMIT ? OFFSET ?"));
    query.addBindValue(userId);
    query.addBindValue(limit);
    query.addBindValue(offset);
    if (!query.exec()) return {false, QStringLiteral("DATABASE_ERROR"), {}};
    QList<QVariantMap> rows;
    while (query.next()) rows.append(orderRow(query));
    return {true, QStringLiteral("OK"), rows};
}

// 调度器实现：QTimer 定期触发 expireReservations（默认 10 秒仅供独立使用；
// 实际生产入口是 ServerBackend 的 5 秒定时器，见 tcp_server.cpp）。
ReservationExpiryScheduler::ReservationExpiryScheduler(OrderService &service, int intervalMs)
    : service_(service)
{
    timer_.setInterval(intervalMs);
    QObject::connect(&timer_, &QTimer::timeout, &timer_, [this] {
        service_.expireReservations();
    });
    timer_.start();
}

int ReservationExpiryScheduler::intervalMs() const
{
    return timer_.interval();
}
