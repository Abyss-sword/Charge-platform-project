// 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
// 用户服务单测：验证登录/注册、新用户赠送 100.00 元、登录防重与冻结限制——
// 同一用户/请求不会重复赠送。

#include <QSqlQuery>
#include <QSqlDatabase>
#include <QtTest>

#include <condition_variable>
#include <future>
#include <mutex>

#include "server/services/user_service.h"
#include "tests/helpers/test_database.h"

class UserServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void rejectsPhonesOutsideMainlandMobilePattern()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        UserService service(testDb.database());

        const QStringList invalid{"", "12812345678", "1381234567", "138123456789",
                                  "+8613812345678", "138 1234 5678", "abcdefghijk"};
        for (const auto &phone : invalid) {
            const auto result = service.loginOrRegister(phone, QStringLiteral("REQ-INVALID-") + phone);
            QVERIFY2(!result.ok, qPrintable(phone));
            QCOMPARE(result.errorCode, QStringLiteral("PARAM_INVALID"));
        }
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT COUNT(*) FROM user_info")).toInt(), 0);
    }

    void createsNewUserOnlyOnceWithInitialCredit()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        UserService service(testDb.database());

        const auto first = service.loginOrRegister(QStringLiteral("13988886666"),
                                                   QStringLiteral("REQ-LOGIN-1"));
        const auto second = service.loginOrRegister(QStringLiteral("13988886666"),
                                                    QStringLiteral("REQ-LOGIN-2"));

        QVERIFY(first.ok);
        QVERIFY(second.ok);
        QCOMPARE(first.userId, second.userId);
        QCOMPARE(first.nickname, QStringLiteral("用户6666"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM user_info WHERE phone='13988886666'" )).toInt(), 1);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT balance_yuan FROM user_info WHERE id=?"), {first.userId}).toString(),
            QStringLiteral("100.00"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM wallet_transaction WHERE user_id=? "
            "AND transaction_type='INITIAL_CREDIT' AND amount_yuan='100.00'"),
            {first.userId}).toInt(), 1);
    }

    void frozenUserReceivesRestrictedSession()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800138000"),
                                             QStringLiteral("100.00"),
                                            QStringLiteral("FROZEN"));
        QVERIFY(userId > 0);
        UserService service(testDb.database());

        const auto result = service.loginOrRegister(QStringLiteral("13800138000"),
                                                    QStringLiteral("REQ-FROZEN"));
        QVERIFY(result.ok);
        QCOMPARE(result.userId, userId);
        QVERIFY(result.restricted);
    }

    void reusedRequestIdCannotTargetAnotherPhone()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        UserService service(testDb.database());

        const auto first = service.loginOrRegister(QStringLiteral("13911112222"),
                                                   QStringLiteral("REQ-REPLAY"));
        const auto replay = service.loginOrRegister(QStringLiteral("13911112222"),
                                                    QStringLiteral("REQ-REPLAY"));
        const auto conflict = service.loginOrRegister(QStringLiteral("13933334444"),
                                                      QStringLiteral("REQ-REPLAY"));

        QVERIFY(first.ok);
        QVERIFY(replay.ok);
        QCOMPARE(replay.userId, first.userId);
        QVERIFY(!conflict.ok);
        QCOMPARE(conflict.errorCode, QStringLiteral("DUPLICATE_REQUEST"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT COUNT(*) FROM user_info")).toInt(), 1);
    }

    void databaseFailureIsNotTreatedAsMissingUser()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        auto database = testDb.database();
        database.close();
        UserService service(database);

        const auto result = service.loginOrRegister(QStringLiteral("13955556666"),
                                                    QStringLiteral("REQ-DB-DOWN"));
        QVERIFY(!result.ok);
        QCOMPARE(result.errorCode, QStringLiteral("DATABASE_ERROR"));
    }

    void concurrentReplayReturnsOriginalResult()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        std::mutex mutex;
        std::condition_variable gate;
        int ready = 0;
        bool go = false;
        const auto attempt = [&] {
            auto database = testDb.openConnection(QStringLiteral("login_replay"));
            const QString connectionName = database.connectionName();
            {
                std::unique_lock lock(mutex);
                ++ready;
                gate.notify_all();
                gate.wait(lock, [&] { return go; });
            }
            LoginResult result;
            {
                UserService service(database);
                result = service.loginOrRegister(QStringLiteral("13977778888"),
                                                 QStringLiteral("REQ-CONCURRENT-LOGIN"));
            }
            database.close();
            database = {};
            QSqlDatabase::removeDatabase(connectionName);
            return result;
        };
        auto first = std::async(std::launch::async, attempt);
        auto second = std::async(std::launch::async, attempt);
        {
            std::unique_lock lock(mutex);
            gate.wait(lock, [&] { return ready == 2; });
            go = true;
        }
        gate.notify_all();
        const auto firstResult = first.get();
        const auto secondResult = second.get();
        QVERIFY(firstResult.ok);
        QVERIFY(secondResult.ok);
        QCOMPARE(firstResult.userId, secondResult.userId);
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT COUNT(*) FROM user_info")).toInt(), 1);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM wallet_transaction WHERE transaction_type='INITIAL_CREDIT'" )).toInt(), 1);
    }

    void concurrentRequestIdConflictRollsBackLosingRegistration()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        std::mutex mutex;
        std::condition_variable gate;
        int ready = 0;
        bool go = false;
        const auto attempt = [&](const QString &phone) {
            auto database = testDb.openConnection(QStringLiteral("login_conflict"));
            const QString connectionName = database.connectionName();
            {
                std::unique_lock lock(mutex);
                ++ready;
                gate.notify_all();
                gate.wait(lock, [&] { return go; });
            }
            LoginResult result;
            {
                UserService service(database);
                result = service.loginOrRegister(phone, QStringLiteral("REQ-CONCURRENT-CONFLICT"));
            }
            database.close();
            database = {};
            QSqlDatabase::removeDatabase(connectionName);
            return result;
        };
        auto first = std::async(std::launch::async, attempt, QStringLiteral("13911110000"));
        auto second = std::async(std::launch::async, attempt, QStringLiteral("13922220000"));
        {
            std::unique_lock lock(mutex);
            gate.wait(lock, [&] { return ready == 2; });
            go = true;
        }
        gate.notify_all();
        const auto firstResult = first.get();
        const auto secondResult = second.get();
        QCOMPARE(static_cast<int>(firstResult.ok) + static_cast<int>(secondResult.ok), 1);
        const auto &loser = firstResult.ok ? secondResult : firstResult;
        QCOMPARE(loser.errorCode, QStringLiteral("DUPLICATE_REQUEST"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT COUNT(*) FROM user_info")).toInt(), 1);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM wallet_transaction WHERE transaction_type='INITIAL_CREDIT'" )).toInt(), 1);
    }
};

QTEST_GUILESS_MAIN(UserServiceTest)
#include "user_service_test.moc"
