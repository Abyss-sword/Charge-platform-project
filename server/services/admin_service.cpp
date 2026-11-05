#include "server/services/admin_service.h"

#include "server/services/linkage.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <utility>

// AdminService 实现：认证安全原语 + 管理查询 + 带审计的事务性写操作。
// 所有写操作模式统一：参数校验 → 开事务 → 业务写入 → addAuditLog → 提交（失败整体回滚）。
namespace {
// 统一失败结果构造。
AdminResult failure(const QString &code)
{
    return {false, code, 0};
}

// HMAC-SHA256（标准 RFC 2104 流程）：密钥超长先哈希、补零对齐 64 字节块，
// 内层/外层异或 ipad(0x36)/opad(0x5c) 后两次 SHA-256。
QByteArray hmacSha256(const QByteArray &key, const QByteArray &message)
{
    constexpr int BlockSize = 64;
    QByteArray normalized = key;
    if (normalized.size() > BlockSize) {
        normalized = QCryptographicHash::hash(normalized, QCryptographicHash::Sha256);
    }
    normalized = normalized.leftJustified(BlockSize, '\0');
    QByteArray outer(BlockSize, '\x5c');
    QByteArray inner(BlockSize, '\x36');
    for (int i = 0; i < BlockSize; ++i) {
        outer[i] = static_cast<char>(outer[i] ^ normalized[i]);
        inner[i] = static_cast<char>(inner[i] ^ normalized[i]);
    }
    const QByteArray innerHash = QCryptographicHash::hash(inner + message,
                                                          QCryptographicHash::Sha256);
    return QCryptographicHash::hash(outer + innerHash, QCryptographicHash::Sha256);
}

// PBKDF2-HMAC-SHA256 单块派生（答辩高频）：
// U1 = HMAC(salt‖0x00000001)，Ui = HMAC(U_{i-1})，结果 = U1 ⊕ U2 ⊕ … ⊕ U100000。
// 迭代 10 万次显著提高暴力破解成本；登录路径每次验证都会真实执行这 10 万次 HMAC。
QByteArray pbkdf2Sha256(const QByteArray &password, const QByteArray &salt)
{
    QByteArray block = salt;
    block.append("\0\0\0\1", 4);
    QByteArray value = hmacSha256(password, block);
    QByteArray result = value;
    for (int iteration = 1; iteration < 100000; ++iteration) {
        value = hmacSha256(password, value);
        for (int i = 0; i < result.size(); ++i) {
            result[i] = static_cast<char>(result[i] ^ value[i]);
        }
    }
    return result;
}

// 密码学安全随机字节（QRandomGenerator::system()），用于盐与 Token。
QByteArray randomBytes(int size)
{
    QByteArray salt(size, Qt::Uninitialized);
    auto *generator = QRandomGenerator::system();
    for (int offset = 0; offset < salt.size(); offset += 4) {
        const quint32 value = generator->generate();
        const int count = qMin(4, salt.size() - offset);
        for (int i = 0; i < count; ++i) {
            salt[offset + i] = static_cast<char>((value >> (i * 8)) & 0xff);
        }
    }
    return salt;
}

// 常时比较：逐字节异或累积差异，不因找到首个不同就提前返回（防时序侧信道）；
// 仅长度不同可提前判定（长度本身不是秘密）。
bool secureEquals(const QByteArray &left, const QByteArray &right)
{
    if (left.size() != right.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (int i = 0; i < left.size(); ++i) {
        difference |= static_cast<unsigned char>(left[i] ^ right[i]);
    }
    return difference == 0;
}

// 分页尺寸约束（1~500）。
int pageSize(int requested)
{
    return qBound(1, requested, 500);
}

// 手机号打码（管理端列表同样不输出完整号码）。
QString maskedPhone(const QString &phone)
{
    return phone.size() == 11
        ? phone.left(3) + QStringLiteral("****") + phone.right(4)
        : QStringLiteral("****");
}

// 审计日志：写入 operation_log（操作者/动作/目标/结果 JSON），由调用方事务统一提交。
bool addAuditLog(QSqlDatabase database, const QString &actorId, const QString &action,
                 const QString &targetType, qint64 targetId, const QJsonObject &details)
{
    QSqlQuery log(database);
    log.prepare(QStringLiteral(
        "INSERT INTO operation_log(actor_type,actor_id,action,target_type,target_id,result) "
        "VALUES('ADMIN',?,?,?,?,?)"));
    log.addBindValue(actorId);
    log.addBindValue(action);
    log.addBindValue(targetType);
    log.addBindValue(QString::number(targetId));
    log.addBindValue(QString::fromUtf8(QJsonDocument(details).toJson(QJsonDocument::Compact)));
    return log.exec();
}
}

AdminService::AdminService(QSqlDatabase database)
    : database_(std::move(database))
{
}

// 启动时初始化演示管理员：不存在才创建——16 字节随机盐 + PBKDF2 派生“123456”入库。
// 仅教学环境使用（admin/123456），不构成生产安全体系。
AdminResult AdminService::ensureDefaultAdmin()
{
    if (!database_.isOpen()) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    QSqlQuery existing(database_);
    existing.prepare(QStringLiteral("SELECT 1 FROM admin_user WHERE username='admin'"));
    if (!existing.exec()) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (existing.next()) {
        return {true, QStringLiteral("OK"), 0};
    }

    const QByteArray salt = randomBytes(16);
    const QByteArray hash = pbkdf2Sha256(QByteArrayLiteral("123456"), salt);
    QSqlQuery insert(database_);
    insert.prepare(QStringLiteral(
        "INSERT INTO admin_user(username,password_hash,password_salt) VALUES('admin',?,?)"));
    insert.addBindValue(QString::fromLatin1(hash.toHex()));
    insert.addBindValue(QString::fromLatin1(salt.toHex()));
    if (!insert.exec()) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    return {true, QStringLiteral("OK"), 0};
}

// 管理员登录（答辩高频）：
// 1) 按用户名取派生值与盐，用同盐重算 PBKDF2 后常时比较（防时序）；
// 2) 生成 32 字节随机 Token 明文返回客户端，库里只存 SHA-256(Token)；
// 3) 顺带清理过期会话，新会话 8 小时有效。
AdminLoginResult AdminService::login(const QString &username, const QString &password)
{
    if (!database_.isOpen() || username.trimmed().isEmpty() || password.isEmpty()) {
        return {false, QStringLiteral("PARAM_INVALID"), {}};
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id,password_hash,password_salt FROM admin_user "
        "WHERE username=? AND status='ACTIVE'"));
    query.addBindValue(username);
    if (!query.exec()) {
        return {false, QStringLiteral("DATABASE_ERROR"), {}};
    }
    if (!query.next()) {
        return {false, QStringLiteral("AUTH_FAILED"), {}};
    }
    const qint64 adminId = query.value(0).toLongLong();
    const QByteArray expected = QByteArray::fromHex(query.value(1).toByteArray());
    const QByteArray salt = QByteArray::fromHex(query.value(2).toByteArray());
    const QByteArray actual = pbkdf2Sha256(password.toUtf8(), salt);
    if (!secureEquals(actual, expected)) {
        return {false, QStringLiteral("AUTH_FAILED"), {}};
    }

