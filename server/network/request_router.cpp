#include "server/network/request_router.h"

#include "server/services/admin_service.h"
#include "server/services/linkage.h"
#include "server/services/user_session_service.h"
#include "server/services/alarm_service.h"
#include "server/services/order_service.h"
#include "server/services/station_service.h"
#include "server/services/telemetry_service.h"
#include "server/services/user_service.h"
#include "server/services/wallet_service.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QUuid>
#include <cmath>
#include <exception>
#include <utility>

// ============================================================================
// RequestRouter 实现：动作注册表（成员1 · 第一阶段 · 网络层）。
// 每个 registerHandler 对应一个协议动作，套路一致：
//   databaseReady 前置检查 → 参数校验 → 调用对应 Service → 统一组装响应；
// 失败统一经 serviceFailure() 把服务层错误码映射为固定 ErrorCode；
// 管理类动作（除 ADMIN_LOGIN）都先经 adminReady() 校验 adminToken。
// ============================================================================
namespace {
// 服务层错误码字符串 → ErrorCode 枚举的统一映射，未识别的回落 PROTOCOL_ERROR。
ErrorCode errorCode(const QString &name)
{
    if (name == QStringLiteral("PARAM_INVALID")) return ErrorCode::ParamInvalid;
    if (name == QStringLiteral("USER_FROZEN")) return ErrorCode::UserFrozen;
    if (name == QStringLiteral("UNFINISHED_ORDER")) return ErrorCode::UnfinishedOrder;
    if (name == QStringLiteral("PILE_UNAVAILABLE")) return ErrorCode::PileUnavailable;
    if (name == QStringLiteral("ORDER_STATE_CONFLICT")) return ErrorCode::OrderStateConflict;
    if (name == QStringLiteral("BALANCE_INSUFFICIENT")) return ErrorCode::BalanceInsufficient;
    if (name == QStringLiteral("DUPLICATE_REQUEST")) return ErrorCode::DuplicateRequest;
    if (name == QStringLiteral("TELEMETRY_OUT_OF_ORDER")) {
        return ErrorCode::TelemetryOutOfOrder;
    }
    // M1-03：表计疑似归零/换表——拒绝并要求维护（01 文档 4.2）。
    if (name == QStringLiteral("METER_RESET_SUSPECTED")) {
        return ErrorCode::MeterResetSuspected;
    }
    // M1-03：开始前无新鲜读数（01 文档 4.2）。
    if (name == QStringLiteral("DEVICE_TIMEOUT")) {
        return ErrorCode::DeviceTimeout;
    }
    if (name == QStringLiteral("STATE_CONFLICT")) return ErrorCode::StateConflict;
    if (name == QStringLiteral("DEVICE_NOT_RECOVERED")) {
        return ErrorCode::DeviceNotRecovered;
    }
    if (name == QStringLiteral("NOT_FOUND")) return ErrorCode::NotFound;
    if (name == QStringLiteral("DATABASE_ERROR")) return ErrorCode::DatabaseError;
    if (name == QStringLiteral("AUTH_FAILED")) return ErrorCode::AuthInvalid;
    // M1-02：用户会话/归属类失败（与 AUTH_FAILED 同一枚举，message 保留原串）。
    if (name == QStringLiteral("AUTH_INVALID")) return ErrorCode::AuthInvalid;
    return ErrorCode::ProtocolError;
}

// 统一失败响应：保留原 requestId，code 为映射后的枚举、message 为原始错误码字符串。
Response serviceFailure(const Message &message, const QString &code)
{
    return Response::failure(message.requestId, errorCode(code), code);
}

// —— S1-A 联动契约（01 修改规格 · 公共联动）——
// 写动作集合：成功后 data 注入 operationId/changeVersion/serverTimeUtc；
// 其余动作视为查询：成功后 data 注入 snapshotVersion/snapshotAtUtc。
// 版本由各写服务在事务提交前经 linkage::bumpVersion 抬升，路由层只读取并标注。
// 口径说明：登录类动作只颁发会话（自动注册的用户对桩/订单/告警展示无影响），
// 按查询口径标注快照版本即可，不进入写集合。
bool isWriteAction(const QString &action)
{
    static const QSet<QString> writeActions = {
        QStringLiteral("USER_PROFILE_UPDATE"),
        QStringLiteral("ORDER_RESERVE"),
        QStringLiteral("ORDER_CANCEL"),
        QStringLiteral("ORDER_START"),
        QStringLiteral("ORDER_STOP"),
        QStringLiteral("PILE_TELEMETRY"),
        QStringLiteral("WALLET_PAY"),
        QStringLiteral("WALLET_RECHARGE"),
        QStringLiteral("ADMIN_USER_FREEZE"),
        QStringLiteral("ADMIN_ALARM_ACK"),
        QStringLiteral("ADMIN_TICKET_START"),
        QStringLiteral("ADMIN_TICKET_RESOLVE"),
        QStringLiteral("ADMIN_TICKET_CLOSE"),
        QStringLiteral("ADMIN_STATION_CREATE"),
        QStringLiteral("ADMIN_STATION_UPDATE"),
        QStringLiteral("ADMIN_PILE_SET_STATUS"),
        QStringLiteral("ADMIN_PILE_RESTART"),
    };
    return writeActions.contains(action);
}

void stampLinkageMetadata(const Message &message, Response *response,
                          const RequestContext &context)
{
    if (isWriteAction(message.action)) {
        response->data.insert(QStringLiteral("operationId"),
                              QStringLiteral("OP-")
                                  + QUuid::createUuid().toString(QUuid::WithoutBraces));
        response->data.insert(QStringLiteral("changeVersion"),
                              static_cast<double>(linkage::currentVersion(context.database)));
        response->data.insert(QStringLiteral("serverTimeUtc"), linkage::serverTimeUtc());
    } else if (message.action != QStringLiteral("PING")) {
        response->data.insert(QStringLiteral("snapshotVersion"),
                              static_cast<double>(linkage::currentVersion(context.database)));
        response->data.insert(QStringLiteral("snapshotAtUtc"), linkage::serverTimeUtc());
    }
}

// 公共前置检查：数据库连接未打开时直接返回 DATABASE_ERROR，不进入业务处理。
bool databaseReady(const Message &message, RequestContext &context, Response *response)
{
    if (context.database.isOpen()) {
        return true;
    }
    *response = Response::failure(message.requestId, ErrorCode::DatabaseError,
                                  QStringLiteral("DATABASE_ERROR"));
    return false;
}

// 提取十进制字符串字段并解析为 DecimalValue；显式拒绝负号开头——
// 电量/金额/单价等业务输入不允许为负（此处先拦截，服务层再做范围校验）。
std::optional<DecimalValue> decimalField(const QJsonObject &payload, const QString &name)
{
    const QJsonValue raw = payload.value(name);
    if (!raw.isString() || raw.toString().startsWith(QLatin1Char('-'))) {
        return std::nullopt;
    }
    return DecimalValue::parseRounded(raw.toString());
}

// 查询结果行（QVariantMap）→ JSON 数组，供各列表类动作复用。
QJsonArray rowsToJson(const QList<QVariantMap> &rows)
{
    QJsonArray result;
    for (const auto &row : rows) {
        result.append(QJsonObject::fromVariantMap(row));
    }
    return result;
}

// 管理动作统一身份闸门：校验 payload.adminToken（数据库存其 SHA-256 哈希，8 小时有效）。
// 答辩口径：除 ADMIN_LOGIN 外所有 ADMIN_* 动作都必须先过这一关；
// 用户侧动作目前以 userId 标识、尚无等价会话认证，属如实说明的边界。
bool adminReady(const Message &message, RequestContext &context, Response *response,
                QString *username = nullptr)
{
    if (!databaseReady(message, context, response)) {
        return false;
    }
    const QString authenticated = AdminService(context.database).authenticatedAdmin(
        message.payload.value(QStringLiteral("adminToken")).toString());
    if (authenticated.isEmpty()) {
        *response = Response::failure(message.requestId, ErrorCode::AuthInvalid,
                                      QStringLiteral("AUTH_INVALID"));
        return false;
    }
    if (username) {
        *username = authenticated;
    }
    return true;
}

// M1-02 用户侧统一身份闸门：校验 payload.userToken（库存 SHA-256 摘要，8 小时）。
// 返回 false 时 response 已填好 AUTH_INVALID；返回 true 时 *userId 为令牌归属用户，
// *restricted 为冻结标志。与 adminToken 互不通用（表不同、校验不同）。
bool userReady(const Message &message, RequestContext &context, Response *response,
               qint64 *userId = nullptr, bool *restricted = nullptr)
{
    if (!databaseReady(message, context, response)) {
        return false;
    }
    const auto session = UserSessionService(context.database).verify(
        message.payload.value(QStringLiteral("userToken")).toString());
    if (!session.ok) {
        *response = Response::failure(message.requestId, ErrorCode::AuthInvalid,
                                      QStringLiteral("AUTH_INVALID"));
        return false;
    }
    if (userId) {
        *userId = session.userId;
    }
    if (restricted) {
        *restricted = session.restricted;
    }
    return true;
}
}

