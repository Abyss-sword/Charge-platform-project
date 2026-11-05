#pragma once

#include "shared/domain/decimal_value.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include <optional>

// 订单记录：priceLocked 表示开始充电时已锁定单价快照（结算优先用它）。
struct OrderRecord
{
    QString id;
    qint64 userId = 0;
    qint64 pileId = 0;
    QString status;
    DecimalValue energyKwh;
    QDateTime reservedUntil;
    DecimalValue unitPriceYuanPerKwh;
    bool priceLocked = false;
};

struct ExpiredReservation
{
    QString orderId;
    qint64 pileId = 0;
};

// 防重回放所需的最小信息：原请求的用户/桩/订单与动作名。
struct ReservationReplay
{
    qint64 userId = 0;
    qint64 pileId = 0;
    QString orderId;
    QString action;
};

// OrderRepository：charge_order / pile_info / request_dedup 的数据访问层。
// 核心模式（答辩高频）：所有状态写入都是“条件更新”——UPDATE ... WHERE 状态=期望旧值，
// 再检查受影响行数必须为 1（execChanged），防止并发下覆盖别人已改的状态；
// 防重记录写入 request_dedup；活动订单的唯一性由部分唯一索引 ux_charge_order_active_pile 兜底。
class OrderRepository
{
public:
    explicit OrderRepository(QSqlDatabase database);

    // —— 查询 ——用户/桩是否有未完成（活动）订单；用户/桩状态（桩状态联动站点营业判断）。
    bool hasUnfinishedOrder(qint64 userId, QString *error = nullptr) const;
    bool hasActiveOrderForPile(qint64 pileId, QString *error = nullptr) const;
    std::optional<QString> userStatus(qint64 userId, QString *error = nullptr) const;
    std::optional<QString> pileStatus(qint64 pileId, QString *error = nullptr) const;
    // 订单详情 / 桩所属站点单价（计费来源）/ 超时预约列表 / 防重回放查询。
    std::optional<OrderRecord> findOrder(const QString &orderId,
                                         QString *error = nullptr) const;
    std::optional<DecimalValue> stationPriceForPile(qint64 pileId,
                                                    QString *error = nullptr) const;
    QVector<ExpiredReservation> expiredReservations(const QDateTime &now,
                                                     QString *error = nullptr) const;
    std::optional<ReservationReplay> reservationByRequest(const QString &requestId,
                                                           QString *error = nullptr) const;

    // —— 写入（全部条件更新，受影响行数=1 才算成功）——
    // 新建 RESERVED 订单；桩状态按旧值流转；开始（锁价快照+price_locked=1）；
    // 过期/取消（记录 end_reason）；电量 CAS 更新；停止结算；写预约防重记录。
    bool createReservation(const QString &orderId, qint64 userId, qint64 pileId,
                           const QDateTime &reservedUntil, const QDateTime &createdAt,
                           QString *error = nullptr);
    bool changePileStatus(qint64 pileId, const QString &from, const QString &to,
                          QString *error = nullptr);
    // M1-03：开始时同时记录表计基准（最新累计读数）与最近应用读数时间。
    bool startOrder(const QString &orderId, const QDateTime &startedAt,
                    const DecimalValue &unitPriceYuanPerKwh,
                    const QString &meterStartKwh, QString *error = nullptr);
    bool expireOrder(const QString &orderId, const QDateTime &endedAt,
                     QString *error = nullptr);
    bool cancelOrder(const QString &orderId, const QDateTime &endedAt,
                     QString *error = nullptr);
    bool updateEnergy(const QString &orderId, const DecimalValue &previousEnergyKwh,
                      const DecimalValue &energyKwh, QString *error = nullptr);
    bool stopOrder(const QString &orderId, const DecimalValue &unitPriceYuanPerKwh,
                   const DecimalValue &amountYuan,
                   const QDateTime &endedAt, QString *error = nullptr);
    bool recordReservationRequest(const QString &requestId, qint64 userId, qint64 pileId,
                                  const QString &orderId, QString *error = nullptr);

private:
    QSqlDatabase database_;
};
