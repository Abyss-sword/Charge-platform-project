#pragma once

#include <QString>

// 统一错误码（公共协议层）：客户端以固定错误码判断失败原因（参数错误、余额不足、状态冲突等），
// 而不是解析中文提示文本；errorCodeName() 提供枚举到协议字符串名的稳定映射。
enum class ErrorCode {
    Ok,
    AuthInvalid,
    ParamInvalid,
    UserFrozen,
    UnfinishedOrder,
    PileUnavailable,
    OrderStateConflict,
    BalanceInsufficient,
    DuplicateRequest,
    MapUnavailable,
    DeviceTimeout,
    TelemetryOutOfOrder,
    MeterResetSuspected,
    StateConflict,
    DeviceNotRecovered,
    NotFound,
    DatabaseError,
    ModelUnavailable,
    ProtocolError
};

// 枚举 → 协议字符串名（如 BALANCE_INSUFFICIENT），写入响应的 code 与 error.code 字段。
inline QString errorCodeName(ErrorCode code)
{
    switch (code) {
    case ErrorCode::Ok: return QStringLiteral("OK");
    case ErrorCode::AuthInvalid: return QStringLiteral("AUTH_INVALID");
    case ErrorCode::ParamInvalid: return QStringLiteral("PARAM_INVALID");
    case ErrorCode::UserFrozen: return QStringLiteral("USER_FROZEN");
    case ErrorCode::UnfinishedOrder: return QStringLiteral("UNFINISHED_ORDER");
    case ErrorCode::PileUnavailable: return QStringLiteral("PILE_UNAVAILABLE");
    case ErrorCode::OrderStateConflict: return QStringLiteral("ORDER_STATE_CONFLICT");
    case ErrorCode::BalanceInsufficient: return QStringLiteral("BALANCE_INSUFFICIENT");
    case ErrorCode::DuplicateRequest: return QStringLiteral("DUPLICATE_REQUEST");
    case ErrorCode::MapUnavailable: return QStringLiteral("MAP_UNAVAILABLE");
    case ErrorCode::DeviceTimeout: return QStringLiteral("DEVICE_TIMEOUT");
    case ErrorCode::TelemetryOutOfOrder: return QStringLiteral("TELEMETRY_OUT_OF_ORDER");
    case ErrorCode::MeterResetSuspected: return QStringLiteral("METER_RESET_SUSPECTED");
    case ErrorCode::StateConflict: return QStringLiteral("STATE_CONFLICT");
    case ErrorCode::DeviceNotRecovered: return QStringLiteral("DEVICE_NOT_RECOVERED");
    case ErrorCode::NotFound: return QStringLiteral("NOT_FOUND");
    case ErrorCode::DatabaseError: return QStringLiteral("DATABASE_ERROR");
    case ErrorCode::ModelUnavailable: return QStringLiteral("MODEL_UNAVAILABLE");
    case ErrorCode::ProtocolError: return QStringLiteral("PROTOCOL_ERROR");
    }
    // 未覆盖的枚举值统一回落为 PROTOCOL_ERROR，保证响应始终带合法错误码。
    return QStringLiteral("PROTOCOL_ERROR");
}
