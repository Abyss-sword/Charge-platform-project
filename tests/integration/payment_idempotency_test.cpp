// 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
// 支付幂等集成测试：重复支付不重复扣款、余额不足回滚无残留、故障桩结算、零金额订单、
// 充值限制与资料限制——资金安全的核心证据。

#include <QFile>
#include <QTemporaryFile>
#include <QtTest>

#include "server/services/wallet_service.h"
#include "tests/helpers/test_database.h"

class PaymentIdempotencyTest final : public QObject
{
    Q_OBJECT

private:
    static QString addPendingOrder(TestDatabase &testDb, qint64 userId, qint64 pileId,
                                   const QString &amountYuan, const QString &suffix)
    {
        const QString orderId = QStringLiteral("ORDER-") + suffix;
        if (!testDb.exec(QStringLiteral(
                "INSERT INTO charge_order(id,user_id,pile_id,status,energy_kwh,amount_yuan,"
                "unit_price_yuan_per_kwh) "
                "VALUES(?,?,?,'PENDING_PAYMENT','10.00',?,'1.20')"),
                {orderId, userId, pileId, amountYuan})) {
            return {};
        }
        return orderId;
    }

private slots:
    void samePaymentRequestChargesOnceAndFinishesOrder()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800138101"),
                                             QStringLiteral("100.00"));
        const qint64 pileId = testDb.addPile(testDb.addStation(), QStringLiteral("PILE-PAY"),
                                            QStringLiteral("CHARGING"));
        const QString orderId = addPendingOrder(
            testDb, userId, pileId, QStringLiteral("12.00"), QStringLiteral("PAY"));
        QVERIFY(!orderId.isEmpty());
        WalletService service(testDb.database());

        const auto first = service.pay(orderId, QStringLiteral("REQ-PAY-1"));
        const auto second = service.pay(orderId, QStringLiteral("REQ-PAY-1"));

        QVERIFY(first.ok);
        QVERIFY(second.ok);
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT balance_yuan FROM user_info WHERE id=?"),
                               {userId}).toString(), QStringLiteral("88.00"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM wallet_transaction WHERE request_id='REQ-PAY-1' "
            "AND transaction_type='PAYMENT' AND amount_yuan='-12.00'" )).toInt(), 1);
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM charge_order WHERE id=?"),
                               {orderId}).toString(), QStringLiteral("FINISHED"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM pile_info WHERE id=?"),
                               {pileId}).toString(), QStringLiteral("IDLE"));
    }

    void insufficientBalanceRollsBackEveryPaymentWrite()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800138102"),
                                             QStringLiteral("11.99"));
        const qint64 pileId = testDb.addPile(testDb.addStation(), QStringLiteral("PILE-POOR"),
                                            QStringLiteral("CHARGING"));
        const QString orderId = addPendingOrder(
            testDb, userId, pileId, QStringLiteral("12.00"), QStringLiteral("POOR"));
        QVERIFY(!orderId.isEmpty());
        WalletService service(testDb.database());

        const auto result = service.pay(orderId, QStringLiteral("REQ-PAY-POOR"));

        QVERIFY(!result.ok);
        QCOMPARE(result.errorCode, QStringLiteral("BALANCE_INSUFFICIENT"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT balance_yuan FROM user_info WHERE id=?"),
                               {userId}).toString(), QStringLiteral("11.99"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM charge_order WHERE id=?"),
                               {orderId}).toString(), QStringLiteral("PENDING_PAYMENT"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM pile_info WHERE id=?"),
                               {pileId}).toString(), QStringLiteral("CHARGING"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM wallet_transaction WHERE request_id='REQ-PAY-POOR'" )).toInt(), 0);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM request_dedup WHERE request_id='REQ-PAY-POOR'" )).toInt(), 0);
    }

    void faultedPileDoesNotBlockPaymentCompletion()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800138106"),
                                             QStringLiteral("100.00"));
        const qint64 pileId = testDb.addPile(testDb.addStation(),
                                             QStringLiteral("PILE-PAY-FAULT"),
                                             QStringLiteral("FAULT"));
        const QString orderId = addPendingOrder(
            testDb, userId, pileId, QStringLiteral("12.00"), QStringLiteral("FAULT"));
        QVERIFY(!orderId.isEmpty());

        const auto result = WalletService(testDb.database()).pay(
            orderId, QStringLiteral("REQ-PAY-FAULT"));

        QVERIFY(result.ok);
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT balance_yuan FROM user_info WHERE id=?"),
                               {userId}).toString(), QStringLiteral("88.00"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM charge_order WHERE id=?"),
                               {orderId}).toString(), QStringLiteral("FINISHED"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT status FROM pile_info WHERE id=?"),
                               {pileId}).toString(), QStringLiteral("FAULT"));
    }

    void zeroAmountPaymentFinishesWithoutWalletTransactionAndIsIdempotent()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800138105"),
                                             QStringLiteral("7.77"));
        const qint64 pileId = testDb.addPile(testDb.addStation(), QStringLiteral("PILE-FREE"),
                                            QStringLiteral("CHARGING"));
        const QString orderId = addPendingOrder(testDb, userId, pileId, QStringLiteral("0.00"),
                                                QStringLiteral("FREE"));
        QVERIFY(!orderId.isEmpty());
        WalletService service(testDb.database());

        const auto first = service.pay(orderId, QStringLiteral("REQ-PAY-FREE"));
        const auto second = service.pay(orderId, QStringLiteral("REQ-PAY-FREE"));

        QVERIFY(first.ok);
        QVERIFY(second.ok);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT balance_yuan FROM user_info WHERE id=?"), {userId}).toString(),
            QStringLiteral("7.77"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM wallet_transaction WHERE request_id='REQ-PAY-FREE'" )).toInt(), 0);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM request_dedup WHERE request_id='REQ-PAY-FREE' "
            "AND action='PAYMENT'" )).toInt(), 1);
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT status FROM charge_order WHERE id=?"), {orderId}).toString(),
            QStringLiteral("FINISHED"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT status FROM pile_info WHERE id=?"), {pileId}).toString(),
            QStringLiteral("IDLE"));
    }

    void rechargeEnforcesRangeAndIsIdempotent()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800138103"),
                                             QStringLiteral("0.00"));
        WalletService service(testDb.database());

        QVERIFY(!service.recharge(userId, DecimalValue{}, QStringLiteral("REQ-R-0")).ok);
        QVERIFY(!service.recharge(
            userId, *DecimalValue::parseCanonical(QStringLiteral("10000.01")),
            QStringLiteral("REQ-R-HIGH")).ok);
        QVERIFY(service.recharge(
            userId, *DecimalValue::parseCanonical(QStringLiteral("1.00")),
            QStringLiteral("REQ-R-MIN")).ok);
        const auto first = service.recharge(
            userId, *DecimalValue::parseCanonical(QStringLiteral("10000.00")),
            QStringLiteral("REQ-R-1"));
        const auto second = service.recharge(
            userId, *DecimalValue::parseCanonical(QStringLiteral("10000.00")),
            QStringLiteral("REQ-R-1"));

        QVERIFY(first.ok && second.ok);
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT balance_yuan FROM user_info WHERE id=?"),
                               {userId}).toString(), QStringLiteral("10001.00"));
        QCOMPARE(testDb.scalar(QStringLiteral(
            "SELECT COUNT(*) FROM wallet_transaction WHERE request_id='REQ-R-1' "
            "AND transaction_type='RECHARGE' AND amount_yuan='10000.00'" )).toInt(), 1);
    }

    void profileValidatesNicknameAndAvatar()
    {
        TestDatabase testDb;
        QVERIFY2(testDb.isReady(), qPrintable(testDb.error()));
        const qint64 userId = testDb.addUser(QStringLiteral("13800138104"));
        WalletService service(testDb.database());

        QCOMPARE(service.updateProfile(userId, QStringLiteral("短"), QString{}).errorCode,
                 QStringLiteral("PARAM_INVALID"));
        QCOMPARE(service.updateProfile(userId, QString(21, QChar('a')), QString{}).errorCode,
                 QStringLiteral("PARAM_INVALID"));

        QTemporaryFile textFile(QDir::tempPath() + QStringLiteral("/avatar-XXXXXX.txt"));
        QVERIFY(textFile.open());
        textFile.write("not-an-image");
        textFile.flush();
        QCOMPARE(service.updateProfile(userId, QStringLiteral("有效昵称"), textFile.fileName()).errorCode,
                 QStringLiteral("PARAM_INVALID"));

        QTemporaryFile largeJpg(QDir::tempPath() + QStringLiteral("/avatar-XXXXXX.jpg"));
        QVERIFY(largeJpg.open());
        // 01 规格：头像上限 5MiB；超过 1 字节即拒绝（不再沿用旧的 2MiB 上限）。
        QVERIFY(largeJpg.write(QByteArray(5 * 1024 * 1024 + 1, 'x')) > 0);
        largeJpg.flush();
        QCOMPARE(service.updateProfile(userId, QStringLiteral("有效昵称"), largeJpg.fileName()).errorCode,
                 QStringLiteral("PARAM_INVALID"));

        // 5MiB 以内的合法 JPEG（FF D8 FF 魔数）应通过：覆盖旧 2MiB 阈值与新 5MiB 阈值之间的区间。
        QTemporaryFile midJpeg(QDir::tempPath() + QStringLiteral("/avatar-XXXXXX.jpg"));
        QVERIFY(midJpeg.open());
        QByteArray jpegBytes = QByteArray::fromHex("ffd8ff") + QByteArray(3 * 1024 * 1024, '\0');
        QCOMPARE(midJpeg.write(jpegBytes), jpegBytes.size());
        midJpeg.flush();
        QVERIFY2(service.updateProfile(userId, QStringLiteral("有效昵称"), midJpeg.fileName()).ok,
                 "3MiB JPEG 应符合 5MiB 上限");

        // 合法 WebP（RIFF....WEBP 魔数）应被接受（01 规格含 WebP）。
        QTemporaryFile webpFile(QDir::tempPath() + QStringLiteral("/avatar-XXXXXX.webp"));
        QVERIFY(webpFile.open());
        QByteArray webpBytes = QByteArray("RIFF") + QByteArray(4, '\0') + QByteArray("WEBP")
                               + QByteArray(256, '\0');
        QCOMPARE(webpFile.write(webpBytes), webpBytes.size());
        webpFile.flush();
        QVERIFY2(service.updateProfile(userId, QStringLiteral("有效昵称"), webpFile.fileName()).ok,
                 "WebP 应符合 01 规格");

        QTemporaryFile validPng(QDir::tempPath() + QStringLiteral("/avatar-XXXXXX.png"));
        QVERIFY(validPng.open());
        const QByteArray onePixelPng = QByteArray::fromBase64(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk"
            "YAAAAAYAAjCB0C8AAAAASUVORK5CYII=");
        QCOMPARE(validPng.write(onePixelPng), onePixelPng.size());
        validPng.flush();
        QVERIFY(service.updateProfile(userId, QStringLiteral("有效昵称"), validPng.fileName()).ok);
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT nickname FROM user_info WHERE id=?"),
                               {userId}).toString(), QStringLiteral("有效昵称"));
        QCOMPARE(testDb.scalar(QStringLiteral("SELECT avatar_url FROM user_info WHERE id=?"),
                               {userId}).toString(), validPng.fileName());
    }
};

QTEST_GUILESS_MAIN(PaymentIdempotencyTest)
#include "payment_idempotency_test.moc"
