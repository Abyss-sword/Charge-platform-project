// 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
// 管理服务集成测试：验证管理员认证（PBKDF2 派生与会话）、营收统计及管理服务行为。

#include <QSqlQuery>
#include <QtTest>

#include "server/services/admin_service.h"
#include "tests/helpers/test_database.h"

class AdminServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void defaultPasswordIsHashedAndLoginStillWorks()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        AdminService service(testDb.database());

        QVERIFY(service.ensureDefaultAdmin().ok);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM admin_user WHERE username='admin'" )).toInt(), 1);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM admin_user WHERE password_hash='123456' "
            "OR password_salt='123456'" )).toInt(), 0);
        QVERIFY(testDb.scalar(QStringLiteral(
            "SELECT length(password_hash) FROM admin_user WHERE username='admin'" )).toInt() >= 32);
        QVERIFY(testDb.scalar(QStringLiteral(
            "SELECT length(password_salt) FROM admin_user WHERE username='admin'" )).toInt() >= 16);
        const auto login = service.login(QStringLiteral("admin"), QStringLiteral("123456"));
        QVERIFY(login.ok);
        QVERIFY(!login.token.isEmpty());
        QCOMPARE(service.authenticatedAdmin(login.token), QStringLiteral("admin"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM admin_session WHERE token_hash=?"),
            {login.token}).toInt(), 0);
        QVERIFY(!service.login(QStringLiteral("admin"), QStringLiteral("wrong")).ok);
    }

    void revenueIncludesFinishedOrdersOnly()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800138201"));
        const qint64 stationId = testDb.addStation();
        const qint64 pile1 = testDb.addPile(stationId, QStringLiteral("PILE-REV-1"));
        const qint64 pile2 = testDb.addPile(stationId, QStringLiteral("PILE-REV-2"));
        const qint64 pile3 = testDb.addPile(stationId, QStringLiteral("PILE-REV-3"));
        QVERIFY(testDb.exec(QStringLiteral(
            "INSERT INTO charge_order(id,user_id,pile_id,status,amount_yuan) VALUES"
            "('ORDER-F',?,?,'FINISHED','10.27'),"
            "('ORDER-P',?,?,'PENDING_PAYMENT','9.00'),"
            "('ORDER-C',?,?,'CANCELED','7.00')"),
            {userId, pile1, userId, pile2, userId, pile3}));
        AdminService service(testDb.database());

        QCOMPARE(service.totalRevenueYuan().toString(), QStringLiteral("10.27"));
    }

    void freezeRequiresReasonAndWritesOperationLog()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800138202"));
        AdminService service(testDb.database());
        QVERIFY(service.ensureDefaultAdmin().ok);

        const auto missingReason = service.setUserFrozen(userId, true, QString{},
                                                         QStringLiteral("admin"));
        QVERIFY(!missingReason.ok);
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM user_info WHERE id=?"),
                               {userId}).toString(), QStringLiteral("NORMAL"));

        const QString reason = QStringLiteral("异常支付复核");
        QVERIFY(service.setUserFrozen(userId, true, reason, QStringLiteral("admin")).ok);
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM user_info WHERE id=?"),
                               {userId}).toString(), QStringLiteral("FROZEN"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM operation_log WHERE actor_id='admin' "
            "AND action='USER_FREEZE' AND target_type='USER' AND target_id=? "
            "AND result LIKE ?"), {QString::number(userId), QStringLiteral("%") + reason + "%"}).toInt(), 1);
    }

    void stationAndAllInitialPilesAreCreatedAtomically()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        AdminService service(testDb.database());
        const QList<NewPile> piles{
            {QStringLiteral("PILE-NEW-1"), QStringLiteral("FAST"), 60.0},
            {QStringLiteral("PILE-NEW-2"), QStringLiteral("SLOW"), 7.0},
            {QStringLiteral("PILE-NEW-3"), QStringLiteral("FAST"), 120.0}};

        const auto created = service.createStation(QStringLiteral("新站"),
                                                   QStringLiteral("新路8号"),
                                                   23.2, 113.3,
                                                   *DecimalValue::parseCanonical(
                                                       QStringLiteral("1.35")), piles);
        QVERIFY(created.ok);
        QVERIFY(created.stationId > 0);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM pile_info WHERE station_id=?"),
            {created.stationId}).toInt(), piles.size());

        const QList<NewPile> invalidPiles{
            {QStringLiteral("PILE-DUP"), QStringLiteral("FAST"), 60.0},
            {QStringLiteral("PILE-DUP"), QStringLiteral("FAST"), 60.0}};
        const auto rejected = service.createStation(QStringLiteral("应回滚站"),
                                                    QStringLiteral("回滚路1号"),
                                                    23.3, 113.4,
                                                    *DecimalValue::parseCanonical(
                                                        QStringLiteral("1.20")), invalidPiles);
        QVERIFY(!rejected.ok);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM station_info WHERE name='应回滚站'" )).toInt(), 0);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM pile_info WHERE pile_sn='PILE-DUP'" )).toInt(), 0);
    }

    void exposesTrendAndPagedOperationsDataForAdminPages()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800138203"));
        const qint64 stationId = testDb.addStation(QStringLiteral("查询站"));
        testDb.addPile(stationId, QStringLiteral("PILE-QUERY"));
        AdminService service(testDb.database());
        QVERIFY(service.setUserFrozen(userId, true, QStringLiteral("复核"),
                                      QStringLiteral("admin")).ok);

        QCOMPARE(service.revenueTrend(7).size(), 7);
        QCOMPARE(service.stations(QStringLiteral("查询")).size(), 1);
        QCOMPARE(service.piles(stationId).size(), 1);
        QCOMPARE(service.users(QStringLiteral("FROZEN")).size(), 1);
        const auto allUsers = service.users();
        QCOMPARE(allUsers.size(), 1);
        QCOMPARE(allUsers.first().value(QStringLiteral("phone")).toString(),
                 QStringLiteral("138****8203"));
        QCOMPARE(service.operationLogs(QStringLiteral("USER_FREEZE")).size(), 1);
    }
};

QTEST_GUILESS_MAIN(AdminServiceTest)
#include "admin_service_test.moc"
