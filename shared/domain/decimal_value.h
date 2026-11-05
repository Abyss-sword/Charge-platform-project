#pragma once

#include <boost/multiprecision/cpp_dec_float.hpp>

#include <QString>

#include <optional>

// DecimalValue：核心金额/电量统一十进制类型（公共算法之一，charge_domain 库）。
// 内部使用 Boost cpp_dec_float_50（50 位有效十进制数字）做高精度运算，避免二进制浮点误差；
// 对外约定：输入输出均为字符串，金额/电量一律量化保留两位小数。
// 答辩口径：不是“整数分存储”，也不会永久保留输入的全部小数位——两位量化是当前业务契约。
class DecimalValue
{
public:
    DecimalValue();

    // 从任意十进制文本解析（如 "10.2705"），量化到两位（"10.27"）；超长/格式非法返回 nullopt。
    static std::optional<DecimalValue> parseRounded(const QString &text);
    // 仅接受严格规范文本（无多余前导零、必须恰两位小数，如 "10.27"）；
    // 解析后再序列化做回环校验，用于充值/计费等要求输入本身已是两位的入口。
    static std::optional<DecimalValue> parseCanonical(const QString &text);

    // 序列化为固定两位小数字符串（JSON 与数据库中的金额/电量均用此文本格式）。
    QString toString() const;
    bool isNegative() const;
    bool isZero() const;

    // 比较与加减：加减结果同样经过两位量化。
    friend bool operator==(const DecimalValue &left, const DecimalValue &right);
    friend bool operator!=(const DecimalValue &left, const DecimalValue &right);
    friend bool operator<(const DecimalValue &left, const DecimalValue &right);
    friend bool operator<=(const DecimalValue &left, const DecimalValue &right);
    friend bool operator>(const DecimalValue &left, const DecimalValue &right);
    friend bool operator>=(const DecimalValue &left, const DecimalValue &right);
    friend DecimalValue operator+(const DecimalValue &left, const DecimalValue &right);
    friend DecimalValue operator-(const DecimalValue &left, const DecimalValue &right);
    // 乘法即计费核心：电量(kWh) × 单价(元/kWh)，高精度相乘后量化两位（如 8.35×1.23=10.27）。
    friend DecimalValue multiplyRounded(const DecimalValue &left, const DecimalValue &right);
    // S1-B 派生指标：高精度相除后量化两位（如 10.27/3=3.42）。除数为 0 返回 0.00（调用方应先行守卫）。
    friend DecimalValue divideRounded(const DecimalValue &left, const DecimalValue &right);

private:
    // 底层高精度十进制类型：cpp_dec_float_50，50 位十进制精度。
    using Number = boost::multiprecision::cpp_dec_float_50;

    explicit DecimalValue(Number value);
    // 两位舍入（half away from zero：正好一半时远离零）。
    static Number roundToTwo(Number value);

    Number value_ = 0;
};