    const QString token = QString::fromLatin1(randomBytes(32).toHex());
    const QString tokenHash = QString::fromLatin1(
        QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256).toHex());
    QSqlQuery cleanup(database_);
    cleanup.prepare(QStringLiteral("DELETE FROM admin_session WHERE expires_at<=?"));
    cleanup.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!cleanup.exec()) {
        return {false, QStringLiteral("DATABASE_ERROR"), {}};
    }
    QSqlQuery insert(database_);
    insert.prepare(QStringLiteral(
        "INSERT INTO admin_session(token_hash,admin_id,expires_at) VALUES(?,?,?)"));
    insert.addBindValue(tokenHash);
    insert.addBindValue(adminId);
    insert.addBindValue(QDateTime::currentDateTimeUtc().addSecs(8 * 60 * 60)
                            .toString(Qt::ISODate));
    if (!insert.exec()) {
        return {false, QStringLiteral("DATABASE_ERROR"), {}};
    }
    return {true, QStringLiteral("OK"), token};
}

// 会话校验（所有管理动作的闸门）：SHA-256(请求 Token) 能在 admin_session 中命中
// 且未过期、管理员状态 ACTIVE，返回用户名；否则空串 → 路由层返回 AUTH_INVALID。
QString AdminService::authenticatedAdmin(const QString &token) const
{
    if (token.isEmpty() || !database_.isOpen()) {
        return {};
    }
    const QString tokenHash = QString::fromLatin1(
        QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256).toHex());
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT a.username FROM admin_session s "
        "JOIN admin_user a ON a.id=s.admin_id "
        "WHERE s.token_hash=? AND s.expires_at>? AND a.status='ACTIVE'"));
    query.addBindValue(tokenHash);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return query.exec() && query.next() ? query.value(0).toString() : QString{};
}

// 累计营收：已完成（FINISHED）订单金额逐条 DecimalValue 累加（十进制精确）；
// 充值流水不算营收；库中金额文本非法时直接整体失败，不做静默截断。
DecimalValue AdminService::totalRevenueYuan() const
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT amount_yuan FROM charge_order WHERE status='FINISHED'"))) {
        return {};
    }
    DecimalValue total;
    while (query.next()) {
        const auto amount = DecimalValue::parseCanonical(query.value(0).toString());
        if (!amount) {
            return {};
        }
        total = total + *amount;
    }
    return total;
}

