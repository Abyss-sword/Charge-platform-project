#pragma once

#include "shared/protocol/message.h"

#include <QHash>
#include <QSqlDatabase>

#include <functional>

// 请求路由上下文：把当前连接自己的数据库连接带给业务处理函数
// （每连接一个独立连接，业务层直接在此连接上执行 SQL）。
struct RequestContext
{
    QSqlDatabase database;
};

// RequestRouter：动作分发入口表（“看到动作名就能找到业务实现”）。
// 构造时注册全部 40+ 动作（PING/USER_*/ORDER_*/WALLET_*/PILE_TELEMETRY/ADMIN_*），
// route() 按动作名查表调用；未注册动作返回 PARAM_INVALID，处理异常兜底为 PROTOCOL_ERROR。
class RequestRouter
{
public:
    // 处理函数签名：入参为已校验的请求消息与路由上下文，返回统一响应。
    using Handler = std::function<Response(const Message &, RequestContext &)>;

    RequestRouter();

    // 注册动作处理函数（构造函数中批量调用）。
    void registerHandler(const QString &action, Handler handler);
    // 分发：按 message.action 查表执行；管理类动作在对应 handler 内先做 adminToken 校验。
    Response route(const Message &message, RequestContext &context) const;

private:
    QHash<QString, Handler> handlers_;
};
