// 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
// 订单状态机单测：验证六状态的合法/非法跳转，终态不能跳回充电等规则。

#include <QtTest>

#include "shared/domain/order_state.h"

class OrderStateTest final : public QObject
{
    Q_OBJECT

private slots:
    void allowsOnlyDeclaredTransitions()
    {
        const QList<OrderState> states{OrderState::Reserved, OrderState::Charging,
                                       OrderState::PendingPayment, OrderState::Finished,
                                       OrderState::Canceled, OrderState::Expired};
        for (const auto from : states) {
            for (const auto to : states) {
                const bool declared =
                    (from == OrderState::Reserved && to == OrderState::Charging)
                    || (from == OrderState::Reserved && to == OrderState::Canceled)
                    || (from == OrderState::Reserved && to == OrderState::Expired)
                    || (from == OrderState::Charging && to == OrderState::PendingPayment)
                    || (from == OrderState::PendingPayment && to == OrderState::Finished);
                QCOMPARE(canTransition(from, to), declared);
            }
        }
    }
};

QTEST_GUILESS_MAIN(OrderStateTest)
#include "order_state_test.moc"
