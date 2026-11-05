#include "shared/domain/order_state.h"

// 状态跳转表：用分支明确列出允许的转换。
// 注意答辩口径：业务 Service 还会额外做状态/SQL 条件检查，
// 不能说“所有写入都直接调用了这个辅助函数”。
bool canTransition(OrderState from, OrderState to)
{
    switch (from) {
        // 预约中：可开始充电、被取消、或超时过期。
    case OrderState::Reserved:
        return to == OrderState::Charging || to == OrderState::Canceled
            || to == OrderState::Expired;
        // 充电中：只能停止进入待支付。
    case OrderState::Charging:
        return to == OrderState::PendingPayment;
        // 待支付：只能支付完成。
    case OrderState::PendingPayment:
        return to == OrderState::Finished;
        // 三个终态：不允许任何后续跳转（终态不能随意跳回充电）。
    case OrderState::Finished:
    case OrderState::Canceled:
    case OrderState::Expired:
        return false;
    }
    return false;
}

// 状态名映射：与数据库 status 列及协议 JSON 完全一致，便于排查与展示。
QString orderStateName(OrderState state)
{
    switch (state) {
    case OrderState::Reserved: return QStringLiteral("RESERVED");
    case OrderState::Charging: return QStringLiteral("CHARGING");
    case OrderState::PendingPayment: return QStringLiteral("PENDING_PAYMENT");
    case OrderState::Finished: return QStringLiteral("FINISHED");
    case OrderState::Canceled: return QStringLiteral("CANCELED");
    case OrderState::Expired: return QStringLiteral("EXPIRED");
    }
    return {};
}