// 运营总览：今日营收/服务次数（按 UTC 日期口径）、桩利用率（RESERVED/CHARGING 占比）、
// 在线率（非 OFFLINE 占比）、待处理告警数（OPEN/ACKNOWLEDGED）。
AdminOverview AdminService::overview() const
{
    AdminOverview result;
    QSqlQuery query(database_);
    const QString today = QDateTime::currentDateTimeUtc().date().toString(Qt::ISODate);
    query.prepare(QStringLiteral(
        "SELECT amount_yuan FROM charge_order "
        "WHERE status='FINISHED' AND substr(COALESCE(ended_at,created_at),1,10)=?"));
    query.addBindValue(today);
    if (query.exec()) {
        while (query.next()) {
            const auto amount = DecimalValue::parseCanonical(query.value(0).toString());
            if (!amount) {
                result.todayRevenueYuan = {};
                result.todayServiceCount = 0;
                break;
            }
            result.todayRevenueYuan = result.todayRevenueYuan + *amount;
            ++result.todayServiceCount;
        }
    }
    if (query.exec(QStringLiteral(
            "SELECT COUNT(*),SUM(CASE WHEN status IN ('CHARGING','RESERVED') THEN 1 ELSE 0 END),"
            "SUM(CASE WHEN status!='OFFLINE' THEN 1 ELSE 0 END) FROM pile_info"))
        && query.next()) {
        const int total = query.value(0).toInt();
        if (total > 0) {
            result.utilizationRate = query.value(1).toDouble() / total;
            result.onlineRate = query.value(2).toDouble() / total;
        }
    }
    if (query.exec(QStringLiteral(
            "SELECT COUNT(*) FROM alarm_event WHERE status IN ('OPEN','ACKNOWLEDGED')"))
        && query.next()) {
        result.pendingAlarmCount = query.value(0).toInt();
    }

    // —— S1-B 扩展：总览页指标条、分布计数、24h 负荷序列与最新清单 ——
    // 本月营收：已支付口径，UTC 月前缀（两位小数累加）。
    query.prepare(QStringLiteral(
        "SELECT amount_yuan FROM charge_order "
        "WHERE status='FINISHED' AND substr(COALESCE(ended_at,created_at),1,7)=?"));
    query.addBindValue(today.left(7));
    if (query.exec()) {
        while (query.next()) {
            const auto amount = DecimalValue::parseCanonical(query.value(0).toString());
            if (amount) {
                result.monthRevenueYuan = result.monthRevenueYuan + *amount;
            }
        }
    }
    // 今日订单总数（含进行中/取消，口径=created_at 的 UTC 日期）。
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM charge_order WHERE substr(created_at,1,10)=?"));
    query.addBindValue(today);
    if (query.exec() && query.next()) {
        result.todayOrderCount = query.value(0).toInt();
    }
    // 电桩/订单状态分布计数（环图与柱图数据源）。
    if (query.exec(QStringLiteral(
            "SELECT status, COUNT(*) FROM pile_info GROUP BY status"))) {
        while (query.next()) {
            result.pileStatusCounts.insert(query.value(0).toString(), query.value(1).toInt());
        }
    }
    if (query.exec(QStringLiteral(
            "SELECT status, COUNT(*) FROM charge_order GROUP BY status"))) {
        while (query.next()) {
            result.orderStatusCounts.insert(query.value(0).toString(), query.value(1).toInt());
        }
    }
    // 近 24 小时负荷：按桩分段计算累计电表增量（倒退与重复读数不计入），整点桶聚合。
    {
        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        const QDateTime windowStart = nowUtc.addSecs(-23 * 3600);
        const QString startText = QDateTime(windowStart.date(), QTime(windowStart.time().hour(), 0),
                                            Qt::UTC).toString(Qt::ISODate);
        QSqlQuery telemetryQuery(database_);
        telemetryQuery.prepare(QStringLiteral(
            "SELECT pile_id, recorded_at, energy_kwh FROM pile_telemetry "
            "WHERE recorded_at >= ? ORDER BY pile_id, recorded_at, id"));
        telemetryQuery.addBindValue(startText);
        QHash<QString, DecimalValue> hourlyKwh;   // 整点桶（UTC "YYYY-MM-DDTHH"）→ 增量累计
        QHash<qint64, DecimalValue> lastMeter;    // 每桩上一条累计读数
        if (telemetryQuery.exec()) {
            while (telemetryQuery.next()) {
                const auto meter = DecimalValue::parseCanonical(
                    telemetryQuery.value(2).toString());
                if (!meter) {
                    continue;
                }
                const qint64 pileId = telemetryQuery.value(0).toLongLong();
                const auto previous = lastMeter.value(pileId);
                if (lastMeter.contains(pileId) && *meter >= previous) {
                    const QString bucket = telemetryQuery.value(1).toString().left(13);
                    hourlyKwh[bucket] = hourlyKwh.value(bucket) + (*meter - previous);
                }
                lastMeter.insert(pileId, *meter);
            }
        }
        // 补齐 24 个整点桶（无读数的小时为 0.00，不缺列）。
        QDateTime hour = QDateTime(windowStart.date(), QTime(windowStart.time().hour(), 0),
                                   Qt::UTC);
        for (int i = 0; i < 24; ++i) {
            const QString key = hour.toString(QStringLiteral("yyyy-MM-ddTHH"));
            QVariantMap point;
            point.insert(QStringLiteral("hourUtc"), key + QStringLiteral(":00:00Z"));
            point.insert(QStringLiteral("kwh"), hourlyKwh.value(key).toString());
            result.loadSeries24h.append(point);
            hour = hour.addSecs(3600);
        }
    }
    // 最新告警 / 未关闭工单 / 异常桩 / 最近订单（各 5 条，供总览下部清单）。
    if (query.exec(QStringLiteral(
            "SELECT id, pile_id, order_id, severity, alarm_type, content, status, occurred_at "
            "FROM alarm_event ORDER BY occurred_at DESC, id DESC LIMIT 5"))) {
        while (query.next()) {
            QVariantMap row;
            row.insert(QStringLiteral("alarmId"), query.value(0));
            row.insert(QStringLiteral("pileId"), query.value(1));
            row.insert(QStringLiteral("orderId"), query.value(2));
            row.insert(QStringLiteral("severity"), query.value(3));
            row.insert(QStringLiteral("alarmType"), query.value(4));
            row.insert(QStringLiteral("content"), query.value(5));
            row.insert(QStringLiteral("status"), query.value(6));
            row.insert(QStringLiteral("occurredAt"), query.value(7));
            result.latestAlarms.append(row);
        }
    }
    if (query.exec(QStringLiteral(
            "SELECT t.id, t.alarm_id, t.assignee, t.status, t.created_at, t.updated_at, "
            "a.alarm_type, a.severity FROM maintenance_ticket t "
            "JOIN alarm_event a ON a.id = t.alarm_id "
            "WHERE t.status IN ('OPEN','PROCESSING') "
            "ORDER BY t.updated_at DESC, t.id DESC LIMIT 5"))) {
        while (query.next()) {
            QVariantMap row;
            row.insert(QStringLiteral("ticketId"), query.value(0));
            row.insert(QStringLiteral("alarmId"), query.value(1));
            row.insert(QStringLiteral("assignee"), query.value(2));
            row.insert(QStringLiteral("status"), query.value(3));
            row.insert(QStringLiteral("createdAt"), query.value(4));
            row.insert(QStringLiteral("updatedAt"), query.value(5));
            row.insert(QStringLiteral("alarmType"), query.value(6));
            row.insert(QStringLiteral("severity"), query.value(7));
            result.openTickets.append(row);
        }
    }
    if (query.exec(QStringLiteral(
            "SELECT p.id, p.pile_sn, p.station_id, p.status, p.last_heartbeat_at, "
            "s.name AS station_name FROM pile_info p "
            "JOIN station_info s ON s.id = p.station_id "
            "WHERE p.status IN ('FAULT','OFFLINE') "
            "ORDER BY p.status, p.id LIMIT 5"))) {
        while (query.next()) {
            QVariantMap row;
            row.insert(QStringLiteral("pileId"), query.value(0));
            row.insert(QStringLiteral("pileSn"), query.value(1));
            row.insert(QStringLiteral("stationId"), query.value(2));
            row.insert(QStringLiteral("status"), query.value(3));
            row.insert(QStringLiteral("lastHeartbeatAt"), query.value(4));
            row.insert(QStringLiteral("stationName"), query.value(5));
            result.abnormalPiles.append(row);
        }
    }
    if (query.exec(QStringLiteral(
            "SELECT id, user_id, pile_id, status, energy_kwh, amount_yuan, created_at "
            "FROM charge_order ORDER BY created_at DESC, id DESC LIMIT 5"))) {
        while (query.next()) {
            QVariantMap row;
            row.insert(QStringLiteral("orderId"), query.value(0));
            row.insert(QStringLiteral("userId"), query.value(1));
            row.insert(QStringLiteral("pileId"), query.value(2));
            row.insert(QStringLiteral("status"), query.value(3));
            row.insert(QStringLiteral("energyKwh"), query.value(4));
            row.insert(QStringLiteral("amountYuan"), query.value(5));
            row.insert(QStringLiteral("createdAt"), query.value(6));
            result.recentOrders.append(row);
        }
    }
    return result;
}

