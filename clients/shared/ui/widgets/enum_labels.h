#pragma once

class QComboBox;

#include <QList>
#include <QString>

namespace Ui {

// 固定枚举的“中文显示值 ↔ 协议值”二元组（审查 01 规格 · 候选栏改造）。
// 规则：界面只允许从候选栏选择（不可自由输入），显示中文、提交协议值，
// 服务端仍对协议值做最终校验。
struct EnumOption
{
    QString label;  // 中文显示值，如 “快充”。
    QString value;  // 协议值，如 “FAST”。
};

// 桩充电类型（pile_info.charge_type）。
QList<EnumOption> chargeTypeOptions();
// 站点营业状态（station_info.status）：服务端限定 OPEN/CLOSED/MAINTENANCE。
QList<EnumOption> stationStatusOptions();
// 运营可设置的桩状态（ADMIN_PILE_SET_STATUS 仅接受这三种）。
QList<EnumOption> pileStatusOptions();
// 用协议值反查中文显示值；未登记的值原样返回（服务端新增值不至显示为空）。
QString labelFor(const QList<EnumOption> &options, const QString &value);

// 把候选列表填进下拉框：显示中文、userData 存协议值，默认选中首个。
void fillCombo(QComboBox *combo, const QList<EnumOption> &options);

}  // namespace Ui
