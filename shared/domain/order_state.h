#pragma once

#include <QString>

// 订单六状态枚举与状态机（公共算法之三）：
// RESERVED ─开始→ CHARGING ─停止→ PENDING_PAYMENT ─支付→ FINISHED；
// RESERVED 还可 → CANCELED（用户取消）/ EXPIRED（15分钟超时）。
// 答辩口径：这是明确业务规则的状态机，不是机器学习；相同状态+事件必得确定结果。
enum class OrderState {
    Reserved,
    Charging,
    PendingPayment,
    Finished,
    Canceled,
    Expired
};

// 判断 from → to 是否为合法跳转（终态 FINISHED/CANCELED/EXPIRED 一律不可再跳）。
bool canTransition(OrderState from, OrderState to);
// 枚举 → 协议字符串名（与数据库/JSON 中的 status 字段一致）。
QString orderStateName(OrderState state);