// 营收趋势：近 N 天（1~366）逐日 DecimalValue 聚合，日期维度按 UTC；可按站点过滤。
// 无订单的日期补零填充，保证趋势曲线日期连续。
// S1-B 营收页汇总：区间内已支付（FINISHED）订单的指标、站点排行与 24 小时分布。
// 只用 SQL 取原始行，Decimal 聚合在 C++ 侧完成，避免浮点累加；全部输出两位小数。
AdminRevenueSummary AdminService::revenueSummary(const QString &fromDate,
                                                 const QString &toDate) const
{
    AdminRevenueSummary result;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT s.id, s.name, o.amount_yuan, o.energy_kwh, "
        "substr(COALESCE(o.ended_at,o.created_at),12,2) "
        "FROM charge_order o "
        "JOIN pile_info p ON p.id = o.pile_id "
        "JOIN station_info s ON s.id = p.station_id "
        "WHERE o.status='FINISHED' "
        "AND substr(COALESCE(o.ended_at,o.created_at),1,10) BETWEEN ? AND ? "
        "ORDER BY s.id, o.created_at"));
    query.addBindValue(fromDate);
    query.addBindValue(toDate);
    if (!query.exec()) {
        return result;
    }
    struct StationLine
    {
        DecimalValue revenue;
        int orderCount = 0;
        QString name;
    };
    QHash<qint64, StationLine> ranking;        // 站点 → 营收/单数
    QHash<QString, DecimalValue> hourRevenue;  // "HH" → 营收
    while (query.next()) {
        const auto amount = DecimalValue::parseCanonical(query.value(2).toString());
        const auto energy = DecimalValue::parseCanonical(query.value(3).toString());
        if (!amount) {
            continue;
        }
        const qint64 stationId = query.value(0).toLongLong();
        auto &line = ranking[stationId];
        line.name = query.value(1).toString();
        line.revenue = line.revenue + *amount;
        ++line.orderCount;
        result.totalRevenueYuan = result.totalRevenueYuan + *amount;
        ++result.orderCount;
        if (energy) {
            result.totalEnergyKwh = result.totalEnergyKwh + *energy;
        }
        const QString hour = query.value(4).toString();
        if (hour.size() == 2) {
            hourRevenue[hour] = hourRevenue.value(hour) + *amount;
        }
    }
    // 平均客单价与平均单价：Decimal 高精度除法保留两位（总电量/单数为 0 时输出 "0.00"）。
    if (result.orderCount > 0) {
        const auto count = DecimalValue::parseRounded(QString::number(result.orderCount));
        if (count) {
            result.avgOrderYuan = divideRounded(result.totalRevenueYuan, *count);
        }
    }
    if (!result.totalEnergyKwh.isZero()) {
        result.avgPriceYuan = divideRounded(result.totalRevenueYuan, result.totalEnergyKwh);
    }
    QList<qint64> stationIds;
    for (auto it = ranking.constBegin(); it != ranking.constEnd(); ++it) {
        stationIds.append(it.key());
    }
    std::sort(stationIds.begin(), stationIds.end(), [&ranking](qint64 a, qint64 b) {
        const DecimalValue &ra = ranking[a].revenue;
        const DecimalValue &rb = ranking[b].revenue;
        if (ra == rb) return a < b;
        return ra > rb;
    });
    for (qint64 id : stationIds) {
        QVariantMap row;
        row.insert(QStringLiteral("stationId"), id);
        row.insert(QStringLiteral("stationName"), ranking[id].name);
        row.insert(QStringLiteral("revenueYuan"), ranking[id].revenue.toString());
        row.insert(QStringLiteral("orderCount"), ranking[id].orderCount);
        result.stationRanking.append(row);
    }
    for (int hour = 0; hour < 24; ++hour) {
        const QString key = QStringLiteral("%1").arg(hour, 2, 10, QLatin1Char('0'));
        QVariantMap row;
        row.insert(QStringLiteral("hourUtc"), key);
        row.insert(QStringLiteral("revenueYuan"), hourRevenue.value(key).toString());
        result.hourHistogram.append(row);
    }
    return result;
}

QVector<RevenuePoint> AdminService::revenueTrend(int days, qint64 stationId) const
{
    if (days < 1 || days > 366 || stationId < 0) {
        return {};
    }
    const QDate end = QDateTime::currentDateTimeUtc().date();
    const QDate start = end.addDays(1 - days);
    QHash<QString, DecimalValue> values;
    QSqlQuery query(database_);
    QString sql = QStringLiteral(
        "SELECT substr(COALESCE(o.ended_at,o.created_at),1,10),o.amount_yuan "
        "FROM charge_order o JOIN pile_info p ON p.id=o.pile_id "
        "WHERE o.status='FINISHED' AND substr(COALESCE(o.ended_at,o.created_at),1,10)>=?");
    if (stationId > 0) {
        sql += QStringLiteral(" AND p.station_id=?");
    }
    sql += QStringLiteral(" ORDER BY 1");
    query.prepare(sql);
    query.addBindValue(start.toString(Qt::ISODate));
    if (stationId > 0) query.addBindValue(stationId);
    if (!query.exec()) {
        return {};
    }
    while (query.next()) {
        const QString date = query.value(0).toString();
        const auto amount = DecimalValue::parseCanonical(query.value(1).toString());
        if (!amount) {
            return {};
        }
        values.insert(date, values.value(date) + *amount);
    }
    QVector<RevenuePoint> result;
    result.reserve(days);
    for (int index = 0; index < days; ++index) {
        const QString date = start.addDays(index).toString(Qt::ISODate);
        result.push_back({date, values.value(date)});
    }
    return result;
}

