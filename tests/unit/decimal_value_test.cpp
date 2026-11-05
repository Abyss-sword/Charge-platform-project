// 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
// 十进制单测：验证两位舍入边界（10.2705→10.27、10.275→10.28）、规范格式校验与加减乘运算——
// 核心计费精度的直接证据。

#include "shared/domain/decimal_value.h"

#include <QTest>

class DecimalValueTest : public QObject
{
    Q_OBJECT

private slots:
    void roundsHalfUp();
    void roundsNegativeHalfUp();
    void formatsExactlyTwoDecimals();
    void rejectsInvalidDecimalText();
    void acceptsOnlyCanonicalStorageText();
    void multipliesAndRoundsOnce();
    void dividesAndRoundsOnce();
};

void DecimalValueTest::roundsHalfUp()
{
    QCOMPARE(DecimalValue::parseRounded(QStringLiteral("10.2705"))->toString(),
             QStringLiteral("10.27"));
    QCOMPARE(DecimalValue::parseRounded(QStringLiteral("10.275"))->toString(),
             QStringLiteral("10.28"));
    QCOMPARE(DecimalValue::parseRounded(QStringLiteral("8.344"))->toString(),
             QStringLiteral("8.34"));
    QCOMPARE(DecimalValue::parseRounded(QStringLiteral("8.345"))->toString(),
             QStringLiteral("8.35"));
}

void DecimalValueTest::roundsNegativeHalfUp()
{
    QCOMPARE(DecimalValue::parseRounded(QStringLiteral("-10.275"))->toString(),
             QStringLiteral("-10.28"));
    QCOMPARE(DecimalValue::parseRounded(QStringLiteral("-10.274"))->toString(),
             QStringLiteral("-10.27"));
}

void DecimalValueTest::formatsExactlyTwoDecimals()
{
    QCOMPARE(DecimalValue::parseRounded(QStringLiteral("10"))->toString(),
             QStringLiteral("10.00"));
    QCOMPARE(DecimalValue::parseRounded(QStringLiteral("0.1"))->toString(),
             QStringLiteral("0.10"));
    QCOMPARE(DecimalValue{}.toString(), QStringLiteral("0.00"));
}

void DecimalValueTest::rejectsInvalidDecimalText()
{
    QVERIFY(!DecimalValue::parseRounded(QStringLiteral("")));
    QVERIFY(!DecimalValue::parseRounded(QStringLiteral("1e3")));
    QVERIFY(!DecimalValue::parseRounded(QStringLiteral("NaN")));
    QVERIFY(!DecimalValue::parseRounded(QStringLiteral("10元")));
    QVERIFY(!DecimalValue::parseRounded(QStringLiteral(" 10.00")));
    QVERIFY(!DecimalValue::parseRounded(QStringLiteral("10.00 ")));
}

void DecimalValueTest::acceptsOnlyCanonicalStorageText()
{
    QVERIFY(DecimalValue::parseCanonical(QStringLiteral("10.00")));
    QVERIFY(DecimalValue::parseCanonical(QStringLiteral("-10.28")));
    QVERIFY(!DecimalValue::parseCanonical(QStringLiteral("10")));
    QVERIFY(!DecimalValue::parseCanonical(QStringLiteral("10.0")));
    QVERIFY(!DecimalValue::parseCanonical(QStringLiteral("10.000")));
}

void DecimalValueTest::multipliesAndRoundsOnce()
{
    const auto energy = DecimalValue::parseRounded(QStringLiteral("8.35"));
    const auto price = DecimalValue::parseRounded(QStringLiteral("1.23"));
    QVERIFY(energy);
    QVERIFY(price);
    QCOMPARE(multiplyRounded(*energy, *price).toString(), QStringLiteral("10.27"));
}

// S1-B 派生指标：divideRounded 高精度相除后两位舍入；除数为 0 回落 0.00。
void DecimalValueTest::dividesAndRoundsOnce()
{
    const auto ten = DecimalValue::parseCanonical(QStringLiteral("10.27"));
    QVERIFY(ten);
    const auto three = DecimalValue::parseRounded(QStringLiteral("3"));
    QVERIFY(three);
    // 10.27/3 = 3.4233… → 两位 HALF_UP 为 3.42。
    QCOMPARE(divideRounded(*ten, *three).toString(), QStringLiteral("3.42"));
    const auto ten00 = DecimalValue::parseCanonical(QStringLiteral("10.00"));
    QVERIFY(ten00);
    const auto four = DecimalValue::parseRounded(QStringLiteral("4"));
    QVERIFY(four);
    // 10.00/4 = 2.50：整除也必须保持两位格式。
    QCOMPARE(divideRounded(*ten00, *four).toString(), QStringLiteral("2.50"));
    const auto zero = DecimalValue::parseCanonical(QStringLiteral("0.00"));
    QVERIFY(zero);
    // 除数为 0：返回 0.00（调用方应先行守卫，这里验证不抛异常不产生脏值）。
    QCOMPARE(divideRounded(*ten, *zero).toString(), QStringLiteral("0.00"));
}

QTEST_MAIN(DecimalValueTest)
#include "decimal_value_test.moc"
