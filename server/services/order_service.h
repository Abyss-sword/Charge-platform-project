#pragma once

#include "shared/domain/decimal_value.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QList>

#include <functional>

// 订单业务结果：energyKwh/unitPriceYuanPerKwh/amountYuan 仅在停止/取消等场景填充。
struct OrderResult
{
    bool ok = false;
    QString errorCode;
    QString orderId;
    DecimalValue energyKwh;
    DecimalValue unitPriceYuanPerKwh;
    DecimalValue amountYuan;
};

struct OrderQueryResult
{
    bool ok = false;
    QString errorCode;
    QVariantMap order;
};

struct OrderListResult
{
    bool ok = false;
    QString errorCode;
    QList<QVariantMap> rows;
};

// OrderService：订单业务核心（预约/开始/取消/过期回收/停止结算/查询）。
// 并发与一致性手段：所有写操作先 BEGIN IMMEDIATE 拿写资格，再按“期望旧状态”条件更新，
// 最后由部分唯一索引 ux_charge_order_active_pile 兜底（同一电桩不允许两条活动订单）。
// Clock 可注入，测试用它构造过去/未来的时间，无需真实等待。
class OrderService
{
public:
    // 时钟抽象：默认 UTC 当前时间；测试注入假时钟验证 15 分钟期限。
    using Clock = std::function<QDateTime()>;

    explicit OrderService(QSqlDatabase database,
                          Clock clock = [] { return QDateTime::currentDateTimeUtc(); });

    // 预约：冻结用户/未完成订单/电桩占用/桩可用性四重检查 + 15 分钟期限 + 三写一事务。
    OrderResult reserve(qint64 userId, qint64 pileId, const QString &requestId);
    // 回收所有超时（reserved_until 已过）的 RESERVED 订单并把桩释放为 IDLE，返回回收条数。
    int expireReservations();
    // 取消（仅 RESERVED）；订单置 CANCELED 且桩 RESERVED→IDLE。
    // requireUserId>0 时校验订单归属（他人订单 → AUTH_INVALID，不泄露状态细节）。
    OrderResult cancel(const QString &orderId, qint64 requireUserId = 0);
    // 开始充电（RESERVED→CHARGING）；若预约已超期，先落 EXPIRED 并释放桩再拒绝。
    OrderResult start(const QString &orderId, qint64 requireUserId = 0);
    // 订单电量上报：电量不允许倒退，按旧值条件更新（CAS）。
    OrderResult recordTelemetry(const QString &orderId, const DecimalValue &energyKwh);
    // 停止结算：金额 = 电量 × 单价（DecimalValue 两位量化）；单价优先取开始充电时
    // 锁定的单价快照（priceLocked），升级前的旧订单无快照则回退为停止时站点单价。
    OrderResult stop(const QString &orderId, qint64 requireUserId = 0);
    // 详情 / 活动订单 / 历史分页（LIMIT 上限 100）。
    // detail 同样支持归属校验：requireUserId>0 时他人订单按 AUTH_INVALID 拒绝。
    OrderQueryResult detail(const QString &orderId, qint64 requireUserId = 0) const;
    OrderQueryResult active(qint64 userId) const;
    OrderListResult history(qint64 userId, int limit, int offset) const;

private:
    QSqlDatabase database_;
    Clock clock_;
};

// 预约过期调度器：内部 QTimer 定期调用 OrderService::expireReservations()。
// 注意：类仍存在，但其默认 10 秒间隔不是当前生产入口的调度间隔——
// 实际生产入口是 ServerBackend 中 5 秒后台定时器统一执行离线扫描与过期回收。
class ReservationExpiryScheduler
{
public:
    explicit ReservationExpiryScheduler(OrderService &service, int intervalMs = 10000);
    int intervalMs() const;

private:
    OrderService &service_;
    QTimer timer_;
};