// —— 管理列表查询组 ——以下七个查询只读，统一分页（1~500），LIKE 检索用参数绑定防注入。
QList<QVariantMap> AdminService::stations(const QString &search, int limit, int offset) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT s.id,s.name,s.address,s.status,s.price_yuan_per_kwh,COUNT(p.id),"
        "SUM(CASE WHEN p.status='IDLE' THEN 1 ELSE 0 END) "
        "FROM station_info s LEFT JOIN pile_info p ON p.station_id=s.id "
        "WHERE (?='' OR s.name LIKE ? OR s.address LIKE ?) GROUP BY s.id "
        "ORDER BY s.id LIMIT ? OFFSET ?"));
    const QString pattern = QStringLiteral("%") + search.trimmed() + QStringLiteral("%");
    query.addBindValue(search.trimmed()); query.addBindValue(pattern); query.addBindValue(pattern);
    query.addBindValue(pageSize(limit)); query.addBindValue(qMax(0, offset));
    QList<QVariantMap> result;
    if (!query.exec()) return result;
    while (query.next()) result.append({
        {QStringLiteral("stationId"), query.value(0)}, {QStringLiteral("name"), query.value(1)},
        {QStringLiteral("address"), query.value(2)}, {QStringLiteral("status"), query.value(3)},
        {QStringLiteral("priceYuanPerKwh"), query.value(4)},
        {QStringLiteral("totalPiles"), query.value(5)}, {QStringLiteral("availablePiles"), query.value(6)}});
    return result;
}

QList<QVariantMap> AdminService::piles(qint64 stationId, const QString &status,
                                       int limit, int offset) const
{
    QSqlQuery query(database_);
    QString sql = QStringLiteral(
        "SELECT id,station_id,pile_sn,charge_type,rated_power_kw,status,last_heartbeat_at "
        "FROM pile_info WHERE 1=1");
    if (stationId > 0) sql += QStringLiteral(" AND station_id=?");
    if (!status.isEmpty()) sql += QStringLiteral(" AND status=?");
    sql += QStringLiteral(" ORDER BY id LIMIT ? OFFSET ?");
    query.prepare(sql);
    if (stationId > 0) query.addBindValue(stationId);
    if (!status.isEmpty()) query.addBindValue(status);
    query.addBindValue(pageSize(limit)); query.addBindValue(qMax(0, offset));
    QList<QVariantMap> result;
    if (!query.exec()) return result;
    while (query.next()) result.append({
        {QStringLiteral("pileId"), query.value(0)}, {QStringLiteral("stationId"), query.value(1)},
        {QStringLiteral("serial"), query.value(2)}, {QStringLiteral("chargeType"), query.value(3)},
        {QStringLiteral("ratedPowerKw"), query.value(4)}, {QStringLiteral("status"), query.value(5)},
        {QStringLiteral("lastHeartbeatAt"), query.value(6)}});
    return result;
}

QList<QVariantMap> AdminService::orders(const QString &status, int limit, int offset) const
{
    QSqlQuery query(database_);
    QString sql = QStringLiteral(
        "SELECT id,user_id,pile_id,status,energy_kwh,amount_yuan,created_at,ended_at "
        "FROM charge_order WHERE 1=1");
    if (!status.isEmpty()) sql += QStringLiteral(" AND status=?");
    sql += QStringLiteral(" ORDER BY created_at DESC,id LIMIT ? OFFSET ?");
    query.prepare(sql);
    if (!status.isEmpty()) query.addBindValue(status);
    query.addBindValue(pageSize(limit)); query.addBindValue(qMax(0, offset));
    QList<QVariantMap> result;
    if (!query.exec()) return result;
    while (query.next()) result.append({
        {QStringLiteral("orderId"), query.value(0)}, {QStringLiteral("userId"), query.value(1)},
        {QStringLiteral("pileId"), query.value(2)}, {QStringLiteral("status"), query.value(3)},
        {QStringLiteral("energyKwh"), query.value(4)},
        {QStringLiteral("amountYuan"), query.value(5)},
        {QStringLiteral("createdAt"), query.value(6)}, {QStringLiteral("endedAt"), query.value(7)}});
    return result;
}

QList<QVariantMap> AdminService::users(const QString &status, const QString &search,
                                       int limit, int offset) const
{
    QSqlQuery query(database_);
    QString sql = QStringLiteral(
        "SELECT id,phone,nickname,balance_yuan,status,created_at FROM user_info "
        "WHERE 1=1");
    const QString pattern = QStringLiteral("%") + search.trimmed() + QStringLiteral("%");
    if (!status.isEmpty()) sql += QStringLiteral(" AND status=?");
    if (!search.trimmed().isEmpty()) sql += QStringLiteral(" AND (phone LIKE ? OR nickname LIKE ?)");
    sql += QStringLiteral(" ORDER BY id LIMIT ? OFFSET ?");
    query.prepare(sql);
    if (!status.isEmpty()) query.addBindValue(status);
    if (!search.trimmed().isEmpty()) {
        query.addBindValue(pattern); query.addBindValue(pattern);
    }
    query.addBindValue(pageSize(limit)); query.addBindValue(qMax(0, offset));
    QList<QVariantMap> result;
    if (!query.exec()) return result;
    while (query.next()) result.append({
        {QStringLiteral("userId"), query.value(0)},
        {QStringLiteral("phone"), maskedPhone(query.value(1).toString())},
        {QStringLiteral("nickname"), query.value(2)},
        {QStringLiteral("balanceYuan"), query.value(3)},
        {QStringLiteral("status"), query.value(4)}, {QStringLiteral("createdAt"), query.value(5)}});
    return result;
}

