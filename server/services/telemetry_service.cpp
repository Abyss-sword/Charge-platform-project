#include "server/services/telemetry_service.h"

#include "server/repositories/alarm_repository.h"
#include "server/repositories/order_repository.h"
#include "server/repositories/telemetry_repository.h"
#include "server/services/linkage.h"

#include <QSqlQuery>

#include <cmath>
#include <utility>

// 统一设备计量实现（M1-03）：设备遥测是订单计量的唯一驱动源。
// 原则：
//  - 单请求单事务：遥测明细、心跳、订单用量、桩状态、告警、工单全部一次提交；
//  - 订单用量 = 当前累计读数 - 开始基准（charge_order.meter_start_kwh，008 迁移），
//    只增不减；完全重复读数幂等跳过；同时间不同值/时间倒退/电量倒退一律拒绝；
//  - 故障结算（过热/离线）先计入本条合法增量再冻结，金额 = 锁价 × 用量半向上两位；
//  - 管理禁用（admin_status_override，009 迁移）优先于通信恢复推导，正常遥测不清除。
namespace {
TelemetryResult failure(const QString &errorCode)
{
    return {false, errorCode};
}

bool beginImmediate(QSqlDatabase database)
{
    QSqlQuery query(database);
    return query.exec(QStringLiteral("BEGIN IMMEDIATE"));
}

// 提交失败时回滚；提交前原子抬升联动版本（S1-A 契约，见 linkage.h）。
bool commitOrRollback(QSqlDatabase database)
{
    if (linkage::bumpVersion(database) < 0) {
        database.rollback();
        return false;
    }
    if (database.commit()) {
        return true;
    }
    database.rollback();
    return false;
}

// 物理量范围校验：温度 -60~200℃；电压、电流 0~1000；功率 0~1000000 整数瓦。
bool validPhysicalValues(const TelemetryInput &input)
{
    return std::isfinite(input.temperatureC) && std::isfinite(input.voltageV)
        && std::isfinite(input.currentA) && input.temperatureC >= -60.0
        && input.temperatureC <= 200.0 && input.voltageV >= 0.0
        && input.voltageV <= 1000.0 && input.currentA >= 0.0
        && input.currentA <= 1000.0 && input.powerW >= 0 && input.powerW <= 1000000;
}

// 语义化桩状态：管理禁用优先；其次按活动订单推导占用（PENDING_PAYMENT 仍占桩，
// 但显示为 CHARGING 占用而非"正在充电"——充电中统计以订单 CHARGING 口径为准，见 01 第5节）。
QString pileDisplayStatus(const TelemetryPileRecord &pile)
{
    if (!pile.adminOverride.isEmpty()) {
        return pile.adminOverride; // 'FAULT' / 'OFFLINE'
    }
    if (pile.activeOrderStatus == QStringLiteral("RESERVED")) {
        return QStringLiteral("RESERVED");
    }
    if (pile.activeOrderStatus == QStringLiteral("CHARGING")
        || pile.activeOrderStatus == QStringLiteral("PENDING_PAYMENT")) {
        return QStringLiteral("CHARGING");
    }
    return QStringLiteral("IDLE");
}
}

TelemetryService::TelemetryService(QSqlDatabase database, Clock clock,
                                   double overheatThresholdC)
    : database_(std::move(database))
    , clock_(std::move(clock))
    , overheatThresholdC_(overheatThresholdC)
{
}

