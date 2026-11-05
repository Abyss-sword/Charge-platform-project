#pragma once

#include "shared/protocol/error_code.h"

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

#include <optional>

// 请求消息封装与解析校验（公共协议层）。
// 版本口径：封装层 version 当前固定要求 "1.0"；协议文档 v2.0 的变化主要体现在业务字段
// （金额、电量等统一为两位小数的十进制字符串），并不是封装版本号本身升级。
struct Message
{
    // 请求字段：版本 / 请求编号（关联响应；登录、预约、充值、支付兼做防重）/
    // 时间戳 / 动作名 / 发起者 / 业务参数对象。
    QString version;
    QString requestId;
    qint64 timestamp = 0;
    QString action;
    QJsonObject actor;
    QJsonObject payload;

    // 从 JSON 解析并校验请求封装：版本必须是 "1.0"、requestId 与 action 非空、payload 必须是对象；
    // 校验失败返回 nullopt 并通过 error 输出原因，网络层据此返回协议错误。
    static std::optional<Message> fromJson(const QJsonObject &object, QString *error = nullptr)
    {
        const auto fail = [error](const QString &message) -> std::optional<Message> {
            if (error) {
                *error = message;
            }
            return std::nullopt;
        };

        if (object.value(QStringLiteral("version")).toString() != QStringLiteral("1.0")) {
            return fail(QStringLiteral("version must be 1.0"));
        }
        if (object.value(QStringLiteral("requestId")).toString().isEmpty()) {
            return fail(QStringLiteral("requestId is required"));
        }
        if (object.value(QStringLiteral("action")).toString().isEmpty()) {
            return fail(QStringLiteral("action is required"));
        }
        if (!object.value(QStringLiteral("payload")).isObject()) {
            return fail(QStringLiteral("payload must be an object"));
        }

        Message message;
        message.version = object.value(QStringLiteral("version")).toString();
        message.requestId = object.value(QStringLiteral("requestId")).toString();
        message.timestamp = object.value(QStringLiteral("timestamp")).toInteger();
        message.action = object.value(QStringLiteral("action")).toString();
        message.actor = object.value(QStringLiteral("actor")).toObject();
        message.payload = object.value(QStringLiteral("payload")).toObject();
        return message;
    }
};

// 统一响应封装：所有动作的返回结构一致，客户端按 requestId 关联请求、按固定错误码判断失败原因。
struct Response
{
    // 回带原请求的 requestId；code 为稳定错误码（见 error_code.h），不依赖中文提示文本。
    QString requestId;
    bool ok = false;
    ErrorCode code = ErrorCode::ProtocolError;
    QString message;
    QJsonObject data;

    // 成功响应。
    static Response success(const QString &requestId, const QJsonObject &data = {})
    {
        return {requestId, true, ErrorCode::Ok, QStringLiteral("success"), data};
    }

    // 失败响应：携带错误码与人类可读说明。
    static Response failure(const QString &requestId, ErrorCode code, const QString &message)
    {
        return {requestId, false, code, message, {}};
    }

    // 序列化：仅失败时额外嵌套 error{code,message} 对象，客户端可统一解析。
    QJsonObject toJson() const
    {
        QJsonObject object{
            {QStringLiteral("requestId"), requestId},
            {QStringLiteral("ok"), ok},
            {QStringLiteral("code"), errorCodeName(code)},
            {QStringLiteral("message"), message},
            {QStringLiteral("data"), data}
        };
        if (!ok) {
            object.insert(QStringLiteral("error"), QJsonObject{
                {QStringLiteral("code"), errorCodeName(code)},
                {QStringLiteral("message"), message}
            });
        }
        return object;
    }
};