QList<QVariantMap> AdminService::operationLogs(const QString &action,
                                               int limit, int offset) const
{
    QSqlQuery query(database_);
    QString sql = QStringLiteral(
        "SELECT id,actor_type,actor_id,action,target_type,target_id,result,created_at "
        "FROM operation_log WHERE 1=1");
    if (!action.isEmpty()) sql += QStringLiteral(" AND action=?");
    sql += QStringLiteral(" ORDER BY id DESC LIMIT ? OFFSET ?");
    query.prepare(sql);
    if (!action.isEmpty()) query.addBindValue(action);
    query.addBindValue(pageSize(limit)); query.addBindValue(qMax(0, offset));
    QList<QVariantMap> result;
    if (!query.exec()) return result;
    while (query.next()) result.append({
        {QStringLiteral("logId"), query.value(0)}, {QStringLiteral("actorType"), query.value(1)},
        {QStringLiteral("actorId"), query.value(2)}, {QStringLiteral("action"), query.value(3)},
        {QStringLiteral("targetType"), query.value(4)}, {QStringLiteral("targetId"), query.value(5)},
        {QStringLiteral("result"), query.value(6)}, {QStringLiteral("createdAt"), query.value(7)}});
    return result;
}

QList<QVariantMap> AdminService::alarms(const QString &status, const QString &severity,
                                        int limit, int offset) const
{
    QSqlQuery query(database_);
    QString sql = QStringLiteral(
        "SELECT a.id,a.pile_id,a.order_id,a.severity,a.alarm_type,a.content,a.status,"
        "a.occurred_at,a.closed_at,p.pile_sn FROM alarm_event a "
        "JOIN pile_info p ON p.id=a.pile_id WHERE 1=1");
    if (!status.isEmpty()) sql += QStringLiteral(" AND a.status=?");
    if (!severity.isEmpty()) sql += QStringLiteral(" AND a.severity=?");
    sql += QStringLiteral(" ORDER BY a.occurred_at DESC,a.id DESC LIMIT ? OFFSET ?");
    query.prepare(sql);
    if (!status.isEmpty()) query.addBindValue(status);
    if (!severity.isEmpty()) query.addBindValue(severity);
    query.addBindValue(pageSize(limit));
    query.addBindValue(qMax(0, offset));
    QList<QVariantMap> result;
    if (!query.exec()) return result;
    while (query.next()) result.append({
        {QStringLiteral("alarmId"), query.value(0)},
        {QStringLiteral("pileId"), query.value(1)},
        {QStringLiteral("orderId"), query.value(2)},
        {QStringLiteral("severity"), query.value(3)},
        {QStringLiteral("alarmType"), query.value(4)},
        {QStringLiteral("content"), query.value(5)},
        {QStringLiteral("status"), query.value(6)},
        {QStringLiteral("occurredAt"), query.value(7)},
        {QStringLiteral("closedAt"), query.value(8)},
        {QStringLiteral("pileSerial"), query.value(9)}});
    return result;
}

QList<QVariantMap> AdminService::tickets(const QString &status, int limit, int offset) const
{
    QSqlQuery query(database_);
    QString sql = QStringLiteral(
        "SELECT t.id,t.alarm_id,a.pile_id,t.assignee,t.status,t.notes,t.created_at,"
        "t.updated_at,t.resolved_at,t.closed_at,a.severity,a.alarm_type FROM maintenance_ticket t "
        "JOIN alarm_event a ON a.id=t.alarm_id WHERE 1=1");
    if (!status.isEmpty()) sql += QStringLiteral(" AND t.status=?");
    sql += QStringLiteral(" ORDER BY t.updated_at DESC,t.id DESC LIMIT ? OFFSET ?");
    query.prepare(sql);
    if (!status.isEmpty()) query.addBindValue(status);
    query.addBindValue(pageSize(limit));
    query.addBindValue(qMax(0, offset));
    QList<QVariantMap> result;
    if (!query.exec()) return result;
    while (query.next()) result.append({
        {QStringLiteral("ticketId"), query.value(0)},
        {QStringLiteral("alarmId"), query.value(1)},
        {QStringLiteral("pileId"), query.value(2)},
        {QStringLiteral("assignee"), query.value(3)},
        {QStringLiteral("status"), query.value(4)},
        {QStringLiteral("notes"), query.value(5)},
        {QStringLiteral("createdAt"), query.value(6)},
        {QStringLiteral("updatedAt"), query.value(7)},
        {QStringLiteral("resolvedAt"), query.value(8)},
        {QStringLiteral("closedAt"), query.value(9)},
        {QStringLiteral("severity"), query.value(10)},
        {QStringLiteral("alarmType"), query.value(11)}});
    return result;
}

// 用户冻结/解冻：受影响行数必须为 1（否则 NOT_FOUND）；冻结原因与审计日志同事务提交。
AdminResult AdminService::setUserFrozen(qint64 userId, bool frozen, const QString &reason,
                                        const QString &actorId)
{
    if (userId <= 0 || reason.trimmed().isEmpty() || actorId.trimmed().isEmpty()) {
        return failure(QStringLiteral("PARAM_INVALID"));
    }
    if (!database_.transaction()) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    const QString status = frozen ? QStringLiteral("FROZEN") : QStringLiteral("NORMAL");
    QSqlQuery update(database_);
    update.prepare(QStringLiteral("UPDATE user_info SET status=? WHERE id=?"));
    update.addBindValue(status);
    update.addBindValue(userId);
    if (!update.exec() || update.numRowsAffected() != 1) {
        database_.rollback();
        return failure(update.lastError().isValid() ? QStringLiteral("DATABASE_ERROR")
                                                    : QStringLiteral("NOT_FOUND"));
    }

    const QString action = frozen ? QStringLiteral("USER_FREEZE")
                                  : QStringLiteral("USER_UNFREEZE");
    const QString result = QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("ok"), true}, {QStringLiteral("reason"), reason.trimmed()}
    }).toJson(QJsonDocument::Compact));
    QSqlQuery log(database_);
    log.prepare(QStringLiteral(
        "INSERT INTO operation_log(actor_type,actor_id,action,target_type,target_id,result) "
        "VALUES('ADMIN',? ,?,'USER',?,?)"));
    log.addBindValue(actorId);
    log.addBindValue(action);
    log.addBindValue(QString::number(userId));
    log.addBindValue(result);
    // S1-A：提交前抬升联动版本，冻结/解冻对其他端可见。
    if (linkage::bumpVersion(database_) < 0) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (!log.exec() || !database_.commit()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    return {true, QStringLiteral("OK"), 0};
}