RequestRouter::RequestRouter()
{
    // —— 健康检查 ——
    registerHandler(QStringLiteral("PING"), [](const Message &message, RequestContext &) {
        return Response::success(message.requestId,
                                 QJsonObject{{QStringLiteral("reply"), QStringLiteral("PONG")}});
    });
    // —— 用户侧 ——
    // 登录（教学模拟）：按手机号查找或自动注册并赠送初始余额；requestId 兼做登录防重。
    registerHandler(QStringLiteral("USER_LOGIN"), [](const Message &message,
                                                       RequestContext &context) {
        Response unavailable;
        if (!databaseReady(message, context, &unavailable)) return unavailable;
        UserService service(context.database);
        const auto result = service.loginOrRegister(
            message.payload.value(QStringLiteral("phone")).toString(), message.requestId);
        if (!result.ok) return serviceFailure(message, result.errorCode);
        return Response::success(message.requestId, {
            {QStringLiteral("userId"), result.userId},
            {QStringLiteral("nickname"), result.nickname},
            {QStringLiteral("restricted"), result.restricted},
            {QStringLiteral("userToken"), result.userToken},
            {QStringLiteral("expiresAt"), result.expiresAtUtc}
        });
    });
    // 查询资料：手机号打码输出，余额为两位小数字符串。
    registerHandler(QStringLiteral("USER_PROFILE"), [](const Message &message,
                                                         RequestContext &context) {
        // M1-02：身份由 userToken 决定；payload.userId 若携带必须与令牌一致。
        Response unavailable;
        qint64 tokenUserId = 0;
        if (!userReady(message, context, &unavailable, &tokenUserId)) return unavailable;
        const QJsonValue requestedId = message.payload.value(QStringLiteral("userId"));
        if (!requestedId.isUndefined()
            && requestedId.toInteger() != tokenUserId) {
            return serviceFailure(message, QStringLiteral("AUTH_INVALID"));
        }
        const auto result = UserService(context.database).profile(tokenUserId);
        if (!result.ok) return serviceFailure(message, result.errorCode);
        return Response::success(message.requestId, {
            {QStringLiteral("userId"), result.userId},
            {QStringLiteral("phoneMasked"), result.phoneMasked},
            {QStringLiteral("nickname"), result.nickname},
            {QStringLiteral("balanceYuan"), result.balanceYuan.toString()},
            {QStringLiteral("status"), result.status},
            {QStringLiteral("avatarUrl"), result.avatarUrl}
        });
    });
    // 更新昵称/头像：昵称 trim 后 2~20 字符；avatarPath 为服务端本地路径（非完整上传链路）。
    registerHandler(QStringLiteral("USER_PROFILE_UPDATE"), [](const Message &message,
                                                                RequestContext &context) {
        // M1-02：只能改本人资料；userId 字段忽略，一律以令牌归属为准。
        Response unavailable;
        qint64 tokenUserId = 0;
        if (!userReady(message, context, &unavailable, &tokenUserId)) return unavailable;
        const QJsonValue nickname = message.payload.value(QStringLiteral("nickname"));
        const QJsonValue avatarPath = message.payload.value(QStringLiteral("avatarPath"));
        if (!nickname.isString()
            || (!avatarPath.isUndefined() && !avatarPath.isString())) {
            return serviceFailure(message, QStringLiteral("PARAM_INVALID"));
        }
        const auto result = WalletService(context.database).updateProfile(
            tokenUserId, nickname.toString(), avatarPath.toString());
        if (!result.ok) return serviceFailure(message, result.errorCode);
        return Response::success(message.requestId,
                                 {{QStringLiteral("userId"), tokenUserId},
                                  {QStringLiteral("nickname"), nickname.toString().trimmed()}});
    });
    // 查询钱包余额（两位小数字符串输出）。
    registerHandler(QStringLiteral("WALLET_BALANCE"), [](const Message &message,
                                                           RequestContext &context) {
        // M1-02：余额只能查本人；payload.userId 必须与令牌一致。
        Response unavailable;
        qint64 tokenUserId = 0;
        if (!userReady(message, context, &unavailable, &tokenUserId)) return unavailable;
        const QJsonValue requestedId = message.payload.value(QStringLiteral("userId"));
        if (!requestedId.isUndefined()
            && requestedId.toInteger() != tokenUserId) {
            return serviceFailure(message, QStringLiteral("AUTH_INVALID"));
        }
        const auto result = UserService(context.database).profile(tokenUserId);
        if (!result.ok) return serviceFailure(message, result.errorCode);
        return Response::success(message.requestId, {
            {QStringLiteral("userId"), result.userId},
            {QStringLiteral("balanceYuan"), result.balanceYuan.toString()}
        });
    });
    // —— 站点 ——
    // 附近站点：先校验经纬度范围（纬度±90、经度±180），再由 StationService 做
    // Haversine 距离计算与升序排序，输出含空闲桩数与球面距离（公里）。
    registerHandler(QStringLiteral("STATION_NEARBY"), [](const Message &message,
                                                          RequestContext &context) {
        Response unavailable;
        if (!databaseReady(message, context, &unavailable)) return unavailable;
        const QJsonValue latitude = message.payload.value(QStringLiteral("latitude"));
        const QJsonValue longitude = message.payload.value(QStringLiteral("longitude"));
        if (!latitude.isDouble() || !longitude.isDouble()
            || !std::isfinite(latitude.toDouble()) || latitude.toDouble() < -90.0
            || latitude.toDouble() > 90.0 || !std::isfinite(longitude.toDouble())
            || longitude.toDouble() < -180.0 || longitude.toDouble() > 180.0) {
            return serviceFailure(message, QStringLiteral("PARAM_INVALID"));
        }
        StationService service(context.database);
        QJsonArray stations;
        for (const auto &station : service.nearbyStations(
                 {latitude.toDouble(), longitude.toDouble()})) {
            stations.append(QJsonObject{
                {QStringLiteral("stationId"), station.stationId},
                {QStringLiteral("name"), station.name},
                {QStringLiteral("address"), station.address},
                {QStringLiteral("latitude"), station.latitude},
                {QStringLiteral("longitude"), station.longitude},
                {QStringLiteral("priceYuanPerKwh"), station.priceYuanPerKwh.toString()},
                {QStringLiteral("totalPiles"), station.totalPiles},
                {QStringLiteral("availablePiles"), station.availablePiles},
                {QStringLiteral("distanceKm"), station.distanceKm}
            });
        }
        return Response::success(message.requestId,
                                 {{QStringLiteral("stations"), stations}});
    });
    // 站点详情：站点信息 + 所属电桩列表（编号/类型/额定功率/状态/最近心跳）。
    registerHandler(QStringLiteral("STATION_DETAIL"), [](const Message &message,
                                                           RequestContext &context) {
        Response unavailable;
        if (!databaseReady(message, context, &unavailable)) return unavailable;
        QString error;
        const auto detail = StationService(context.database).stationDetail(
            message.payload.value(QStringLiteral("stationId")).toInteger(), &error);
        if (!detail) return serviceFailure(message, error);
        QJsonArray piles;
        for (const auto &pile : detail->piles) {
            piles.append(QJsonObject{{QStringLiteral("pileId"), pile.pileId},
                                     {QStringLiteral("serial"), pile.serial},
                                     {QStringLiteral("chargeType"), pile.chargeType},
                                     {QStringLiteral("ratedPowerKw"), pile.ratedPowerKw},
                                     {QStringLiteral("status"), pile.status},
                                     {QStringLiteral("lastHeartbeatAt"), pile.lastHeartbeatAt}});
        }
        return Response::success(message.requestId, {
            {QStringLiteral("stationId"), detail->stationId},
            {QStringLiteral("name"), detail->name},
            {QStringLiteral("address"), detail->address},
            {QStringLiteral("latitude"), detail->latitude},
            {QStringLiteral("longitude"), detail->longitude},
            {QStringLiteral("priceYuanPerKwh"), detail->priceYuanPerKwh.toString()},
            {QStringLiteral("status"), detail->status},
            {QStringLiteral("piles"), piles}
        });
    });
    // —— 订单 ——
    // 预约电桩：并发安全核心动作（BEGIN IMMEDIATE + 条件更新 + 部分唯一索引兜底）；
    // requestId 兼做预约防重；15 分钟预约期限由服务层写入 reserved_until。
    registerHandler(QStringLiteral("ORDER_RESERVE"), [](const Message &message,
                                                         RequestContext &context) {
        // M1-02：预约以令牌身份为准（冻结用户在服务层拒绝新预约）。
        Response unavailable;
        qint64 tokenUserId = 0;
        bool restricted = false;
        if (!userReady(message, context, &unavailable, &tokenUserId, &restricted)) {
            return unavailable;
        }
        if (restricted) return serviceFailure(message, QStringLiteral("USER_FROZEN"));
        const QJsonValue requestedId = message.payload.value(QStringLiteral("userId"));
        if (!requestedId.isUndefined()
            && requestedId.toInteger() != tokenUserId) {
            return serviceFailure(message, QStringLiteral("AUTH_INVALID"));
        }
        OrderService service(context.database);
        const auto result = service.reserve(
            tokenUserId,
            message.payload.value(QStringLiteral("pileId")).toInteger(), message.requestId);
        if (!result.ok) return serviceFailure(message, result.errorCode);
        return Response::success(message.requestId,
                                 {{QStringLiteral("orderId"), result.orderId}});
    });
    // 工厂：ORDER_START / ORDER_STOP / ORDER_CANCEL 三个动作共用同一响应组装，
    // 只注入不同的 OrderService 成员函数指针，避免三份重复代码。
    // M1-02：START/STOP/CANCEL 全部先过用户闸门，再以令牌归属校验订单。
    const auto orderAction = [this](const QString &action,
                                    OrderResult (OrderService::*operation)
                                        (const QString &, qint64)) {
        registerHandler(action, [operation](const Message &message, RequestContext &context) {
            Response unavailable;
            qint64 tokenUserId = 0;
            if (!userReady(message, context, &unavailable, &tokenUserId)) return unavailable;
            OrderService service(context.database);
            const auto result = (service.*operation)(
                message.payload.value(QStringLiteral("orderId")).toString(), tokenUserId);
            if (!result.ok) return serviceFailure(message, result.errorCode);
            return Response::success(message.requestId, {
                {QStringLiteral("orderId"), result.orderId},
                {QStringLiteral("energyKwh"), result.energyKwh.toString()},
                {QStringLiteral("unitPriceYuanPerKwh"),
                 result.unitPriceYuanPerKwh.toString()},
                {QStringLiteral("amountYuan"), result.amountYuan.toString()}
            });
        });
    };
    orderAction(QStringLiteral("ORDER_START"),
                static_cast<OrderResult (OrderService::*)(const QString &, qint64)>(
                    &OrderService::start));
    orderAction(QStringLiteral("ORDER_STOP"),
                static_cast<OrderResult (OrderService::*)(const QString &, qint64)>(
                    &OrderService::stop));
    orderAction(QStringLiteral("ORDER_CANCEL"),
                static_cast<OrderResult (OrderService::*)(const QString &, qint64)>(
                    &OrderService::cancel));
    // M1-03（01 文档 4.2）：正式模式关闭网络 ORDER_TELEMETRY 写入口。
    // 唯一计量驱动是 PILE_TELEMETRY（设备累计读数）；用户/模拟器不得直接改订单电量。
    // 既有服务层测试经内部方法调用不受影响；网络层一律 AUTH_INVALID。
    registerHandler(QStringLiteral("ORDER_TELEMETRY"), [](const Message &message,
                                                           RequestContext &) {
        return Response::failure(message.requestId, ErrorCode::AuthInvalid,
                                  QStringLiteral("AUTH_INVALID"));
    });
    // 查询用户当前活动订单（RESERVED/CHARGING/PENDING_PAYMENT）。
    registerHandler(QStringLiteral("ORDER_ACTIVE"), [](const Message &message,
                                                         RequestContext &context) {
        Response unavailable;
        qint64 tokenUserId = 0;
        if (!userReady(message, context, &unavailable, &tokenUserId)) return unavailable;
        const QJsonValue requestedId = message.payload.value(QStringLiteral("userId"));
        if (!requestedId.isUndefined()
            && requestedId.toInteger() != tokenUserId) {
            return serviceFailure(message, QStringLiteral("AUTH_INVALID"));
        }
        const auto result = OrderService(context.database).active(tokenUserId);
        if (!result.ok) return serviceFailure(message, result.errorCode);
        return Response::success(message.requestId, {
            {QStringLiteral("order"), QJsonObject::fromVariantMap(result.order)}
        });
    });
    // 查询单个订单详情。
    registerHandler(QStringLiteral("ORDER_DETAIL"), [](const Message &message,
                                                         RequestContext &context) {
        Response unavailable;
        qint64 tokenUserId = 0;
        if (!userReady(message, context, &unavailable, &tokenUserId)) return unavailable;
        const auto result = OrderService(context.database).detail(
            message.payload.value(QStringLiteral("orderId")).toString(), tokenUserId);
        if (!result.ok) return serviceFailure(message, result.errorCode);
        return Response::success(message.requestId,
                                 QJsonObject::fromVariantMap(result.order));
    });
    // 分页查询历史订单（终态 FINISHED/CANCELED/EXPIRED），limit 默认 20、上限 100。
    registerHandler(QStringLiteral("ORDER_HISTORY"), [](const Message &message,
                                                          RequestContext &context) {
        Response unavailable;
        qint64 tokenUserId = 0;
        if (!userReady(message, context, &unavailable, &tokenUserId)) return unavailable;
        const QJsonValue requestedId = message.payload.value(QStringLiteral("userId"));
        if (!requestedId.isUndefined()
            && requestedId.toInteger() != tokenUserId) {
            return serviceFailure(message, QStringLiteral("AUTH_INVALID"));
        }
        const auto result = OrderService(context.database).history(
            tokenUserId,
            message.payload.value(QStringLiteral("limit")).toInt(20),
            message.payload.value(QStringLiteral("offset")).toInt());
        if (!result.ok) return serviceFailure(message, result.errorCode);
        return Response::success(message.requestId,
                                 {{QStringLiteral("rows"), rowsToJson(result.rows)}});
    });
    // —— 设备遥测 ——
    // 电桩遥测上报：路由层参数校验最严格的动作——功率 0~1000000 且必须为整数等物理量约束、
    // recordedAt 必须是带时区的 ISO 时间（LocalTime 视为非法）；业务规则在 TelemetryService::record()。
    registerHandler(QStringLiteral("PILE_TELEMETRY"), [](const Message &message,
                                                           RequestContext &context) {
        Response unavailable;
        if (!databaseReady(message, context, &unavailable)) return unavailable;
        const QJsonValue pileId = message.payload.value(QStringLiteral("pileId"));
        const QJsonValue recordedAt = message.payload.value(QStringLiteral("recordedAt"));
        const QJsonValue temperature = message.payload.value(QStringLiteral("temperatureC"));
        const QJsonValue voltage = message.payload.value(QStringLiteral("voltageV"));
        const QJsonValue current = message.payload.value(QStringLiteral("currentA"));
        const QJsonValue power = message.payload.value(QStringLiteral("powerW"));
        const auto energy = decimalField(message.payload, QStringLiteral("energyKwh"));
        const double rawPower = power.toDouble(-1.0);
        if (!pileId.isDouble() || pileId.toInteger() <= 0 || !recordedAt.isString()
            || !temperature.isDouble() || !voltage.isDouble() || !current.isDouble()
            || !power.isDouble() || !std::isfinite(rawPower) || rawPower < 0.0
            || rawPower > 1000000.0 || std::floor(rawPower) != rawPower || !energy) {
            return serviceFailure(message, QStringLiteral("PARAM_INVALID"));
        }
        const QDateTime timestamp = QDateTime::fromString(recordedAt.toString(), Qt::ISODate);
        if (!timestamp.isValid() || timestamp.timeSpec() == Qt::LocalTime) {
            return serviceFailure(message, QStringLiteral("PARAM_INVALID"));
        }
        TelemetryService service(context.database);
        const auto result = service.record({
            pileId.toInteger(), timestamp, temperature.toDouble(), voltage.toDouble(),
            current.toDouble(), static_cast<int>(rawPower), *energy,
            message.payload.value(QStringLiteral("dataMode")).toString(
                QStringLiteral("UNKNOWN"))
        });
        if (!result.ok) return serviceFailure(message, result.errorCode);
        return Response::success(message.requestId, {
            {QStringLiteral("pileId"), pileId.toInteger()},
            {QStringLiteral("recordedAt"), timestamp.toUTC().toString(Qt::ISODate)},
            {QStringLiteral("energyKwh"), energy->toString()},
            {QStringLiteral("duplicate"), result.duplicate},
            {QStringLiteral("orderId"), result.orderId},
            {QStringLiteral("orderEnergyKwh"), result.orderEnergyKwh.toString()}
        });
    });
    // —— 钱包 ——
    // 支付待支付订单：requestId 兼做支付防重；扣款/订单完成/释放电桩在同一事务内。
    registerHandler(QStringLiteral("WALLET_PAY"), [](const Message &message,
                                                      RequestContext &context) {
        Response unavailable;
        qint64 tokenUserId = 0;
        if (!userReady(message, context, &unavailable, &tokenUserId)) return unavailable;
        WalletService service(context.database);
        const auto result = service.pay(
            message.payload.value(QStringLiteral("orderId")).toString(), message.requestId,
            tokenUserId);
        return result.ok ? Response::success(message.requestId)
                         : serviceFailure(message, result.errorCode);
    });
    // 钱包充值：金额 1.00~10000.00 元的两位小数字符串，requestId 兼做充值防重。
    registerHandler(QStringLiteral("WALLET_RECHARGE"), [](const Message &message,
                                                           RequestContext &context) {
        // M1-02：充值只能给本人；payload.userId 忽略，以令牌为准。
        Response unavailable;
        qint64 tokenUserId = 0;
        if (!userReady(message, context, &unavailable, &tokenUserId)) return unavailable;
        const auto amountYuan = decimalField(message.payload, QStringLiteral("amountYuan"));
        if (!amountYuan) return serviceFailure(message, QStringLiteral("PARAM_INVALID"));
        WalletService service(context.database);
        const auto result = service.recharge(
            tokenUserId, *amountYuan, message.requestId);
        return result.ok ? Response::success(message.requestId)
                         : serviceFailure(message, result.errorCode);
    });
    // —— 请求状态（M1-02，01文档4.5）——
    // 不确定结果核对：本人 userToken + targetRequestId，仅返回该用户名下
    // 充值/支付防重记录的 COMMITTED / NOT_FOUND。不通过余额差猜测；查询结果不等于
    // "该请求永远不会到达"，确认重试仍须复用相同 requestId。
    registerHandler(QStringLiteral("REQUEST_STATUS"), [](const Message &message,
                                                          RequestContext &context) {
        Response unavailable;
        qint64 tokenUserId = 0;
        if (!userReady(message, context, &unavailable, &tokenUserId)) return unavailable;
        const QJsonValue target = message.payload.value(QStringLiteral("targetRequestId"));
        if (!target.isString() || target.toString().trimmed().isEmpty()) {
            return serviceFailure(message, QStringLiteral("PARAM_INVALID"));
        }
        QSqlQuery lookup(context.database);
        // 仅认 RECHARGE/PAYMENT 两类本人防重记录（LOGIN/RESERVE 不属于钱包不确定结果）。
        lookup.prepare(QStringLiteral(
            "SELECT d.action, w.user_id FROM request_dedup d "
            "JOIN wallet_transaction w ON w.request_id=d.request_id "
            "WHERE d.request_id=? AND d.action IN ('RECHARGE','PAYMENT') "
            "AND w.user_id=?"));
        lookup.addBindValue(target.toString().trimmed());
        lookup.addBindValue(tokenUserId);
        if (!lookup.exec()) {
            return serviceFailure(message, QStringLiteral("DATABASE_ERROR"));
        }
        const QString state = lookup.next()
            ? QStringLiteral("COMMITTED") : QStringLiteral("NOT_FOUND");
        return Response::success(message.requestId, {
            {QStringLiteral("targetRequestId"), target.toString().trimmed()},
            {QStringLiteral("state"), state}
        });
    });
    // —— 管理员 ——
    // 登录：用户名+密码（PBKDF2 派生值常时比较），成功返回 32 字节随机 Token（8 小时有效）。
    // 这是唯一不需要 adminToken 的管理动作——Token 正是由它颁发。
    registerHandler(QStringLiteral("ADMIN_LOGIN"), [](const Message &message,
                                                       RequestContext &context) {
        Response unavailable;
        if (!databaseReady(message, context, &unavailable)) return unavailable;
        AdminService service(context.database);
        const auto result = service.login(
            message.payload.value(QStringLiteral("username")).toString(),
            message.payload.value(QStringLiteral("password")).toString());
        return result.ok ? Response::success(message.requestId,
                            {{QStringLiteral("adminToken"), result.token}})
                         : serviceFailure(message, result.errorCode);
    });
    // —— 以下管理动作均需 adminToken（adminReady 前置）——
    // 累计营收：已完成订单金额 DecimalValue 累加（充值不算充电营收）。
    registerHandler(QStringLiteral("ADMIN_REVENUE"), [](const Message &message,
                                                         RequestContext &context) {
        Response unavailable;
        if (!adminReady(message, context, &unavailable)) return unavailable;
        AdminService service(context.database);
        return Response::success(message.requestId, {
            {QStringLiteral("totalRevenueYuan"), service.totalRevenueYuan().toString()}
        });
    });
    // 营收趋势：按 UTC 日口径聚合近 N 天营收（可按站点过滤）。
    registerHandler(QStringLiteral("ADMIN_REVENUE_TREND"), [](const Message &message,
                                                               RequestContext &context) {
        Response unavailable;
        if (!adminReady(message, context, &unavailable)) return unavailable;
        const auto points = AdminService(context.database).revenueTrend(
            message.payload.value(QStringLiteral("days")).toInt(7),
            message.payload.value(QStringLiteral("stationId")).toInteger());
        QJsonArray values;
        for (const auto &point : points) {
            values.append(QJsonObject{
                {QStringLiteral("date"), point.date},
                {QStringLiteral("revenueYuan"), point.revenueYuan.toString()}
            });
        }
        return Response::success(message.requestId, {{QStringLiteral("points"), values}});
    });
    // 运营总览：今日/本月营收、服务次数、订单数、电桩利用率、在线率、待处理告警数，
    // 外加 S1-B 总览页所需的分布计数、24h 负荷序列与最新告警/工单/异常桩/最近订单清单。
    registerHandler(QStringLiteral("ADMIN_OVERVIEW"), [](const Message &message,
                                                          RequestContext &context) {
        Response unavailable;
        if (!adminReady(message, context, &unavailable)) return unavailable;
        const auto value = AdminService(context.database).overview();
        QJsonObject data;
        data.insert(QStringLiteral("todayRevenueYuan"), value.todayRevenueYuan.toString());
        data.insert(QStringLiteral("monthRevenueYuan"), value.monthRevenueYuan.toString());
        data.insert(QStringLiteral("todayServiceCount"), value.todayServiceCount);
        data.insert(QStringLiteral("todayOrderCount"), value.todayOrderCount);
        data.insert(QStringLiteral("utilizationRate"), value.utilizationRate);
        data.insert(QStringLiteral("onlineRate"), value.onlineRate);
        data.insert(QStringLiteral("pendingAlarmCount"), value.pendingAlarmCount);
        data.insert(QStringLiteral("pileStatusCounts"),
                    QJsonObject::fromVariantMap(value.pileStatusCounts));
        data.insert(QStringLiteral("orderStatusCounts"),
                    QJsonObject::fromVariantMap(value.orderStatusCounts));
        data.insert(QStringLiteral("loadSeries24h"),
                    QJsonArray::fromVariantList(value.loadSeries24h));
        data.insert(QStringLiteral("latestAlarms"),
                    QJsonArray::fromVariantList(value.latestAlarms));
        data.insert(QStringLiteral("openTickets"),
                    QJsonArray::fromVariantList(value.openTickets));
        data.insert(QStringLiteral("abnormalPiles"),
                    QJsonArray::fromVariantList(value.abnormalPiles));
        data.insert(QStringLiteral("recentOrders"),
                    QJsonArray::fromVariantList(value.recentOrders));
        return Response::success(message.requestId, data);
    });
    // S1-B 营收页汇总：区间（UTC 日期闭区间）内已支付订单的指标卡、站点排行与小时分布。
    registerHandler(QStringLiteral("ADMIN_REVENUE_SUMMARY"), [](const Message &message,
                                                                 RequestContext &context) {
        Response unavailable;
        if (!adminReady(message, context, &unavailable)) return unavailable;
        const QString fromDate =
            message.payload.value(QStringLiteral("fromDate")).toString();
        const QString toDate = message.payload.value(QStringLiteral("toDate")).toString();
        const auto datePattern = QRegularExpression(
            QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"));
        if (!datePattern.match(fromDate).hasMatch()
            || !datePattern.match(toDate).hasMatch() || fromDate > toDate) {
            return serviceFailure(message, QStringLiteral("PARAM_INVALID"));
        }
        const auto value = AdminService(context.database).revenueSummary(fromDate, toDate);
        QJsonObject data;
        data.insert(QStringLiteral("fromDate"), fromDate);
        data.insert(QStringLiteral("toDate"), toDate);
        data.insert(QStringLiteral("totalRevenueYuan"), value.totalRevenueYuan.toString());
        data.insert(QStringLiteral("orderCount"), value.orderCount);
        data.insert(QStringLiteral("totalEnergyKwh"), value.totalEnergyKwh.toString());
        data.insert(QStringLiteral("avgOrderYuan"), value.avgOrderYuan.toString());
        data.insert(QStringLiteral("avgPriceYuan"), value.avgPriceYuan.toString());
        data.insert(QStringLiteral("stationRanking"),
                    QJsonArray::fromVariantList(value.stationRanking));
        data.insert(QStringLiteral("hourHistogram"),
                    QJsonArray::fromVariantList(value.hourHistogram));
        return Response::success(message.requestId, data);
    });
    // —— 运营列表查询组 ——站点/电桩/订单/用户/操作日志/告警/工单七个动作，均支持分页筛选。
    registerHandler(QStringLiteral("ADMIN_STATIONS"), [](const Message &message,
                                                          RequestContext &context) {
        Response unavailable;
        if (!adminReady(message, context, &unavailable)) return unavailable;
        const auto rows = AdminService(context.database).stations(
            message.payload.value(QStringLiteral("search")).toString(),
            message.payload.value(QStringLiteral("limit")).toInt(100),
            message.payload.value(QStringLiteral("offset")).toInt());
        return Response::success(message.requestId, {{QStringLiteral("rows"), rowsToJson(rows)}});
    });
    registerHandler(QStringLiteral("ADMIN_PILES"), [](const Message &message,
                                                       RequestContext &context) {
        Response unavailable;
        if (!adminReady(message, context, &unavailable)) return unavailable;
        const auto rows = AdminService(context.database).piles(
            message.payload.value(QStringLiteral("stationId")).toInteger(),
            message.payload.value(QStringLiteral("status")).toString(),
            message.payload.value(QStringLiteral("limit")).toInt(100),
            message.payload.value(QStringLiteral("offset")).toInt());
        return Response::success(message.requestId, {{QStringLiteral("rows"), rowsToJson(rows)}});
    });
    registerHandler(QStringLiteral("ADMIN_ORDERS"), [](const Message &message,
                                                        RequestContext &context) {
        Response unavailable;
        if (!adminReady(message, context, &unavailable)) return unavailable;
        const auto rows = AdminService(context.database).orders(
            message.payload.value(QStringLiteral("status")).toString(),
            message.payload.value(QStringLiteral("limit")).toInt(100),
            message.payload.value(QStringLiteral("offset")).toInt());
        return Response::success(message.requestId, {{QStringLiteral("rows"), rowsToJson(rows)}});
    });
    registerHandler(QStringLiteral("ADMIN_USERS"), [](const Message &message,
                                                       RequestContext &context) {
        Response unavailable;
        if (!adminReady(message, context, &unavailable)) return unavailable;
        const auto rows = AdminService(context.database).users(
            message.payload.value(QStringLiteral("status")).toString(),
            message.payload.value(QStringLiteral("search")).toString(),
            message.payload.value(QStringLiteral("limit")).toInt(100),
            message.payload.value(QStringLiteral("offset")).toInt());
        return Response::success(message.requestId, {{QStringLiteral("rows"), rowsToJson(rows)}});
    });
    registerHandler(QStringLiteral("ADMIN_LOGS"), [](const Message &message,
                                                      RequestContext &context) {
        Response unavailable;
        if (!adminReady(message, context, &unavailable)) return unavailable;
        const auto rows = AdminService(context.database).operationLogs(
            message.payload.value(QStringLiteral("action")).toString(),
            message.payload.value(QStringLiteral("limit")).toInt(100),
            message.payload.value(QStringLiteral("offset")).toInt());
        return Response::success(message.requestId, {{QStringLiteral("rows"), rowsToJson(rows)}});
    });
    registerHandler(QStringLiteral("ADMIN_ALARMS"), [](const Message &message,
                                                        RequestContext &context) {
        Response unavailable;
        if (!adminReady(message, context, &unavailable)) return unavailable;
        const auto rows = AdminService(context.database).alarms(
            message.payload.value(QStringLiteral("status")).toString(),
            message.payload.value(QStringLiteral("severity")).toString(),
            message.payload.value(QStringLiteral("limit")).toInt(100),
            message.payload.value(QStringLiteral("offset")).toInt());
        return Response::success(message.requestId,
                                 {{QStringLiteral("rows"), rowsToJson(rows)}});
    });
    registerHandler(QStringLiteral("ADMIN_TICKETS"), [](const Message &message,
                                                         RequestContext &context) {
        Response unavailable;
        if (!adminReady(message, context, &unavailable)) return unavailable;
        const auto rows = AdminService(context.database).tickets(
            message.payload.value(QStringLiteral("status")).toString(),
            message.payload.value(QStringLiteral("limit")).toInt(100),
            message.payload.value(QStringLiteral("offset")).toInt());
        return Response::success(message.requestId,
                                 {{QStringLiteral("rows"), rowsToJson(rows)}});
    });
    // —— 运营修改组：以下写操作都会以认证到的 username 写入 operation_log 审计 ——
    // 用户冻结/解冻。
    registerHandler(QStringLiteral("ADMIN_USER_FREEZE"), [](const Message &message,
                                                             RequestContext &context) {
        Response unavailable;
        QString username;
        if (!adminReady(message, context, &unavailable, &username)) return unavailable;
        const auto result = AdminService(context.database).setUserFrozen(
            message.payload.value(QStringLiteral("userId")).toInteger(),
            message.payload.value(QStringLiteral("frozen")).toBool(),
            message.payload.value(QStringLiteral("reason")).toString(),
            username);
        return result.ok ? Response::success(message.requestId)
                         : serviceFailure(message, result.errorCode);
    });
    // —— 运维处置组：告警确认、工单开始/解决/关闭（AlarmService 内含状态机守卫）——
    registerHandler(QStringLiteral("ADMIN_ALARM_ACK"), [](const Message &message,
                                                            RequestContext &context) {
        Response unavailable;
        QString username;
        if (!adminReady(message, context, &unavailable, &username)) return unavailable;
        const auto result = AlarmService(context.database).acknowledgeAlarm(
            message.payload.value(QStringLiteral("alarmId")).toInteger(), username);
        return result.ok ? Response::success(message.requestId)
                         : serviceFailure(message, result.errorCode);
    });
    registerHandler(QStringLiteral("ADMIN_TICKET_START"), [](const Message &message,
                                                               RequestContext &context) {
        Response unavailable;
        QString username;
        if (!adminReady(message, context, &unavailable, &username)) return unavailable;
        const auto result = AlarmService(context.database).startTicket(
            message.payload.value(QStringLiteral("ticketId")).toInteger(),
            message.payload.value(QStringLiteral("assignee")).toString(), username);
        return result.ok ? Response::success(message.requestId)
                         : serviceFailure(message, result.errorCode);
    });
    registerHandler(QStringLiteral("ADMIN_TICKET_RESOLVE"), [](const Message &message,
                                                                 RequestContext &context) {
        Response unavailable;
        QString username;
        if (!adminReady(message, context, &unavailable, &username)) return unavailable;
        const auto result = AlarmService(context.database).resolveTicket(
            message.payload.value(QStringLiteral("ticketId")).toInteger(),
            message.payload.value(QStringLiteral("notes")).toString(), username);
        return result.ok ? Response::success(message.requestId)
                         : serviceFailure(message, result.errorCode);
    });
    registerHandler(QStringLiteral("ADMIN_TICKET_CLOSE"), [](const Message &message,
                                                               RequestContext &context) {
        Response unavailable;
        QString username;
        if (!adminReady(message, context, &unavailable, &username)) return unavailable;
        const auto result = AlarmService(context.database).closeTicket(
            message.payload.value(QStringLiteral("ticketId")).toInteger(), username);
        return result.ok ? Response::success(message.requestId)
                         : serviceFailure(message, result.errorCode);
    });
    // 新建站点：价格经 decimalField（拒绝负数）、坐标必须为数值；
    // 站点、初始电桩与审计日志在同一事务内落库。
    registerHandler(QStringLiteral("ADMIN_STATION_CREATE"), [](const Message &message,
                                                                RequestContext &context) {
        Response unavailable;
        QString username;
        if (!adminReady(message, context, &unavailable, &username)) return unavailable;
        const auto priceYuanPerKwh = decimalField(
            message.payload, QStringLiteral("priceYuanPerKwh"));
        if (!priceYuanPerKwh
            || !message.payload.value(QStringLiteral("latitude")).isDouble()
            || !message.payload.value(QStringLiteral("longitude")).isDouble()) {
            return serviceFailure(message, QStringLiteral("PARAM_INVALID"));
        }
        QList<NewPile> piles;
        for (const auto &entry : message.payload.value(QStringLiteral("piles")).toArray()) {
            const auto object = entry.toObject();
            piles.append({object.value(QStringLiteral("serial")).toString(),
                          object.value(QStringLiteral("chargeType")).toString(),
                          object.value(QStringLiteral("ratedPowerKw")).toDouble()});
        }
        const auto result = AdminService(context.database).createStation(
            message.payload.value(QStringLiteral("name")).toString(),
            message.payload.value(QStringLiteral("address")).toString(),
            message.payload.value(QStringLiteral("latitude")).toDouble(),
            message.payload.value(QStringLiteral("longitude")).toDouble(),
            *priceYuanPerKwh, piles, username);
        return result.ok ? Response::success(message.requestId,
                            {{QStringLiteral("stationId"), result.stationId}})
                         : serviceFailure(message, result.errorCode);
    });
    // 修改站点信息（名称/地址/坐标/单价/营业状态）。
    registerHandler(QStringLiteral("ADMIN_STATION_UPDATE"), [](const Message &message,
                                                                RequestContext &context) {
        Response unavailable;
        QString username;
        if (!adminReady(message, context, &unavailable, &username)) return unavailable;
        const auto price = decimalField(message.payload,
                                        QStringLiteral("priceYuanPerKwh"));
        if (!price || !message.payload.value(QStringLiteral("latitude")).isDouble()
            || !message.payload.value(QStringLiteral("longitude")).isDouble()) {
            return serviceFailure(message, QStringLiteral("PARAM_INVALID"));
        }
        const auto result = AdminService(context.database).updateStation(
            message.payload.value(QStringLiteral("stationId")).toInteger(),
            message.payload.value(QStringLiteral("name")).toString(),
            message.payload.value(QStringLiteral("address")).toString(),
            message.payload.value(QStringLiteral("latitude")).toDouble(),
            message.payload.value(QStringLiteral("longitude")).toDouble(),
            *price, message.payload.value(QStringLiteral("status")).toString(), username);
        return result.ok ? Response::success(message.requestId,
                            {{QStringLiteral("stationId"), result.stationId}})
                         : serviceFailure(message, result.errorCode);
    });
    // 人工设置电桩状态：只允许 IDLE/FAULT/OFFLINE；处于预约/充电状态的桩会被服务层拒绝。
    registerHandler(QStringLiteral("ADMIN_PILE_SET_STATUS"), [](const Message &message,
                                                                 RequestContext &context) {
        Response unavailable;
        QString username;
        if (!adminReady(message, context, &unavailable, &username)) return unavailable;
        const qint64 pileId = message.payload.value(QStringLiteral("pileId")).toInteger();
        const QString status = message.payload.value(QStringLiteral("status")).toString();
        const auto result = AdminService(context.database).setPileStatus(
            pileId, status, message.payload.value(QStringLiteral("reason")).toString(),
            username);
        return result.ok ? Response::success(message.requestId,
                            {{QStringLiteral("pileId"), pileId},
                             {QStringLiteral("status"), status}})
                         : serviceFailure(message, result.errorCode);
    });
    // 模拟重启故障/离线电桩：按活动订单恢复为 RESERVED/CHARGING/IDLE 并返回 commandId。
    // 答辩口径：这是服务端模拟恢复，并未向真实硬件下发重启命令并等待回执。
    registerHandler(QStringLiteral("ADMIN_PILE_RESTART"), [](const Message &message,
                                                              RequestContext &context) {
        Response unavailable;
        QString username;
        if (!adminReady(message, context, &unavailable, &username)) return unavailable;
        const qint64 pileId = message.payload.value(QStringLiteral("pileId")).toInteger();
        const auto result = AdminService(context.database).restartPile(pileId, username);
        return result.ok ? Response::success(message.requestId,
                            {{QStringLiteral("pileId"), pileId},
                             {QStringLiteral("status"), result.status},
                             {QStringLiteral("commandId"), result.commandId}})
                         : serviceFailure(message, result.errorCode);
    });
}

// 注册表写入：动作名 → 处理函数。
void RequestRouter::registerHandler(const QString &action, Handler handler)
{
    handlers_.insert(action, std::move(handler));
}

// 分发：按动作名查表调用；未知动作返回 PARAM_INVALID；任何处理异常兜底为 PROTOCOL_ERROR，
// 保证业务层抛出的异常不会击穿连接线程（连接线程无 try/catch，靠这里兜住）。
Response RequestRouter::route(const Message &message, RequestContext &context) const
{
    const auto handler = handlers_.constFind(message.action);
    if (handler == handlers_.constEnd()) {
        return Response::failure(message.requestId, ErrorCode::ParamInvalid,
                                 QStringLiteral("unsupported action: %1").arg(message.action));
    }
    try {
        Response response = (*handler)(message, context);
        // S1-A：成功响应统一注入联动元数据（写=operationId/changeVersion/serverTimeUtc；
        // 查=snapshotVersion/snapshotAtUtc），客户端据此做刷新与旧快照丢弃。
        if (response.ok) {
            stampLinkageMetadata(message, &response, context);
        }
        return response;
    } catch (const std::exception &) {
        return Response::failure(message.requestId, ErrorCode::ProtocolError,
                                 QStringLiteral("request processing failed"));
    }
}
