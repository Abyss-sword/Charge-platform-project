#pragma once

#include "shared/domain/decimal_value.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// 通用管理操作结果（stationId 仅在创建/修改站点时填充）。
struct AdminResult
{
    bool ok = false;
    QString errorCode;
    qint64 stationId = 0;
};

// 管理员登录结果：token 为 32 字节随机数的十六进制（8 小时有效），
// 数据库只存其 SHA-256 哈希——库被拖走也无法反推出可用 Token。
struct AdminLoginResult
{
    bool ok = false;
    QString errorCode;
    QString token;
};

// 新建站点时随站点一并创建的电桩定义。
struct NewPile
{
    QString serial;
    QString chargeType;
    double ratedPowerKw = 0;
};

// 运营总览：今日/本月营收（DecimalValue）、今日服务次数与订单数、电桩利用率/在线率、
// 待处理告警数，以及 S1-B 总览页所需的分布计数、24h 负荷序列与最新清单（各 5 条）。
// 注意口径：“今日/本月”按 UTC 日期统计，不能直接当作北京时间自然日。
struct AdminOverview
{
    DecimalValue todayRevenueYuan;
    DecimalValue monthRevenueYuan;
    int todayServiceCount = 0;
    int todayOrderCount = 0;
    double utilizationRate = 0.0;
    double onlineRate = 0.0;
    int pendingAlarmCount = 0;
    QVariantMap pileStatusCounts;   // 状态 → 数量（IDLE/RESERVED/CHARGING/FAULT/OFFLINE）
    QVariantMap orderStatusCounts;  // 状态 → 数量（RESERVED/CHARGING/PENDING_PAYMENT/FINISHED/CANCELED/EXPIRED）
    QVariantList loadSeries24h;     // [{hourUtc:"YYYY-MM-DDTHH", kwh:"x.xx"}] 共 24 个整点桶
    QVariantList latestAlarms;      // 最新 5 条（任意状态，按时间倒序）
    QVariantList openTickets;       // 未关闭工单 5 条（OPEN/PROCESSING 优先）
    QVariantList abnormalPiles;     // FAULT/OFFLINE 桩 5 条
    QVariantList recentOrders;      // 最近 5 笔订单
};

// 营收趋势单点：UTC 日期 + 当日营收（DecimalValue 累加，充值不计入）。
struct RevenuePoint
{
    QString date;
    DecimalValue revenueYuan;
};

// S1-B 营收页汇总：区间（UTC 日期闭区间）内已支付订单的指标卡、站点排行与 24 小时分布。
// 口径：只统计 FINISHED 订单；金额电量两位小数；平均单价 = 总营收 / 总电量（无电量时为 "0.00"）。
struct AdminRevenueSummary
{
    DecimalValue totalRevenueYuan;
    int orderCount = 0;
    DecimalValue totalEnergyKwh;
    DecimalValue avgOrderYuan;      // 平均客单价 = 总营收 / 订单数
    DecimalValue avgPriceYuan;      // 平均单价 = 总营收 / 总电量
    QVariantList stationRanking;    // [{stationId, stationName, revenueYuan, orderCount}] 按营收降序
    QVariantList hourHistogram;     // 24 桶 [{hourUtc:"HH", revenueYuan}]（UTC 小时）
};

// 电桩模拟重启结果：commandId 形如 CMD-<uuid>，status 为重启后实际恢复的状态。
struct PileRestartResult
{
    bool ok = false;
    QString errorCode;
    QString commandId;
    QString status;
};

// AdminService：运营管理业务（认证、查询、修改、运维）。
// 安全机制（答辩高频）：密码 PBKDF2-HMAC-SHA256（16 字节随机盐、10 万次迭代）派生存储，
// 验证用常时比较防时序侧信道；会话 Token 32 字节随机、库存 SHA-256 哈希、8 小时过期。
// 写操作（冻结/建站/改站/桩状态/重启）全部与 operation_log 审计记录同事务提交。
// 注意口径：这是“密码派生/哈希验证”，不是“可解密密码”；重启是服务端模拟，未连真实硬件。
class AdminService
{
public:
    explicit AdminService(QSqlDatabase database);

    // 启动时确保演示管理员存在（不存在则用随机盐 PBKDF2 派生入库；仅教学环境用）。
    AdminResult ensureDefaultAdmin();
    // 登录校验并颁发会话 Token（顺带清理过期会话）。
    AdminLoginResult login(const QString &username, const QString &password);
    // Token → 管理员用户名（校验哈希与有效期），失败返回空串；管理动作的身份闸门。
    QString authenticatedAdmin(const QString &token) const;
    // —— 查询组 ——营收累计 / 运营总览 / 营收趋势 / 站点·桩·订单·用户·日志·告警·工单列表。
    DecimalValue totalRevenueYuan() const;
    AdminOverview overview() const;
    QVector<RevenuePoint> revenueTrend(int days, qint64 stationId = 0) const;
    // S1-B：区间营收汇总（fromDate/toDate 为 "YYYY-MM-DD" UTC 日期，闭区间）。
    AdminRevenueSummary revenueSummary(const QString &fromDate, const QString &toDate) const;
    QList<QVariantMap> stations(const QString &search = {}, int limit = 100,
                                int offset = 0) const;
    QList<QVariantMap> piles(qint64 stationId = 0, const QString &status = {},
                             int limit = 100, int offset = 0) const;
    QList<QVariantMap> orders(const QString &status = {}, int limit = 100,
                              int offset = 0) const;
    QList<QVariantMap> users(const QString &status = {}, const QString &search = {},
                             int limit = 100, int offset = 0) const;
    QList<QVariantMap> operationLogs(const QString &action = {}, int limit = 100,
                                     int offset = 0) const;
    QList<QVariantMap> alarms(const QString &status = {}, const QString &severity = {},
                              int limit = 100, int offset = 0) const;
    QList<QVariantMap> tickets(const QString &status = {}, int limit = 100,
                               int offset = 0) const;
    // —— 修改组 ——以下写操作均要求 actorId（认证用户名）并写审计日志。
    // 用户冻结/解冻（冻结用户可登录但业务受限）。
    AdminResult setUserFrozen(qint64 userId, bool frozen, const QString &reason,
                              const QString &actorId);
    // 新建站点（含初始电桩列表；单价 ≤1000.00、坐标合法、桩类型 FAST/SLOW）。
    AdminResult createStation(const QString &name, const QString &address,
                              double latitude, double longitude,
                              const DecimalValue &priceYuanPerKwh,
                              const QList<NewPile> &piles);
    AdminResult createStation(const QString &name, const QString &address,
                              double latitude, double longitude,
                              const DecimalValue &priceYuanPerKwh,
                              const QList<NewPile> &piles, const QString &actorId);
    // 修改站点信息与营业状态（OPEN/CLOSED/MAINTENANCE）。
    AdminResult updateStation(qint64 stationId, const QString &name,
                              const QString &address, double latitude, double longitude,
                              const DecimalValue &priceYuanPerKwh, const QString &status,
                              const QString &actorId);
    // 人工设置桩状态：仅 IDLE/FAULT/OFFLINE；预约/充电中的桩拒绝（STATE_CONFLICT）。
    AdminResult setPileStatus(qint64 pileId, const QString &status,
                              const QString &reason, const QString &actorId);
    // 模拟重启：仅 FAULT/OFFLINE 可重启；按最新活动订单恢复 RESERVED/CHARGING/IDLE。
    PileRestartResult restartPile(qint64 pileId, const QString &actorId);

private:
    QSqlDatabase database_;
};