// 遥测上报主流程（唯一计量入口，答辩高频）：
// 1) 参数与物理量校验；时间最多超前服务端 5 分钟；
// 2) 桩存在 + 时序/电量单调（倒退拒绝）+ 完全重复幂等跳过；
// 3) 表计归零（读数 < 已应用基准）在无活动订单时视为换表，拒绝并提示维护；
//    有活动订单时同样拒绝（一阶段不做换表账务）；
// 4) CHARGING 订单用量更新（累计 - 基准，条件更新保只增）；
// 5) 过热：本条合法增量计入后冻结订单（锁价）、桩 FAULT、告警/工单；
// 6) 正常上报恢复通信状态（OFFLINE/FAULT → 按占用推导），但管理禁用不清除；
// 7) 全部写入同一事务，任一失败整体回滚。
TelemetryResult TelemetryService::record(const TelemetryInput &input)
{
    const QDateTime now = clock_().toUTC();
    const QDateTime recordedAt = input.recordedAt.toUTC();
    if (input.pileId <= 0 || !recordedAt.isValid() || !validPhysicalValues(input)
        || input.energyKwh.isNegative() || recordedAt > now.addSecs(5 * 60)) {
        return failure(QStringLiteral("PARAM_INVALID"));
    }
    if (!database_.isOpen() || !beginImmediate(database_)) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }

    TelemetryRepository telemetry(database_);
    AlarmRepository alarms(database_);
    QString error;
    const auto pile = telemetry.findPile(input.pileId, &error);
    if (!error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (!pile) {
        database_.rollback();
        return failure(QStringLiteral("PARAM_INVALID"));
    }

    // 完全重复（同时间同值）：幂等成功，不重复写明细、不重复计量、不重复告警。
    const bool exactDuplicate = pile->latestRecordedAt == recordedAt
        && pile->latestEnergyKwh && *pile->latestEnergyKwh == input.energyKwh;
    if (exactDuplicate) {
        database_.rollback();
        // 2026-09-13（第二阶段 T08 修复）：幂等重复必须回报 duplicate=true，
        // 服务层与协议层的去重断言都依赖该标志。
        return {true, QStringLiteral("OK"), true};
    }
    // 同时间不同内容：拒绝（不允许同时间戳携带不同值覆盖最新读数，01 文档 4.2）。
    if (pile->latestRecordedAt.isValid() && recordedAt == pile->latestRecordedAt) {
        database_.rollback();
        return failure(QStringLiteral("TELEMETRY_OUT_OF_ORDER"));
    }
    // 时序与电量单调性：倒退即拒绝。
    if ((pile->latestRecordedAt.isValid() && recordedAt < pile->latestRecordedAt)
        || (pile->latestEnergyKwh && input.energyKwh < *pile->latestEnergyKwh)) {
        database_.rollback();
        return failure(QStringLiteral("TELEMETRY_OUT_OF_ORDER"));
    }

    // 活动订单与基准。
    const QString activeOrderId = pile->activeOrderId;
    const bool chargingOrder = pile->activeOrderStatus == QStringLiteral("CHARGING");
    std::optional<DecimalValue> meterStart;
    if (chargingOrder) {
        QSqlQuery baseline(database_);
        baseline.prepare(QStringLiteral(
            "SELECT meter_start_kwh FROM charge_order WHERE id=? AND status='CHARGING'"));
        baseline.addBindValue(activeOrderId);
        if (!baseline.exec() || !baseline.next() || baseline.value(0).isNull()) {
            database_.rollback();
            return failure(QStringLiteral("ORDER_STATE_CONFLICT"));
        }
        meterStart = DecimalValue::parseCanonical(baseline.value(0).toString());
        if (!meterStart) {
            database_.rollback();
            return failure(QStringLiteral("DATABASE_ERROR"));
        }
        // 设备累计低于开始基准 = 表计疑似归零/换表：拒绝当前数据并要求维护，
        // 不伪造差值（01 文档 4.2）。
        if (input.energyKwh < *meterStart) {
            database_.rollback();
            return failure(QStringLiteral("METER_RESET_SUSPECTED"));
        }
    }

    // 过热判定（本条合法增量先计入再冻结）。
    const bool overheated = input.temperatureC >= overheatThresholdC_;

    // 目标桩状态：管理禁用 > 过热 FAULT > 恢复推导。
    QString nextStatus = pile->status;
    if (!pile->adminOverride.isEmpty()) {
        nextStatus = pile->adminOverride;
    } else if (overheated) {
        nextStatus = QStringLiteral("FAULT");
    } else if (pile->status == QStringLiteral("OFFLINE")
               || pile->status == QStringLiteral("FAULT")) {
        nextStatus = pileDisplayStatus(*pile);
    }

    // 写遥测明细 + 刷新心跳与状态（同一事务）。
    if (!telemetry.insertTelemetry(input.pileId, recordedAt, input.temperatureC,
                                    input.voltageV, input.currentA, input.powerW,
                                    input.energyKwh, &error)
        || !telemetry.updateHeartbeatAndStatus(input.pileId, now, nextStatus, &error)) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }

    // 订单用量：当前累计 - 开始基准（只增；Decimal 两位）。
    std::optional<DecimalValue> orderEnergy;  // 本条遥测计入订单后的累计用量，随结果返回。
    if (chargingOrder && meterStart) {
        const DecimalValue used = input.energyKwh - *meterStart;
        QSqlQuery apply(database_);
        apply.prepare(QStringLiteral(
            "UPDATE charge_order SET energy_kwh=?, meter_last_at=? "
            "WHERE id=? AND status='CHARGING' AND energy_kwh<=?"));
        apply.addBindValue(used.toString());
        apply.addBindValue(recordedAt.toString(Qt::ISODate));
        apply.addBindValue(activeOrderId);
        apply.addBindValue(used.toString());
        if (!apply.exec() || apply.numRowsAffected() != 1) {
            database_.rollback();
            return failure(QStringLiteral("DATABASE_ERROR"));
        }
        orderEnergy = used;  // 挂接成功的遥测必须回报订单号与用量（TelemetryResult 契约）。
    }

    // 过热结算：本条增量已计入 → 按锁价冻结账单，转 PENDING_PAYMENT；桩 FAULT 已置；
    // 告警/工单联动（唯一索引防重复活动告警）。
    if (overheated) {
        if (chargingOrder
            && !settleChargingOrderByMeter(database_, activeOrderId, now,
                                           QStringLiteral("OVERHEAT"), &error)) {
            database_.rollback();
            return failure(QStringLiteral("DATABASE_ERROR"));
        }
        if (!alarms.ensureActiveAlarmAndTicket(
                input.pileId, activeOrderId, QStringLiteral("CRITICAL"),
                QStringLiteral("OVERHEAT"), QStringLiteral("充电桩温度达到高温阈值"),
                recordedAt, &error)) {
            database_.rollback();
            return failure(QStringLiteral("DATABASE_ERROR"));
        }
    }

    if (!commitOrRollback(database_)) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    // 2026-09-13（第二阶段 T08 修复）：TelemetryResult.orderId/orderEnergyKwh 自 M1-03
    // 起在结构体与路由响应中存在，但 record() 从未填充，单元/协议测试与三端联动均无法
    // 对上订单号。此处按测试契约补齐：挂接到 CHARGING 订单的遥测回报订单号与累计用量。
    TelemetryResult recorded;
    recorded.ok = true;
    recorded.errorCode = QStringLiteral("OK");
    if (chargingOrder) {
        recorded.orderId = activeOrderId;
        if (orderEnergy) {
            recorded.orderEnergyKwh = *orderEnergy;
        }
    }
    return recorded;
}

