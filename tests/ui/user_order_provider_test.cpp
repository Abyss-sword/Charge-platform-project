#include <QtTest>

#include "adapters/remote_user_order_provider.h"
#include "adapters/remote_station_provider.h"
#include "adapters/user_order_provider.h"
#include "adapters/user_account_provider.h"
#include "fake_tcp_server.h"
#include "pages/flow_pages.h"
#include "pages/active_order_page.h"
#include "pages/profile_page.h"
#include "shell/main_window.h"
#include "widgets/confirm_dialog.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QSignalSpy>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QStackedWidget>
#include <QToolButton>

#include <algorithm>
#include <utility>

namespace {

UserOrderResult resultAt(const QSignalSpy &spy, int index = 0)
{
    return qvariant_cast<UserOrderResult>(spy.at(index).at(0));
}

QJsonObject orderData(const QString &orderId = QStringLiteral("ORD-1"))
{
    return {{QStringLiteral("orderId"), orderId},
            {QStringLiteral("energyKwh"), QStringLiteral("8.35")},
            {QStringLiteral("unitPriceYuanPerKwh"), QStringLiteral("1.23")},
            {QStringLiteral("amountYuan"), QStringLiteral("10.27")}};
}

QJsonObject orderSnapshot(const QString &orderId, const QString &status)
{
    return {{QStringLiteral("orderId"), orderId}, {QStringLiteral("userId"), qint64(42)},
            {QStringLiteral("pileId"), qint64(101)}, {QStringLiteral("status"), status},
            {QStringLiteral("energyKwh"), QStringLiteral("8.35")},
            {QStringLiteral("unitPriceYuanPerKwh"), QStringLiteral("1.23")},
            {QStringLiteral("amountYuan"), QStringLiteral("10.27")},
            {QStringLiteral("reservedUntil"), QJsonValue::Null},
            {QStringLiteral("startedAt"), QStringLiteral("2026-09-09T10:00:00Z")},
            {QStringLiteral("endedAt"), QJsonValue::Null},
            {QStringLiteral("createdAt"), QStringLiteral("2026-09-09T09:50:00Z")}};
}

QJsonObject stationDetailData()
{
    return {{QStringLiteral("stationId"), qint64(7)},
            {QStringLiteral("name"), QStringLiteral("广州测试站")},
            {QStringLiteral("address"), QStringLiteral("广州市天河区")},
            {QStringLiteral("latitude"), 23.1351},
            {QStringLiteral("longitude"), 113.3245},
            {QStringLiteral("priceYuanPerKwh"), QStringLiteral("1.23")},
            {QStringLiteral("status"), QStringLiteral("OPEN")},
            {QStringLiteral("piles"), QJsonArray{
                 QJsonObject{{QStringLiteral("pileId"), qint64(101)},
                             {QStringLiteral("serial"), QStringLiteral("GZ-101")},
                             {QStringLiteral("chargeType"), QStringLiteral("FAST")},
                             {QStringLiteral("ratedPowerKw"), 120.0},
                             {QStringLiteral("status"), QStringLiteral("IDLE")},
                             {QStringLiteral("lastHeartbeatAt"),
                              QStringLiteral("2026-09-09T10:00:00Z")}},
                 QJsonObject{{QStringLiteral("pileId"), qint64(102)},
                             {QStringLiteral("serial"), QStringLiteral("GZ-102")},
                             {QStringLiteral("chargeType"), QStringLiteral("SLOW")},
                             {QStringLiteral("ratedPowerKw"), 7.0},
                             {QStringLiteral("status"), QStringLiteral("IDLE")},
                             {QStringLiteral("lastHeartbeatAt"),
                              QStringLiteral("2026-09-09T10:00:00Z")}}}}};
}

PileSummary idlePile(qint64 pileId)
{
    return {pileId, QStringLiteral("GZ-%1").arg(pileId), QStringLiteral("FAST"),
            120.0, QStringLiteral("IDLE"), QStringLiteral("2026-09-09T10:00:00Z")};
}

class FakeOrderProvider final : public IUserOrderProvider
{
public:
    using IUserOrderProvider::IUserOrderProvider;
    void reserve(qint64 userId, qint64 pileId) override
    { lastUserId=userId; lastPileId=pileId; lastAction=UserOrderAction::Reserve; }
    void start(const QString &orderId) override
    { lastOrderId=orderId; lastAction=UserOrderAction::Start; }
    void stop(const QString &orderId) override
    { lastOrderId=orderId; lastAction=UserOrderAction::Stop; }
    void cancel(const QString &orderId) override
    { lastOrderId=orderId; lastAction=UserOrderAction::Cancel; }
    void pay(const QString &orderId) override
    { lastOrderId=orderId; lastAction=UserOrderAction::Pay; }
    void recharge(qint64 userId, const QString &amount) override
    { lastUserId=userId; lastAmount=amount; lastAction=UserOrderAction::Recharge; }
    void queryDetail(const QString &orderId) override
    {
        UserOrderSnapshot order{orderId, 42, 101, QStringLiteral("RESERVED"),
                                QStringLiteral("0.00"), QStringLiteral("0.00"),
                                QStringLiteral("0.00"), {}, {}, {}, {}};
        emit queryFinished({UserOrderResultKind::Success, UserOrderQueryKind::Detail,
                            {}, QStringLiteral("OK"), true, order, {}});
    }
    void queryActive(qint64 userId) override
    {
        ++activeQueries;
        UserOrderQueryResult result;
        result.kind = UserOrderResultKind::Success;
        result.query = UserOrderQueryKind::Active;
        result.errorCode = QStringLiteral("OK");
        result.hasOrder = !activeStatus.isEmpty() || !lastOrderId.isEmpty();
        if (result.hasOrder)
            result.order = {lastOrderId.isEmpty() ? QStringLiteral("ORD-ACTIVE") : lastOrderId,
                            userId, lastPileId, activeStatus.isEmpty() ? QStringLiteral("RESERVED") : activeStatus,
                            QStringLiteral("0.00"), QStringLiteral("0.00"),
                            QStringLiteral("0.00"), {}, {}, {}, {}};
        emit queryFinished(result);
    }
    void queryHistory(qint64, int, int) override { ++historyQueries; }
    void complete(const UserOrderResult &result)
    {
        if (result.kind == UserOrderResultKind::Success
            && result.action == UserOrderAction::Reserve)
            lastOrderId = result.orderId;
        emit operationFinished(result);
    }
    void disconnectTransport() { emit connectivityLost(); }
    void reconnectTransport() { emit connectivityRestored(); }
    qint64 lastUserId=0;
    qint64 lastPileId=0;
    QString lastOrderId;
    QString lastAmount;
    UserOrderAction lastAction=UserOrderAction::Reserve;
    int historyQueries = 0;
    int activeQueries = 0;
    QString activeStatus;
};

class FakeAccountProvider final : public IUserAccountProvider
{
public:
    using IUserAccountProvider::IUserAccountProvider;
    void queryProfile(qint64 userId) override
    {
        emit profileFinished({UserAccountResultKind::Success, {}, QStringLiteral("OK"),
                              {userId, QStringLiteral("138****0000"),
                               QStringLiteral("Alice"), QStringLiteral("88.00"),
                               QStringLiteral("NORMAL")}});
    }
    void queryBalance(qint64 userId) override
    {
        ++balanceQueries;
        emit balanceFinished({UserAccountResultKind::Success, {}, QStringLiteral("OK"),
                              userId, QStringLiteral("88.00")});
    }
    int balanceQueries = 0;
};

} // namespace

class UserOrderProviderTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { qRegisterMetaType<UserOrderResult>(); }

    void homeFindStationButtonNavigatesToStationPage()
    {
        FakeOrderProvider provider;
        UserMainWindow window(UserContext{42, QStringLiteral("Alice"), false},
                              nullptr, &provider);
        auto *stack = window.findChild<QStackedWidget *>(QStringLiteral("primaryPages"));
        auto *bar = window.findChild<QWidget *>(QStringLiteral("bottomNavigationBar"));
        QVERIFY(stack);
        QVERIFY(bar);
        QCOMPARE(stack->currentIndex(), int(PrimaryPage::Home));
        QWidget *const stationPage = stack->widget(int(PrimaryPage::Stations));
        auto *action = stack->widget(int(PrimaryPage::Home))
                           ->findChild<QPushButton *>(QStringLiteral("primaryAction"));
        auto *stationNav = bar->findChild<QToolButton *>(QStringLiteral("nav1"));
        QVERIFY(action);
        QVERIFY(action->isEnabled());
        QVERIFY(stationNav);

        QTest::mouseClick(action, Qt::LeftButton);

        QCOMPARE(stack->currentIndex(), int(PrimaryPage::Stations));
        QCOMPARE(stack->currentWidget(), stationPage);
        QVERIFY(stationNav->isChecked());
    }

    void reservationChecksActiveOrderBeforeSubmitting()
    {
        FakeOrderProvider provider;
        provider.activeStatus = QStringLiteral("PENDING_PAYMENT");
        UserMainWindow window(UserContext{42, QStringLiteral("Alice"), false},
                              nullptr, &provider);
        provider.activeQueries = 0;
        auto *stack = window.findChild<QStackedWidget *>(QStringLiteral("primaryPages"));
        QVERIFY(stack);
        window.navigateTo(SecondaryPage::StationDetail);
        auto *detail = qobject_cast<StationDetailPage *>(stack->currentWidget());
        QVERIFY(detail);
        detail->show();
        // Inject a valid pile through the existing reservation entry point.
        emit detail->reservationRequested(idlePile(101));
        QCOMPARE(provider.activeQueries, 1);
        auto *dialog = window.findChild<Ui::ConfirmDialog *>();
        QVERIFY(dialog);
        QVERIFY(dialog->findChild<QLabel *>(QStringLiteral("dialogMessage"))
                    ->text().contains(QStringLiteral("您有未完成的充电订单，请先结算")));
        dialog->confirmButton()->click();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("settlementPage"));
    }

    void reservationErrorDoesNotPersistIntoNextAttempt()
    {
        FakeTcpServer stationServer;
        QVERIFY(stationServer.listen());
        stationServer.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                request.value(QStringLiteral("requestId")).toString(), stationDetailData())));
        };
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            if (request.value(QStringLiteral("action")).toString()
                    == QStringLiteral("ORDER_ACTIVE")) {
                socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                    request.value(QStringLiteral("requestId")).toString(),
                    {{QStringLiteral("order"), QJsonObject{}}})));
                return;
            }
            socket->write(FakeTcpServer::frame({
                {QStringLiteral("requestId"), request.value(QStringLiteral("requestId"))},
                {QStringLiteral("ok"), false},
                {QStringLiteral("code"), QStringLiteral("PILE_UNAVAILABLE")},
                {QStringLiteral("message"), QStringLiteral("电桩不可用")},
                {QStringLiteral("data"), QJsonObject{}},
                {QStringLiteral("error"), QJsonObject{
                    {QStringLiteral("code"), QStringLiteral("PILE_UNAVAILABLE")},
                    {QStringLiteral("message"), QStringLiteral("电桩不可用")}}}
            }));
        };
        RemoteStationProvider stationProvider(QStringLiteral("127.0.0.1"),
                                              stationServer.port());
        RemoteUserOrderProvider provider(QStringLiteral("127.0.0.1"), server.port());
        UserMainWindow window(UserContext{42, QStringLiteral("Alice"), false},
                              &stationProvider, &provider);
        auto *stack = window.findChild<QStackedWidget *>(QStringLiteral("primaryPages"));
        QVERIFY(stack);
        window.navigateTo(SecondaryPage::StationDetail);
        auto *detail = static_cast<StationDetailPage *>(stack->currentWidget());
        QVERIFY(detail);
        detail->loadStation(7);
        auto *selector = detail->findChild<QComboBox *>(QStringLiteral("availablePileSelector"));
        auto *enter = detail->findChild<QPushButton *>(QStringLiteral("reserveSelectedPileAction"));
        QTRY_COMPARE(selector->count(), 2);
        QVERIFY(enter->isEnabled());
        QTest::mouseClick(enter, Qt::LeftButton);
        QTRY_VERIFY(stack->currentWidget()->objectName()
                    == QStringLiteral("reservationConfirmPage"));
        auto *reservation = qobject_cast<ReservationConfirmPage *>(stack->currentWidget());
        QVERIFY(reservation);
        QTest::mouseClick(reservation->primaryAction(), Qt::LeftButton);
        QTRY_COMPARE(reservation->stateWidget()->state(), Ui::PageState::Error);

        window.goBack();
        QCOMPARE(stack->currentWidget(), detail);
        QTRY_VERIFY(enter->isEnabled());
        QTest::mouseClick(enter, Qt::LeftButton);
        QTRY_VERIFY(stack->currentWidget()->objectName()
                    == QStringLiteral("reservationConfirmPage"));
        QCOMPARE(stack->currentWidget(), reservation);
        QCOMPARE(reservation->stateWidget()->state(), Ui::PageState::Normal);
        QVERIFY(reservation->primaryAction()->isEnabled());
        QVERIFY(!reservation->findChild<QLineEdit *>(QStringLiteral("reservationPileId")));
        for (const auto *label : reservation->findChildren<QLabel *>())
            QVERIFY(!label->text().contains(QStringLiteral("电桩不可用")));
    }

    void stationDetailSelectionSubmitsRealPile()
    {
        FakeTcpServer stationServer;
        QVERIFY(stationServer.listen());
        stationServer.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                request.value(QStringLiteral("requestId")).toString(), stationDetailData())));
        };
        FakeTcpServer orderServer;
        QVERIFY(orderServer.listen());
        bool reserved = false;
        orderServer.handler = [&reserved](QTcpSocket *socket, const QJsonObject &request) {
            const QString action = request.value(QStringLiteral("action")).toString();
            if (action == QStringLiteral("ORDER_ACTIVE")) {
                QJsonObject data{{QStringLiteral("order"), QJsonObject{}}};
                if (reserved)
                    data.insert(QStringLiteral("order"),
                                orderSnapshot(QStringLiteral("ORD-REAL-PILE"),
                                              QStringLiteral("RESERVED")));
                socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                    request.value(QStringLiteral("requestId")).toString(), data)));
            } else if (action == QStringLiteral("ORDER_RESERVE")) {
                QCOMPARE(request.value(QStringLiteral("payload")).toObject()
                             .value(QStringLiteral("pileId")).toInteger(), qint64(101));
                reserved = true;
                socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                    request.value(QStringLiteral("requestId")).toString(),
                    {{QStringLiteral("orderId"), QStringLiteral("ORD-REAL-PILE")}})));
            }
        };
        RemoteStationProvider stationProvider(QStringLiteral("127.0.0.1"),
                                              stationServer.port());
        RemoteUserOrderProvider orderProvider(QStringLiteral("127.0.0.1"),
                                              orderServer.port());
        UserMainWindow window(UserContext{42, QStringLiteral("Alice"), false},
                              &stationProvider, &orderProvider);
        auto *stack = window.findChild<QStackedWidget *>(QStringLiteral("primaryPages"));
        window.navigateTo(SecondaryPage::StationDetail);
        auto *detail = qobject_cast<StationDetailPage *>(stack->currentWidget());
        detail->loadStation(7);
        auto *reserve = detail->findChild<QPushButton *>(
            QStringLiteral("reserveSelectedPileAction"));
        QTRY_VERIFY(reserve->isEnabled());
        reserve->click();
        QTRY_VERIFY(stack->currentWidget()->objectName()
                    == QStringLiteral("reservationConfirmPage"));
        auto *confirmation = qobject_cast<ReservationConfirmPage *>(stack->currentWidget());
        QVERIFY(confirmation);
        QVERIFY(!confirmation->findChild<QLineEdit *>(QStringLiteral("reservationPileId")));
        QVERIFY(confirmation->primaryAction()->isEnabled());
        confirmation->primaryAction()->click();
        QTRY_COMPARE(stack->currentIndex(), int(PrimaryPage::Orders));
        auto *active = qobject_cast<ActiveOrderPage *>(stack->currentWidget());
        auto *continueAction = active->findChild<QPushButton *>(
            QStringLiteral("continueActiveOrderAction"));
        QTRY_VERIFY(continueAction && continueAction->isEnabled());
        continueAction->click();
        QCOMPARE(stack->currentWidget()->objectName(), QStringLiteral("chargingStatusPage"));
    }

    void secondReservationAttemptCanSubmitAgain()
    {
        FakeTcpServer stationServer;
        QVERIFY(stationServer.listen());
        stationServer.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                request.value(QStringLiteral("requestId")).toString(), stationDetailData())));
        };
        FakeTcpServer server;
        QVERIFY(server.listen());
        int reserveCount = 0;
        bool reserved = false;
        server.handler = [&reserveCount, &reserved](QTcpSocket *socket,
                                                    const QJsonObject &request) {
            const QString action = request.value(QStringLiteral("action")).toString();
            if (action == QStringLiteral("ORDER_ACTIVE")) {
                QJsonObject data{{QStringLiteral("order"), QJsonObject{}}};
                if (reserved)
                    data.insert(QStringLiteral("order"),
                                orderSnapshot(QStringLiteral("ORD-SECOND"),
                                              QStringLiteral("RESERVED")));
                socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                    request.value(QStringLiteral("requestId")).toString(), data)));
                return;
            }
            if (action == QStringLiteral("ORDER_RESERVE") && ++reserveCount == 1) {
                socket->write(FakeTcpServer::frame({
                    {QStringLiteral("requestId"), request.value(QStringLiteral("requestId"))},
                    {QStringLiteral("ok"), false},
                    {QStringLiteral("code"), QStringLiteral("PILE_UNAVAILABLE")},
                    {QStringLiteral("message"), QStringLiteral("电桩不可用")},
                    {QStringLiteral("data"), QJsonObject{}},
                    {QStringLiteral("error"), QJsonObject{
                        {QStringLiteral("code"), QStringLiteral("PILE_UNAVAILABLE")},
                        {QStringLiteral("message"), QStringLiteral("电桩不可用")}}}
                }));
                return;
            }
            reserved = true;
            socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                request.value(QStringLiteral("requestId")).toString(),
                {{QStringLiteral("orderId"), QStringLiteral("ORD-SECOND")}})));
        };
        RemoteStationProvider stationProvider(QStringLiteral("127.0.0.1"),
                                              stationServer.port());
        RemoteUserOrderProvider provider(QStringLiteral("127.0.0.1"), server.port());
        UserMainWindow window(UserContext{42, QStringLiteral("Alice"), false},
                              &stationProvider, &provider);
        auto *stack = window.findChild<QStackedWidget *>(QStringLiteral("primaryPages"));
        QVERIFY(stack);
        window.navigateTo(SecondaryPage::StationDetail);
        auto *detail = static_cast<StationDetailPage *>(stack->currentWidget());
        QVERIFY(detail);
        detail->loadStation(7);
        auto *selector = detail->findChild<QComboBox *>(QStringLiteral("availablePileSelector"));
        auto *enter = detail->findChild<QPushButton *>(QStringLiteral("reserveSelectedPileAction"));
        QTRY_COMPARE(selector->count(), 2);
        QTest::mouseClick(enter, Qt::LeftButton);
        QTRY_VERIFY(stack->currentWidget()->objectName()
                    == QStringLiteral("reservationConfirmPage"));
        auto *reservation = qobject_cast<ReservationConfirmPage *>(stack->currentWidget());
        QVERIFY(reservation);
        QTest::mouseClick(reservation->primaryAction(), Qt::LeftButton);
        QTRY_COMPARE(reservation->stateWidget()->state(), Ui::PageState::Error);

        window.goBack();
        selector->setCurrentIndex(1);
        QTRY_VERIFY(enter->isEnabled());
        QTest::mouseClick(enter, Qt::LeftButton);
        QTRY_VERIFY(stack->currentWidget()->objectName()
                    == QStringLiteral("reservationConfirmPage"));
        QTest::mouseClick(reservation->primaryAction(), Qt::LeftButton);

        QTRY_VERIFY(std::count_if(server.requests.cbegin(), server.requests.cend(),
                                 [](const QJsonObject &request) {
            return request.value(QStringLiteral("action")) == QStringLiteral("ORDER_RESERVE");
        }) >= 2);
        QList<QJsonObject> reserveRequests;
        for (const QJsonObject &request : std::as_const(server.requests))
            if (request.value(QStringLiteral("action")) == QStringLiteral("ORDER_RESERVE"))
                reserveRequests.append(request);
        QCOMPARE(reserveRequests.at(1).value(QStringLiteral("payload")).toObject()
                     .value(QStringLiteral("pileId")).toInteger(), qint64(102));
        QVERIFY(reserveRequests.at(0).value(QStringLiteral("requestId")).toString()
                    != reserveRequests.at(1).value(QStringLiteral("requestId")).toString());
        QTRY_COMPARE(stack->currentIndex(), int(PrimaryPage::Orders));
    }

    void requestEnvelopesAndSuccessMapping()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            const QString action = request.value(QStringLiteral("action")).toString();
            QJsonObject data;
            if (action == QStringLiteral("ORDER_RESERVE")) {
                data.insert(QStringLiteral("orderId"), QStringLiteral("ORD-1"));
            } else if (action != QStringLiteral("WALLET_PAY")
                       && action != QStringLiteral("WALLET_RECHARGE")) {
                data = orderData();
            }
            socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                request.value(QStringLiteral("requestId")).toString(), data)));
        };
        RemoteUserOrderProvider provider(QStringLiteral("127.0.0.1"), server.port());
        provider.setUserToken(QStringLiteral("order-write-token"));
        QSignalSpy results(&provider, &IUserOrderProvider::operationFinished);

        provider.reserve(qint64(9876543210123LL), qint64(1234567890123LL));
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(resultAt(results).kind, UserOrderResultKind::Success);
        QCOMPARE(resultAt(results).orderId, QStringLiteral("ORD-1"));
        provider.start(QStringLiteral("ORD-1"));
        QTRY_COMPARE(results.count(), 2);
        provider.stop(QStringLiteral("ORD-1"));
        QTRY_COMPARE(results.count(), 3);
        QCOMPARE(resultAt(results, 2).energyKwh, QStringLiteral("8.35"));
        QCOMPARE(resultAt(results, 2).amountYuan, QStringLiteral("10.27"));
        provider.cancel(QStringLiteral("ORD-1"));
        QTRY_COMPARE(results.count(), 4);
        provider.pay(QStringLiteral("ORD-1"));
        QTRY_COMPARE(results.count(), 5);
        provider.recharge(qint64(9876543210123LL), QStringLiteral("100.00"));
        QTRY_COMPARE(results.count(), 6);

        QCOMPARE(server.requests.size(), 6);
        const QStringList actions{QStringLiteral("ORDER_RESERVE"),
                                  QStringLiteral("ORDER_START"),
                                  QStringLiteral("ORDER_STOP"),
                                  QStringLiteral("ORDER_CANCEL"),
                                  QStringLiteral("WALLET_PAY"),
                                  QStringLiteral("WALLET_RECHARGE")};
        for (int i = 0; i < actions.size(); ++i) {
            QCOMPARE(server.requests.at(i).value(QStringLiteral("action")).toString(),
                     actions.at(i));
            QVERIFY(!server.requests.at(i).value(QStringLiteral("requestId"))
                         .toString().isEmpty());
        }
        const QJsonObject reserve = server.requests.at(0)
                                        .value(QStringLiteral("payload")).toObject();
        QCOMPARE(reserve.size(), 3);
        QCOMPARE(reserve.value(QStringLiteral("userToken")).toString(),
                 QStringLiteral("order-write-token"));
        QCOMPARE(reserve.value(QStringLiteral("userId")).toInteger(),
                 qint64(9876543210123LL));
        QCOMPARE(reserve.value(QStringLiteral("pileId")).toInteger(),
                 qint64(1234567890123LL));
        const QJsonObject recharge = server.requests.at(5)
                                         .value(QStringLiteral("payload")).toObject();
        QCOMPARE(recharge.value(QStringLiteral("amountYuan")).toString(),
                 QStringLiteral("100.00"));
        for (const QJsonObject &request : server.requests) {
            QCOMPARE(request.value(QStringLiteral("payload")).toObject()
                         .value(QStringLiteral("userToken")).toString(),
                     QStringLiteral("order-write-token"));
        }
    }

    void serverErrorMessage()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            socket->write(FakeTcpServer::frame({
                {QStringLiteral("requestId"), request.value(QStringLiteral("requestId"))},
                {QStringLiteral("ok"), false},
                {QStringLiteral("code"), QStringLiteral("PILE_UNAVAILABLE")},
                {QStringLiteral("message"), QStringLiteral("outer")},
                {QStringLiteral("data"), QJsonObject{}},
                {QStringLiteral("error"), QJsonObject{
                    {QStringLiteral("code"), QStringLiteral("PILE_UNAVAILABLE")},
                    {QStringLiteral("message"), QStringLiteral("电桩不可用")}}}
            }));
        };
        RemoteUserOrderProvider provider(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy results(&provider, &IUserOrderProvider::operationFinished);
        provider.reserve(1, 1);
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(resultAt(results).kind, UserOrderResultKind::Error);
        QCOMPARE(resultAt(results).message, QStringLiteral("电桩不可用"));
    }

    void formalBusinessErrors_data()
    {
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("message");
        QTest::newRow("balance-insufficient") << QStringLiteral("BALANCE_INSUFFICIENT") << QStringLiteral("余额不足");
        QTest::newRow("order-state-conflict") << QStringLiteral("ORDER_STATE_CONFLICT") << QStringLiteral("订单状态冲突");
        QTest::newRow("parameter-invalid") << QStringLiteral("PARAM_INVALID") << QStringLiteral("参数错误");
    }

    void formalBusinessErrors()
    {
        QFETCH(QString, code);
        QFETCH(QString, message);
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [code,message](QTcpSocket *socket,const QJsonObject &request) {
            socket->write(FakeTcpServer::frame({
                {QStringLiteral("requestId"),request.value(QStringLiteral("requestId"))},
                {QStringLiteral("ok"),false},{QStringLiteral("code"),code},
                {QStringLiteral("message"),QStringLiteral("outer")},
                {QStringLiteral("data"),QJsonObject{}},
                {QStringLiteral("error"),QJsonObject{{QStringLiteral("code"),code},{QStringLiteral("message"),message}}}
            }));
        };
        RemoteUserOrderProvider provider(QStringLiteral("127.0.0.1"),server.port());
        QSignalSpy results(&provider,&IUserOrderProvider::operationFinished);
        provider.pay(QStringLiteral("ORD-1"));
        QTRY_COMPARE(results.count(),1);
        QCOMPARE(resultAt(results).kind,UserOrderResultKind::Error);
        QCOMPARE(resultAt(results).message,message);
        QCOMPARE(resultAt(results).action,UserOrderAction::Pay);
        QCOMPARE(resultAt(results).errorCode, code);
    }

    void malformedResponse()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            QJsonObject data = orderData();
            data.insert(QStringLiteral("amountYuan"), 10.27);
            socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                request.value(QStringLiteral("requestId")).toString(), data)));
        };
        RemoteUserOrderProvider provider(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy results(&provider, &IUserOrderProvider::operationFinished);
        provider.stop(QStringLiteral("ORD-1"));
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(resultAt(results).kind, UserOrderResultKind::Error);
    }

    void offlineAndTimeout()
    {
        QTcpServer reservation;
        QVERIFY(reservation.listen(QHostAddress::LocalHost, 0));
        const quint16 port = reservation.serverPort();
        reservation.close();
        RemoteUserOrderProvider offline(QStringLiteral("127.0.0.1"), port);
        QSignalSpy offlineResults(&offline, &IUserOrderProvider::operationFinished);
        offline.pay(QStringLiteral("ORD-1"));
        QTRY_COMPARE(offlineResults.count(), 1);
        QCOMPARE(resultAt(offlineResults).kind, UserOrderResultKind::Offline);

        FakeTcpServer server;
        QVERIFY(server.listen());
        ServiceClient client(QStringLiteral("127.0.0.1"), server.port());
        client.setRequestTimeout(150);
        RemoteUserOrderProvider timeout(&client);
        QSignalSpy timeoutResults(&timeout, &IUserOrderProvider::operationFinished);
        timeout.start(QStringLiteral("ORD-1"));
        QTRY_COMPARE(timeoutResults.count(), 1);
        QCOMPARE(resultAt(timeoutResults).kind, UserOrderResultKind::Error);
    }

    void singleInFlight()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        RemoteUserOrderProvider provider(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy results(&provider, &IUserOrderProvider::operationFinished);
        provider.reserve(1, 1);
        provider.pay(QStringLiteral("ORD-2"));
        QCOMPARE(results.count(), 1);
        QCOMPARE(resultAt(results).action, UserOrderAction::Pay);
        QCOMPARE(resultAt(results).kind, UserOrderResultKind::Error);
        QTRY_COMPARE(server.requests.size(), 1);
        server.client()->write(FakeTcpServer::frame(FakeTcpServer::response(
            server.requests.first().value(QStringLiteral("requestId")).toString(),
            {{QStringLiteral("orderId"), QStringLiteral("ORD-1")}})));
        QTRY_COMPARE(results.count(), 2);
        QCOMPARE(resultAt(results, 1).kind, UserOrderResultKind::Success);
    }

    void paymentTimeoutQueriesDetailAndRecoversPaidOrder()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            const QString action = request.value(QStringLiteral("action")).toString();
            if (action == QStringLiteral("ORDER_DETAIL")) {
                socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                    request.value(QStringLiteral("requestId")).toString(),
                    orderSnapshot(QStringLiteral("ORD-PAID"), QStringLiteral("FINISHED")))));
            }
        };
        ServiceClient client(QStringLiteral("127.0.0.1"), server.port());
        client.setRequestTimeout(100);
        RemoteUserOrderProvider provider(&client);
        SettlementPage settlement({QStringLiteral("结算"), {}, {}, {},
                                   Ui::PageState::Normal}, &provider);
        QSignalSpy confirmed(&settlement, &SettlementPage::paymentConfirmed);
        settlement.setOrderId(QStringLiteral("ORD-PAID"));
        settlement.primaryAction()->click();

        QTRY_VERIFY(std::any_of(server.requests.cbegin(), server.requests.cend(),
                                [](const QJsonObject &request) {
            return request.value(QStringLiteral("action")) == QStringLiteral("ORDER_DETAIL");
        }));
        QTRY_COMPARE(confirmed.count(), 1);
        QVERIFY(!settlement.primaryAction()->isEnabled());
        QVERIFY(settlement.primaryAction()->text().contains(QStringLiteral("已完成")));
    }

    void paymentTimeoutQueriesDetailBeforeAllowingRetry()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        int payCount = 0;
        server.handler = [&payCount](QTcpSocket *socket, const QJsonObject &request) {
            const QString action = request.value(QStringLiteral("action")).toString();
            if (action == QStringLiteral("WALLET_PAY")) {
                ++payCount;
                if (payCount > 1)
                    socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                        request.value(QStringLiteral("requestId")).toString())));
            } else if (action == QStringLiteral("ORDER_DETAIL")) {
                socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                    request.value(QStringLiteral("requestId")).toString(),
                    orderSnapshot(QStringLiteral("ORD-UNPAID"),
                                  QStringLiteral("PENDING_PAYMENT")))));
            }
        };
        ServiceClient client(QStringLiteral("127.0.0.1"), server.port());
        client.setRequestTimeout(100);
        RemoteUserOrderProvider provider(&client);
        SettlementPage settlement({QStringLiteral("结算"), {}, {}, {},
                                   Ui::PageState::Normal}, &provider);
        settlement.setOrderId(QStringLiteral("ORD-UNPAID"));
        settlement.primaryAction()->click();

        QTRY_VERIFY(settlement.primaryAction()->isEnabled());
        QCOMPARE(payCount, 1);
        QVERIFY(settlement.primaryAction()->text().contains(QStringLiteral("重新提交")));
        settlement.primaryAction()->click();
        QTRY_COMPARE(payCount, 2);
        QTRY_VERIFY(!settlement.primaryAction()->isEnabled());
    }

    void settlementNewOrderClearsPreviousErrorState()
    {
        FakeOrderProvider provider;
        SettlementPage settlement({QStringLiteral("结算"), {}, {}, {},
                                   Ui::PageState::Normal}, &provider);
        settlement.setOrderId(QStringLiteral("ORD-A"));
        settlement.primaryAction()->click();
        provider.complete({UserOrderResultKind::Error, UserOrderAction::Pay,
                           QStringLiteral("订单A支付失败"), QStringLiteral("ORD-A"),
                           {}, {}, {}, QStringLiteral("BALANCE_INSUFFICIENT")});
        QCOMPARE(settlement.stateWidget()->state(), Ui::PageState::Error);

        settlement.setOrderId(QStringLiteral("ORD-B"));
        QCOMPARE(settlement.stateWidget()->state(), Ui::PageState::Normal);
        QVERIFY(settlement.primaryAction()->isEnabled());
        QVERIFY(!settlement.primaryAction()->text().contains(QStringLiteral("已完成")));
        for (const QLabel *label : settlement.findChildren<QLabel *>())
            QVERIFY(!label->text().contains(QStringLiteral("订单A支付失败")));
    }

    void paymentConfirmationRefreshesBalanceAndHistory()
    {
        FakeOrderProvider orderProvider;
        FakeAccountProvider accountProvider;
        UserMainWindow window(UserContext{42, QStringLiteral("Alice"), false}, nullptr,
                              &orderProvider, &accountProvider);
        const int initialBalanceQueries = accountProvider.balanceQueries;
        auto *stack = window.findChild<QStackedWidget *>(QStringLiteral("primaryPages"));
        window.navigateTo(SecondaryPage::Settlement);
        auto *settlement = qobject_cast<SettlementPage *>(stack->currentWidget());
        settlement->setOrderId(QStringLiteral("ORD-PAY"));
        settlement->primaryAction()->click();
        orderProvider.complete({UserOrderResultKind::Success, UserOrderAction::Pay,
                                QStringLiteral("支付成功"), {}, {}, {}, {},
                                QStringLiteral("OK")});
        QCOMPARE(accountProvider.balanceQueries, initialBalanceQueries + 1);
        QCOMPARE(orderProvider.historyQueries, 1);
    }

    void orderDetailShowsServerTimesAndLocalQueryTime()
    {
        FakeOrderProvider provider;
        ChargingStatusPage page({QStringLiteral("充电"), {}, {}, {},
                                 Ui::PageState::Normal}, &provider);
        page.setOrderId(QStringLiteral("ORD-TIME"));
        QTRY_VERIFY(([&page] {
            for (const QLabel *label : page.findChildren<QLabel *>())
                if (label->text().contains(QStringLiteral("最后查询时间："))) return true;
            return false;
        })());
        QString details;
        for (const QLabel *label : page.findChildren<QLabel *>())
            if (label->text().contains(QStringLiteral("最后查询时间："))) details = label->text();
        QVERIFY(details.contains(QStringLiteral("开始时间：")));
        QVERIFY(details.contains(QStringLiteral("结束时间：")));
        QVERIFY(!details.contains(QStringLiteral("更新时间：")));
    }

    void pageFlowUsesProviderAndServerResults()
    {
        FakeOrderProvider provider;
        UserMainWindow window(UserContext{42, QStringLiteral("Alice"), false},
                              nullptr, &provider);
        window.navigateTo(SecondaryPage::ReservationConfirm);
        auto *stack = window.findChild<QStackedWidget *>(QStringLiteral("primaryPages"));
        QVERIFY(stack);
        auto *reservation = qobject_cast<ReservationConfirmPage *>(stack->currentWidget());
        QVERIFY(reservation);
        reservation->beginForPile(idlePile(7));
        reservation->primaryAction()->click();
        QCOMPARE(provider.lastAction, UserOrderAction::Reserve);
        QCOMPARE(provider.lastUserId, qint64(42));
        QCOMPARE(provider.lastPileId, qint64(7));
        provider.complete({UserOrderResultKind::Success, UserOrderAction::Reserve,
                           {}, QStringLiteral("ORD-UI"), {}, {}, {}, {}});

        auto *active = qobject_cast<ActiveOrderPage *>(stack->currentWidget());
        QVERIFY(active);
        auto *continueAction = active->findChild<QPushButton *>(
            QStringLiteral("continueActiveOrderAction"));
        QVERIFY(continueAction && continueAction->isEnabled());
        continueAction->click();
        auto *charging = qobject_cast<ChargingStatusPage *>(stack->currentWidget());
        QVERIFY(charging);
        QTRY_VERIFY(charging->primaryAction()->isEnabled());
        charging->primaryAction()->click();
        QCOMPARE(provider.lastAction, UserOrderAction::Start);
        QCOMPARE(provider.lastOrderId, QStringLiteral("ORD-UI"));
        provider.complete({UserOrderResultKind::Success, UserOrderAction::Start,
                           {}, QStringLiteral("ORD-UI"), QStringLiteral("0.00"),
                           QStringLiteral("0.00"), QStringLiteral("0.00"), {}});
        charging->primaryAction()->click();
        QCOMPARE(provider.lastAction, UserOrderAction::Stop);
        provider.complete({UserOrderResultKind::Success, UserOrderAction::Stop,
                           {}, QStringLiteral("ORD-UI"), QStringLiteral("8.35"),
                           QStringLiteral("1.23"), QStringLiteral("10.27"), {}});

        auto *settlement = qobject_cast<SettlementPage *>(stack->currentWidget());
        QVERIFY(settlement);
        settlement->primaryAction()->click();
        QCOMPARE(provider.lastAction, UserOrderAction::Pay);
        QCOMPARE(provider.lastOrderId, QStringLiteral("ORD-UI"));
    }

    void cancelAndRechargePagesUseProvider()
    {
        FakeOrderProvider provider;
        PagePreview preview{QStringLiteral("页面"), {}, {}, {},
                            Ui::PageState::Normal};
        ChargingStatusPage charging(preview, &provider);
        charging.setOrderId(QStringLiteral("ORD-CANCEL"));
        auto *cancel = charging.findChild<QPushButton *>(
            QStringLiteral("cancelReservationAction"));
        QVERIFY(cancel);
        cancel->click();
        QCOMPARE(provider.lastAction, UserOrderAction::Cancel);
        QCOMPARE(provider.lastOrderId, QStringLiteral("ORD-CANCEL"));
        provider.complete({UserOrderResultKind::Success, UserOrderAction::Cancel,
                           {}, QStringLiteral("ORD-CANCEL"), QStringLiteral("0.00"),
                           QStringLiteral("0.00"), QStringLiteral("0.00"), {}});

        AccountPage account(preview, &provider, qint64(42));
        auto *amount = account.findChild<QLineEdit *>(QStringLiteral("rechargeAmount"));
        auto *recharge = account.findChild<QPushButton *>(QStringLiteral("rechargeAction"));
        QVERIFY(amount);
        QVERIFY(recharge);
        amount->setText(QStringLiteral("100.00"));
        recharge->click();
        QCOMPARE(provider.lastAction, UserOrderAction::Recharge);
        QCOMPARE(provider.lastUserId, qint64(42));
        QCOMPARE(provider.lastAmount, QStringLiteral("100.00"));
    }

    void staleDisablesWritesAndReconnectDoesNotReplay()
    {
        FakeOrderProvider provider;
        PagePreview preview{QStringLiteral("页面"),{},{},{},Ui::PageState::Normal};
        ReservationConfirmPage reservation(preview,&provider,42);
        reservation.beginForPile(idlePile(7));
        provider.disconnectTransport();
        QCOMPARE(reservation.stateWidget()->state(),Ui::PageState::Offline);
        QVERIFY(!reservation.primaryAction()->isEnabled());
        reservation.primaryAction()->click();
        QCOMPARE(provider.lastPileId,qint64(0));
        provider.reconnectTransport();
        QVERIFY(reservation.primaryAction()->isEnabled());
        QCOMPARE(provider.lastPileId,qint64(0));
    }
};

QTEST_MAIN(UserOrderProviderTest)
#include "user_order_provider_test.moc"
