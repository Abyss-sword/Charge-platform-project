// 测试文件头注释（第一阶段联动契约 · S1-A）：
// 联动契约集成测试：写操作响应必须带 operationId/changeVersion/serverTimeUtc 且版本严格递增；
// 查询响应必须带 snapshotVersion/snapshotAtUtc；运营改桩必须同步反映到用户侧站点查询与运营总览。
// 直调 RequestRouter::route()（真实路由+真实服务+TestDatabase 临时库），不走 TCP。

#include <QDateTime>
#include <QJsonObject>
#include <QtTest>

#include "server/network/request_router.h"
#include "server/services/admin_service.h"
#include "tests/helpers/test_database.h"

namespace {

// 构造协议请求消息（版本/请求编号/动作/载荷）。
Message makeMessage(const QString &requestId, const QString &action,
                    const QJsonObject &payload)
{
    Message message;
    message.version = QStringLiteral("1.0");
    message.requestId = requestId;
    message.action = action;
    message.payload = payload;
    return message;
}

}  // namespace

class LinkageContractTest final : public QObject
{
    Q_OBJECT

private slots:
    // 写操作响应必须携带 operationId/changeVersion/serverTimeUtc，且连续写版本严格递增。
    void writeResponsesCarryLinkageMetadataAndMonotonicVersion()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        RequestRouter router;
        RequestContext context{testDb.database()};

        // 先登录拿 userToken（登录按查询口径标注快照版本，不计入写版本）。
        const auto login = router.route(
            makeMessage(QStringLiteral("REQ-L1"), QStringLiteral("USER_LOGIN"),
                        {{QStringLiteral("phone"), QStringLiteral("13800009999")}}),
            context);
        QVERIFY2(login.ok, qPrintable(login.message));
        QVERIFY(login.data.value(QStringLiteral("snapshotVersion")).toInteger() >= 1);
        const QString userToken = login.data.value(QStringLiteral("userToken")).toString();
        QVERIFY(!userToken.isEmpty());

        const auto first = router.route(
            makeMessage(QStringLiteral("REQ-W1"), QStringLiteral("WALLET_RECHARGE"),
                        {{QStringLiteral("userToken"), userToken},
                         {QStringLiteral("amountYuan"), QStringLiteral("10.00")}}),
            context);
        QVERIFY2(first.ok, qPrintable(first.message));
        const QString operationId = first.data.value(QStringLiteral("operationId")).toString();
        QVERIFY(operationId.startsWith(QStringLiteral("OP-")));
        const qint64 version1 = first.data.value(QStringLiteral("changeVersion")).toInteger();
        QVERIFY(version1 >= 2);  // 迁移 010 初值 1，首笔业务写至少到 2
        QVERIFY(!first.data.value(QStringLiteral("serverTimeUtc")).toString().isEmpty());