// 冻结结算（内部复用）：CHARGING → PENDING_PAYMENT，金额 = 锁价 × 当前用量。
// 调用方事务内执行；使用订单现有 energy_kwh（已由计量路径更新到最新）。
bool TelemetryService::settleChargingOrderByMeter(QSqlDatabase database,
                                                   const QString &orderId,
                                                   const QDateTime &endedAt,
                                                   const QString &reason, QString *error)
{
    QSqlQuery order(database);
    order.prepare(QStringLiteral(
        "SELECT energy_kwh, unit_price_yuan_per_kwh, price_locked, pile_id "
        "FROM charge_order WHERE id=? AND status='CHARGING'"));
    order.addBindValue(orderId);
    if (!order.exec() || !order.next()) {
        if (error) *error = QStringLiteral("settle: order not CHARGING");
        return false;
    }
    const QString energy = order.value(0).toString();
    std::optional<DecimalValue> unitPrice;
    if (order.value(2).toInt() == 1) {
        unitPrice = DecimalValue::parseCanonical(order.value(1).toString());
    } else {
        QSqlQuery fallback(database);
        fallback.prepare(QStringLiteral(
            "SELECT price_yuan_per_kwh FROM station_info s JOIN pile_info p "
            "ON p.station_id=s.id WHERE p.id=?"));
        fallback.addBindValue(order.value(3).toLongLong());
        if (!fallback.exec() || !fallback.next()) {
            if (error) *error = QStringLiteral("settle: price lookup failed");
            return false;
        }
        unitPrice = DecimalValue::parseCanonical(fallback.value(0).toString());
    }
    if (!unitPrice) {
        if (error) *error = QStringLiteral("settle: bad unit price");
        return false;
    }
    const auto energyValue = DecimalValue::parseCanonical(energy);
    if (!energyValue) {
        if (error) *error = QStringLiteral("settle: bad energy");
        return false;
    }
    // Decimal 乘法半向上两位（与既有 multiplyRounded 规则一致：10.275→10.28）。
    const DecimalValue amount = multiplyRounded(*energyValue, *unitPrice);
    QSqlQuery update(database);
    update.prepare(QStringLiteral(
        "UPDATE charge_order SET status='PENDING_PAYMENT', amount_yuan=?, "
        "ended_at=?, end_reason=?, unit_price_yuan_per_kwh=? "
        "WHERE id=? AND status='CHARGING'"));
    update.addBindValue(amount.toString());
    update.addBindValue(endedAt.toUTC().toString(Qt::ISODate));
    update.addBindValue(reason);
    update.addBindValue(unitPrice->toString());
    update.addBindValue(orderId);
    if (!update.exec() || update.numRowsAffected() != 1) {
        if (error) *error = QStringLiteral("settle: update failed");
        return false;
    }
    return true;
}

