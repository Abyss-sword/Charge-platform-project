#pragma once

#include "shared/domain/decimal_value.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include <optional>

// 遥测所需电桩快照：最新遥测时间/电量 + 心跳 + 活动订单（恢复状态推导用）。
struct TelemetryPileRecord
{
    qint64 id = 0;
    QString status;
    QDateTime lastHeartbeatAt;
    QDateTime latestRecordedAt;
    std::optional<DecimalValue> latestEnergyKwh;
    QString activeOrderId;
    QString activeOrderStatus;
    // M1-03：管理禁用覆盖（'FAULT'/'OFFLINE'，空串表示无覆盖）。
    QString adminOverride;
};

// TelemetryRepository：pile_telemetry 明细、心跳与离线扫描的数据访问。
class TelemetryRepository
{
public:
    explicit TelemetryRepository(QSqlDatabase database);

    // 电桩全景：状态、心跳、最近遥测（时间/累计电量）、最新活动订单（子查询）。
    std::optional<TelemetryPileRecord> findPile(qint64 pileId,
                                                 QString *error = nullptr) const;
    // 心跳早于 cutoff 且非 OFFLINE 的桩（last_heartbeat_at 为 NULL 的不参与——未上报≠在线）。
    QVector<TelemetryPileRecord> stalePiles(const QDateTime &cutoff,
                                             QString *error = nullptr) const;
    // 写一条遥测明细；刷新心跳与状态；条件流转桩状态。
    bool insertTelemetry(qint64 pileId, const QDateTime &recordedAt,
                         double temperatureC, double voltageV, double currentA,
                         int powerW, const DecimalValue &energyKwh,
                         QString *error = nullptr);
    bool updateHeartbeatAndStatus(qint64 pileId, const QDateTime &recordedAt,
                                  const QString &status, QString *error = nullptr);
    bool changeStatus(qint64 pileId, const QString &expectedStatus,
                      const QString &newStatus, QString *error = nullptr);

private:
    QSqlDatabase database_;
};