        const auto second = router.route(
            makeMessage(QStringLiteral("REQ-W2"), QStringLiteral("WALLET_RECHARGE"),
                        {{QStringLiteral("userToken"), userToken},
                         {QStringLiteral("amountYuan"), QStringLiteral("5.25")}}),
            context);
        QVERIFY2(second.ok, qPrintable(second.message));
        const qint64 version2 = second.data.value(QStringLiteral("changeVersion")).toInteger();
        QVERIFY2(version2 > version1, "连续两次写操作的 changeVersion 必须严格递增");
    }

    // 查询操作响应必须携带 snapshotVersion/snapshotAtUtc。
    void queryResponsesCarrySnapshotMetadata()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        RequestRouter router;
        RequestContext context{testDb.database()};

        const auto response = router.route(
            makeMessage(QStringLiteral("REQ-Q1"), QStringLiteral("STATION_NEARBY"),
                        {{QStringLiteral("latitude"), 24.4798},
                         {QStringLiteral("longitude"), 118.0894}}),
            context);
        QVERIFY2(response.ok, qPrintable(response.message));
        QVERIFY(response.data.value(QStringLiteral("snapshotVersion")).toInteger() >= 1);
        QVERIFY(response.data.value(QStringLiteral("snapshotAtUtc")).toString()
                    .endsWith(QStringLiteral("Z")));
    }

    // 跨端核心断言：运营把桩设为 FAULT 后，用户侧站点详情不再提供该桩为可预约，
    // 运营总览的桩状态分布同步变化；再恢复 IDLE 后可预约恢复。
    void pileStatusChangeIsVisibleAcrossEnds()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        QVERIFY(AdminService(testDb.database()).ensureDefaultAdmin().ok);
        RequestRouter router;
        RequestContext context{testDb.database()};

        // 管理员登录取 token。
        const auto login = router.route(
            makeMessage(QStringLiteral("REQ-A0"), QStringLiteral("ADMIN_LOGIN"),
                        {{QStringLiteral("username"), QStringLiteral("admin")},
                         {QStringLiteral("password"), QStringLiteral("123456")}}),
            context);
        QVERIFY2(login.ok, qPrintable(login.message));
        const QString adminToken = login.data.value(QStringLiteral("adminToken")).toString();
        QVERIFY(!adminToken.isEmpty());

        // 建站点与可用桩（TestDatabase 不预置站点）。
        const qint64 stationId = testDb.addStation(QStringLiteral("联动验收站"));
        QVERIFY(stationId > 0);
        const qint64 pileId = testDb.addPile(stationId, QStringLiteral("PILE-LINK-001"));
        QVERIFY(pileId > 0);

        // 用户侧先看到 1 台可用桩。
        const auto before = router.route(
            makeMessage(QStringLiteral("REQ-U0"), QStringLiteral("STATION_DETAIL"),
                        {{QStringLiteral("stationId"), stationId}}),
            context);
        QVERIFY2(before.ok, qPrintable(before.message));
        bool idleVisible = false;
        for (const auto &pile : before.data.value(QStringLiteral("piles")).toArray()) {
            if (pile.toObject().value(QStringLiteral("pileId")).toInteger() == pileId) {
                idleVisible = pile.toObject().value(QStringLiteral("status")).toString()
                    == QStringLiteral("IDLE");
            }
        }
        QVERIFY2(idleVisible, "改桩前用户侧必须看到 IDLE 可预约桩");
        QVERIFY(pileId > 0);

        // 运营把桩设为 FAULT（写操作，带联动元数据）。
        const auto fault = router.route(
            makeMessage(QStringLiteral("REQ-A1"), QStringLiteral("ADMIN_PILE_SET_STATUS"),
                        {{QStringLiteral("adminToken"), adminToken},
                         {QStringLiteral("pileId"), pileId},
                         {QStringLiteral("status"), QStringLiteral("FAULT")},
                         {QStringLiteral("reason"), QStringLiteral("联动验收")}}),
            context);
        QVERIFY2(fault.ok, qPrintable(fault.message));
        QVERIFY(fault.data.value(QStringLiteral("operationId")).toString()
                    .startsWith(QStringLiteral("OP-")));

        // 用户侧站点详情：该桩不再可用（状态 FAULT）。
        const auto afterFault = router.route(
            makeMessage(QStringLiteral("REQ-U1"), QStringLiteral("STATION_DETAIL"),
                        {{QStringLiteral("stationId"), stationId}}),
            context);
        QVERIFY2(afterFault.ok, qPrintable(afterFault.message));
        bool faultVisible = false;
        for (const auto &pile : afterFault.data.value(QStringLiteral("piles")).toArray()) {
            if (pile.toObject().value(QStringLiteral("pileId")).toInteger() == pileId) {
                faultVisible = pile.toObject().value(QStringLiteral("status")).toString()
                    == QStringLiteral("FAULT");
            }
        }
        QVERIFY2(faultVisible, "运营改桩后用户侧必须看到 FAULT 状态");

        // 运营总览：桩状态分布与最近清单存在，且查询带快照元数据。
        const auto overview = router.route(
            makeMessage(QStringLiteral("REQ-A2"), QStringLiteral("ADMIN_OVERVIEW"),
                        {{QStringLiteral("adminToken"), adminToken}}),
            context);
        QVERIFY2(overview.ok, qPrintable(overview.message));
        QCOMPARE(overview.data.value(QStringLiteral("pileStatusCounts"))
                     .toObject().value(QStringLiteral("FAULT")).toInt(), 1);
        QVERIFY(!overview.data.value(QStringLiteral("loadSeries24h")).toArray().isEmpty());
        QCOMPARE(overview.data.value(QStringLiteral("loadSeries24h")).toArray().size(), 24);
        QVERIFY(overview.data.value(QStringLiteral("snapshotVersion")).toInteger() >= 2);

        // 恢复 IDLE：用户侧重新可预约。
        const auto restore = router.route(
            makeMessage(QStringLiteral("REQ-A3"), QStringLiteral("ADMIN_PILE_SET_STATUS"),
                        {{QStringLiteral("adminToken"), adminToken},
                         {QStringLiteral("pileId"), pileId},
                         {QStringLiteral("status"), QStringLiteral("IDLE")},
                         {QStringLiteral("reason"), QStringLiteral("恢复")}}),
            context);
        QVERIFY2(restore.ok, qPrintable(restore.message));
        const auto afterRestore = router.route(
            makeMessage(QStringLiteral("REQ-U2"), QStringLiteral("STATION_DETAIL"),
                        {{QStringLiteral("stationId"), stationId}}),
            context);
        QVERIFY2(afterRestore.ok, qPrintable(afterRestore.message));
        bool idleRestored = false;
        for (const auto &pile : afterRestore.data.value(QStringLiteral("piles")).toArray()) {
            if (pile.toObject().value(QStringLiteral("pileId")).toInteger() == pileId) {
                idleRestored = pile.toObject().value(QStringLiteral("status")).toString()
                    == QStringLiteral("IDLE");
            }
        }
        QVERIFY2(idleRestored, "恢复后用户侧必须重新看到可用桩");
    }
};

QTEST_MAIN(LinkageContractTest)
#include "linkage_contract_test.moc"