// 心跳离线扫描（后台 5 秒定时器调用）：
// 心跳超过 15 秒未更新的桩 → OFFLINE + 离线告警/工单；其活动 CHARGING 订单按
// 最后有效读数冻结（energy_kwh 已是最新应用值，直接锁价结算），不估算失联电量。
int TelemetryService::scanOffline()
{
    if (!database_.isOpen() || !beginImmediate(database_)) {
        return 0;
    }
    const QDateTime now = clock_().toUTC();
    TelemetryRepository telemetry(database_);
    AlarmRepository alarms(database_);
    QString error;
    const auto piles = telemetry.stalePiles(now.addSecs(-15), &error);
    if (!error.isEmpty()) {
        database_.rollback();
        return 0;
    }
    int count = 0;
    for (const auto &pile : piles) {
        // 管理禁用的桩保持覆盖状态（OFFLINE 覆盖与扫描结果一致，但不清除覆盖标记）。
        const QString targetStatus = !pile.adminOverride.isEmpty()
            ? pile.adminOverride : QStringLiteral("OFFLINE");
        if (!telemetry.changeStatus(pile.id, pile.status, targetStatus, &error)) {
            database_.rollback();
            return 0;
        }
        if (pile.activeOrderStatus == QStringLiteral("CHARGING")) {
            if (!settleChargingOrderByMeter(database_, pile.activeOrderId, now,
                                            QStringLiteral("OFFLINE"), &error)) {
                database_.rollback();
                return 0;
            }
        }
        if (!alarms.ensureActiveAlarmAndTicket(
                pile.id, pile.activeOrderId, QStringLiteral("WARNING"),
                QStringLiteral("OFFLINE"), QStringLiteral("充电桩心跳超时"), now,
                &error)) {
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
