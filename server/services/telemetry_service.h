#pragma once

#include "shared/domain/decimal_value.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QString>

#include <functional>
#include <optional>

// 一次设备遥测上报的全部数据：累计电量 energyKwh 为两位小数十进制字符串解析结果。
struct TelemetryInput
{
    qint64 pileId = 0;
    QDateTime recordedAt;
    double temperatureC = 0;
    double voltageV = 0;
    double currentA = 0;
    int powerW = 0;
    DecimalValue energyKwh;
    QString sourceMode = QStringLiteral("UNKNOWN");
};

struct TelemetryResult
{
    bool ok = false;
    QString errorCode;
    bool duplicate = false;
    QString orderId;
    DecimalValue orderEnergyKwh;
};

// TelemetryService：统一设备计量与运维状态（M1-03，01文档 4.2/4.3）。
// record()：唯一计量入口——校验物理量/时序/电量单调性后，在同一事务内：
//   写遥测明细 + 刷新心跳 + 更新活动订单用量（累计读数-开始基准）+ 桩状态流转 +
//   过热/离线自动结算（按最后有效读数冻结账单，转 PENDING_PAYMENT）+ 告警/工单联动。
//   完全重复读数（同时间同值）不重复计量不重复写明细；管理禁用（admin_status_override）
//   不被正常遥测清除；表计归零拒绝并要求维护。
// scanOffline()：心跳超时桩置 OFFLINE，活动充电订单按最后有效读数冻结，不估算失联电量。
class TelemetryService
{
public:
    // 时钟与过热阈值可注入（测试用），默认 UTC 当前时间、70℃。
    using Clock = std::function<QDateTime()>;

    explicit TelemetryService(QSqlDatabase database,
                              Clock clock = [] { return QDateTime::currentDateTimeUtc(); },
                              double overheatThresholdC = 70.0);

    // 记录一次遥测上报（校验、计量、状态流转、异常结算、告警联动，整体一个事务）。
    TelemetryResult record(const TelemetryInput &input);
    // 心跳离线扫描：把 last_heartbeat_at 早于 now-15s 且非 OFFLINE 的桩置 OFFLINE，
    // 其活动 CHARGING 订单按最后有效读数冻结为 PENDING_PAYMENT。
    int scanOffline();

    // 内部冻结结算：按订单最后应用的读数把 CHARGING 订单转 PENDING_PAYMENT。
    // 供正常停止（OrderService::stop 复用）与故障停止共用；调用方必须已持有
    // 同一数据库的开启事务（本方法不再 BEGIN/COMMIT）。
    static bool settleChargingOrderByMeter(QSqlDatabase database, const QString &orderId,
                                           const QDateTime &endedAt, const QString &reason,
                                           QString *error);

private:
    QSqlDatabase database_;
    Clock clock_;
    double overheatThresholdC_ = 70.0;
};
