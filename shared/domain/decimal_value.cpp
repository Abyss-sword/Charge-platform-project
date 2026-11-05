#include "shared/domain/decimal_value.h"

#include <QRegularExpression>

#include <ios>
#include <utility>

namespace {
// 输入格式：可选负号 + 整数部分 + 可选小数部分（用于一般解析）。
const QRegularExpression InputPattern(QStringLiteral("^-?[0-9]+(?:\\.[0-9]+)?$"));
// 规范格式：无多余前导零、必须恰两位小数（用于充值金额等严格入口）。
const QRegularExpression CanonicalPattern(QStringLiteral("^-?(?:0|[1-9][0-9]*)\\.[0-9]{2}$"));
}

DecimalValue::DecimalValue() = default;

DecimalValue::DecimalValue(Number value)
    : value_(std::move(value))
{
}

// 一般解析入口：长度上限 64 + 正则拦截非法文本，构造高精度数后直接量化两位；
// Boost 构造抛出的任何异常都吞掉并返回 nullopt，不让异常穿透到业务层。
std::optional<DecimalValue> DecimalValue::parseRounded(const QString &text)
{
    if (text.size() > 64 || !InputPattern.match(text).hasMatch()) {
        return std::nullopt;
    }

    try {
        return DecimalValue(roundToTwo(Number(text.toStdString())));
    } catch (...) {
        return std::nullopt;
    }
}

// 严格解析入口：先按规范格式正则校验，再解析并序列化回环比对（“10.270”虽然匹配两位，
// 但 round-trip 后变成 “10.27”，与原文不等 → 拒绝），确保输入本身已是规范两位值。
std::optional<DecimalValue> DecimalValue::parseCanonical(const QString &text)
{
    if (!CanonicalPattern.match(text).hasMatch()) {
        return std::nullopt;
    }
    const auto value = parseRounded(text);
    if (!value || value->toString() != text) {
        return std::nullopt;
    }
    return value;
}

// 序列化：str(2, fixed) 输出恰两位小数的十进制文本（如 "10.27"），这是 JSON/SQLite 的统一格式。
QString DecimalValue::toString() const
{
    return QString::fromStdString(value_.str(2, std::ios_base::fixed));
}

bool DecimalValue::isNegative() const
{
    return value_ < 0;
}

bool DecimalValue::isZero() const
{
    return value_ == 0;
}

// 两位舍入算法（公共算法核心，答辩高频）：
// 令 s = x × 100；非负数 floor(s + 0.5)，负数 ceil(s - 0.5)，再除回 100。
// 即“正好一半时远离零”：10.2705 → 10.27，10.275 → 10.28。
// 期间全程使用高精度十进制，不经过 double。units==0 时归一化负零。
DecimalValue::Number DecimalValue::roundToTwo(Number value)
{
    const Number scaled = value * 100;
    const Number units = scaled < 0
        ? boost::multiprecision::ceil(scaled - Number("0.5"))
        : boost::multiprecision::floor(scaled + Number("0.5"));
    return units == 0 ? Number(0) : units / 100;
}

bool operator==(const DecimalValue &left, const DecimalValue &right)
{
    return left.value_ == right.value_;
}

bool operator!=(const DecimalValue &left, const DecimalValue &right)
{
    return !(left == right);
}

bool operator<(const DecimalValue &left, const DecimalValue &right)
{
    return left.value_ < right.value_;
}

bool operator<=(const DecimalValue &left, const DecimalValue &right)
{
    return !(right < left);
}

bool operator>(const DecimalValue &left, const DecimalValue &right)
{
    return right < left;
}

bool operator>=(const DecimalValue &left, const DecimalValue &right)
{
    return !(left < right);
}

// 加减法：高精度运算后仍量化两位，保证任意运算结果都在业务两位契约内。
DecimalValue operator+(const DecimalValue &left, const DecimalValue &right)
{
    return DecimalValue(DecimalValue::roundToTwo(left.value_ + right.value_));
}

DecimalValue operator-(const DecimalValue &left, const DecimalValue &right)
{
    return DecimalValue(DecimalValue::roundToTwo(left.value_ - right.value_));
}

// 计费乘法：电量 × 单价，高精度相乘后量化两位（8.35 kWh × 1.23 元/kWh → "10.27"）。
DecimalValue multiplyRounded(const DecimalValue &left, const DecimalValue &right)
{
    return DecimalValue(DecimalValue::roundToTwo(left.value_ * right.value_));
}

// S1-B 派生指标（平均客单价/平均单价）：高精度相除后两位舍入。
// 除数为 0 返回 0.00；调用方（营收汇总）已先行守卫，这里兜底避免抛异常。
DecimalValue divideRounded(const DecimalValue &left, const DecimalValue &right)
{
    if (right.value_ == 0) {
        return DecimalValue();
    }
    return DecimalValue(DecimalValue::roundToTwo(left.value_ / right.value_));
}