AdminResult AdminService::createStation(const QString &name, const QString &address,
                                        double latitude, double longitude,
                                        const DecimalValue &priceYuanPerKwh,
                                        const QList<NewPile> &piles)
{
    return createStation(name, address, latitude, longitude, priceYuanPerKwh, piles,
                         QStringLiteral("SYSTEM"));
}

// 新建站点（带审计重载）：名称/地址/坐标/单价（≤1000.00）/桩列表全部校验后，
// 站点、逐个电桩、审计日志在一个事务内落库，任一失败整体回滚。
AdminResult AdminService::createStation(const QString &name, const QString &address,
                                        double latitude, double longitude,
                                        const DecimalValue &priceYuanPerKwh,
                                        const QList<NewPile> &piles,
                                        const QString &actorId)
{
    const DecimalValue maximumPrice =
        *DecimalValue::parseCanonical(QStringLiteral("1000.00"));
    if (actorId.trimmed().isEmpty() || name.trimmed().isEmpty() || address.trimmed().isEmpty()
        || !std::isfinite(latitude) || latitude < -90.0 || latitude > 90.0
        || !std::isfinite(longitude) || longitude < -180.0 || longitude > 180.0
        || priceYuanPerKwh.isNegative() || priceYuanPerKwh > maximumPrice
        || piles.isEmpty()) {
        return failure(QStringLiteral("PARAM_INVALID"));
    }
    for (const auto &pile : piles) {
        if (pile.serial.trimmed().isEmpty()
            || (pile.chargeType != QStringLiteral("FAST")
                && pile.chargeType != QStringLiteral("SLOW"))
            || !std::isfinite(pile.ratedPowerKw) || pile.ratedPowerKw <= 0) {
            return failure(QStringLiteral("PARAM_INVALID"));
        }
    }
    if (!database_.transaction()) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    QSqlQuery station(database_);
    station.prepare(QStringLiteral(
        "INSERT INTO station_info(name,address,latitude,longitude,price_yuan_per_kwh,status) "
        "VALUES(?,?,?,?,?,'OPEN')"));
    station.addBindValue(name.trimmed());
    station.addBindValue(address.trimmed());
    station.addBindValue(latitude);
    station.addBindValue(longitude);
    station.addBindValue(priceYuanPerKwh.toString());
    if (!station.exec()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    const qint64 stationId = station.lastInsertId().toLongLong();
    for (const auto &pile : piles) {
        QSqlQuery insertPile(database_);
        insertPile.prepare(QStringLiteral(
            "INSERT INTO pile_info(station_id,pile_sn,charge_type,rated_power_kw,status) "
            "VALUES(?,?,?,?,'IDLE')"));
        insertPile.addBindValue(stationId);
        insertPile.addBindValue(pile.serial.trimmed());
        insertPile.addBindValue(pile.chargeType);
        insertPile.addBindValue(pile.ratedPowerKw);
        if (!insertPile.exec()) {
            database_.rollback();
            return failure(QStringLiteral("DATABASE_ERROR"));
        }
    }
    if (linkage::bumpVersion(database_) < 0) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (!addAuditLog(database_, actorId, QStringLiteral("CREATE_STATION"),
                     QStringLiteral("STATION"), stationId,
                     {{QStringLiteral("ok"), true},
                      {QStringLiteral("pileCount"), piles.size()}})
        || !database_.commit()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    return {true, QStringLiteral("OK"), stationId};
}

// 修改站点：状态限定 OPEN/CLOSED/MAINTENANCE；受影响行数必须为 1；审计日志同事务。
AdminResult AdminService::updateStation(qint64 stationId, const QString &name,
                                        const QString &address, double latitude,
                                        double longitude,
                                        const DecimalValue &priceYuanPerKwh,
                                        const QString &status, const QString &actorId)
{
    static const QSet<QString> Statuses{QStringLiteral("OPEN"),
                                        QStringLiteral("CLOSED"),
                                        QStringLiteral("MAINTENANCE")};
    const DecimalValue maximumPrice =
        *DecimalValue::parseCanonical(QStringLiteral("1000.00"));
    if (stationId <= 0 || actorId.trimmed().isEmpty() || name.trimmed().isEmpty()
        || address.trimmed().isEmpty() || !std::isfinite(latitude) || latitude < -90.0
        || latitude > 90.0 || !std::isfinite(longitude) || longitude < -180.0
        || longitude > 180.0 || priceYuanPerKwh.isNegative()
        || priceYuanPerKwh > maximumPrice || !Statuses.contains(status)) {
        return failure(QStringLiteral("PARAM_INVALID"));
    }
    if (!database_.transaction()) return failure(QStringLiteral("DATABASE_ERROR"));
    QSqlQuery update(database_);
    update.prepare(QStringLiteral(
        "UPDATE station_info SET name=?,address=?,latitude=?,longitude=?,"
        "price_yuan_per_kwh=?,status=? WHERE id=?"));
    update.addBindValue(name.trimmed());
    update.addBindValue(address.trimmed());
    update.addBindValue(latitude);
    update.addBindValue(longitude);
    update.addBindValue(priceYuanPerKwh.toString());
    update.addBindValue(status);
    update.addBindValue(stationId);
    if (!update.exec() || update.numRowsAffected() != 1) {
        const bool databaseError = update.lastError().isValid();
        database_.rollback();
        return failure(databaseError ? QStringLiteral("DATABASE_ERROR")
                                     : QStringLiteral("NOT_FOUND"));
    }
    if (linkage::bumpVersion(database_) < 0) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (!addAuditLog(database_, actorId, QStringLiteral("UPDATE_STATION"),
                     QStringLiteral("STATION"), stationId,
                     {{QStringLiteral("ok"), true}, {QStringLiteral("status"), status}})
        || !database_.commit()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    return {true, QStringLiteral("OK"), stationId};
}

// 人工设置桩状态：只允许 IDLE/FAULT/OFFLINE 三个目标状态；
// 当前状态为 RESERVED/CHARGING 时拒绝（STATE_CONFLICT）——不能把正在使用的桩“手动空闲”。
AdminResult AdminService::setPileStatus(qint64 pileId, const QString &status,
                                        const QString &reason, const QString &actorId)
{
    static const QSet<QString> Statuses{QStringLiteral("IDLE"),
                                        QStringLiteral("FAULT"),
                                        QStringLiteral("OFFLINE")};
    if (pileId <= 0 || actorId.trimmed().isEmpty() || reason.trimmed().isEmpty()
        || !Statuses.contains(status)) {
        return failure(QStringLiteral("PARAM_INVALID"));
    }
    if (!database_.transaction()) return failure(QStringLiteral("DATABASE_ERROR"));
    QSqlQuery current(database_);
    current.prepare(QStringLiteral("SELECT status FROM pile_info WHERE id=?"));
    current.addBindValue(pileId);
    if (!current.exec()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (!current.next()) {
        database_.rollback();
        return failure(QStringLiteral("NOT_FOUND"));
    }
    const QString previous = current.value(0).toString();
    if (previous == QStringLiteral("RESERVED") || previous == QStringLiteral("CHARGING")) {
        database_.rollback();
        return failure(QStringLiteral("STATE_CONFLICT"));
    }
    QSqlQuery update(database_);
    update.prepare(QStringLiteral("UPDATE pile_info SET status=? WHERE id=?"));
    update.addBindValue(status);
    update.addBindValue(pileId);
    if (linkage::bumpVersion(database_) < 0) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (!update.exec() || !addAuditLog(
            database_, actorId, QStringLiteral("SET_PILE_STATUS"), QStringLiteral("PILE"),
            pileId, {{QStringLiteral("ok"), true}, {QStringLiteral("from"), previous},
                     {QStringLiteral("to"), status},
                     {QStringLiteral("reason"), reason.trimmed()}})
        || !database_.commit()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    return {true, QStringLiteral("OK"), 0};
}

// 模拟重启电桩（答辩易被追问）：
// 1) 查该桩最新活动订单，推导恢复状态：有 RESERVED 订单 → RESERVED；
//    有 CHARGING/PENDING_PAYMENT 订单 → CHARGING；无活动订单 → IDLE；
// 2) 条件更新 WHERE status IN ('FAULT','OFFLINE')——只能重启故障/离线桩，
//    受影响行数≠1 时区分 STATE_CONFLICT 与 NOT_FOUND；
// 3) 生成 commandId（CMD-<uuid>）并写审计日志，同一事务提交。
// 注意口径：这是服务端数据库层面的模拟恢复，并未向真实硬件下发重启命令并等待回执。
PileRestartResult AdminService::restartPile(qint64 pileId, const QString &actorId)
{
    if (pileId <= 0 || actorId.trimmed().isEmpty()) {
        return {false, QStringLiteral("PARAM_INVALID"), {}, {}};
    }
    if (!database_.transaction()) {
        return {false, QStringLiteral("DATABASE_ERROR"), {}, {}};
    }
    QString restoredStatus = QStringLiteral("IDLE");
    QSqlQuery activeOrder(database_);
    activeOrder.prepare(QStringLiteral(
        "SELECT status FROM charge_order WHERE pile_id=? "
        "AND status IN ('RESERVED','CHARGING','PENDING_PAYMENT') "
        "ORDER BY created_at DESC,id LIMIT 1"));
    activeOrder.addBindValue(pileId);
    if (!activeOrder.exec()) {
        database_.rollback();
        return {false, QStringLiteral("DATABASE_ERROR"), {}, {}};
    }
    if (activeOrder.next()) {
        const QString orderStatus = activeOrder.value(0).toString();
        restoredStatus = orderStatus == QStringLiteral("RESERVED")
            ? QStringLiteral("RESERVED") : QStringLiteral("CHARGING");
    }
    QSqlQuery update(database_);
    update.prepare(QStringLiteral(
        "UPDATE pile_info SET status=?,last_heartbeat_at=? "
        "WHERE id=? AND status IN ('FAULT','OFFLINE')"));
    update.addBindValue(restoredStatus);
    update.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    update.addBindValue(pileId);
    if (!update.exec()) {
        database_.rollback();
        return {false, QStringLiteral("DATABASE_ERROR"), {}, {}};
    }
    if (update.numRowsAffected() != 1) {
        QSqlQuery exists(database_);
        exists.prepare(QStringLiteral("SELECT 1 FROM pile_info WHERE id=?"));
        exists.addBindValue(pileId);
        const bool found = exists.exec() && exists.next();
        database_.rollback();
        return {false, found ? QStringLiteral("STATE_CONFLICT")
                             : QStringLiteral("NOT_FOUND"), {}, {}};
    }
    const QString commandId = QStringLiteral("CMD-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (linkage::bumpVersion(database_) < 0) {
        database_.rollback();
        return {false, QStringLiteral("DATABASE_ERROR"), {}, {}};
    }
    if (!addAuditLog(database_, actorId, QStringLiteral("RESTART_PILE"),
                     QStringLiteral("PILE"), pileId,
                     {{QStringLiteral("ok"), true},
                      {QStringLiteral("commandId"), commandId}})
        || !database_.commit()) {
        database_.rollback();
        return {false, QStringLiteral("DATABASE_ERROR"), {}, {}};
    }
    return {true, QStringLiteral("OK"), commandId, restoredStatus};
}
