#pragma once

#include <QFrame>

class QLabel;

namespace Ui {

// 总览页指标卡（审查 01 规格）：标题 + 主数值 + 单位 + 口径说明行
// （口径行固定展示“统计时间 / 口径 / 快照版本”，由页面统一注入）。
class MetricCard final : public QFrame
{
    Q_OBJECT
public:
    explicit MetricCard(const QString &title, QWidget *parent = nullptr);
    // 设置主数值与单位（数值由调用方保持两位小数字符串口径）。
    void setValue(const QString &value, const QString &unit);
    // 设置底部说明（时间/口径/快照版本）。
    void setCaption(const QString &caption);
    QString value() const;

private:
    QLabel *title_;
    QLabel *value_;
    QLabel *unit_;
    QLabel *caption_;
};

}  // namespace Ui
