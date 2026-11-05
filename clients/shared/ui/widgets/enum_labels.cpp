#include "widgets/enum_labels.h"

#include <QComboBox>

namespace Ui {

QList<EnumOption> chargeTypeOptions()
{
    return {{QStringLiteral("快充"), QStringLiteral("FAST")},
            {QStringLiteral("慢充"), QStringLiteral("SLOW")}};
}

QList<EnumOption> stationStatusOptions()
{
    return {{QStringLiteral("营业中"), QStringLiteral("OPEN")},
            {QStringLiteral("维护中"), QStringLiteral("MAINTENANCE")},
            {QStringLiteral("已关闭"), QStringLiteral("CLOSED")}};
}

QList<EnumOption> pileStatusOptions()
{
    // ADMIN_PILE_SET_STATUS 服务端仅接受这三种；预约/充电/恢复由业务自动推导。
    return {{QStringLiteral("空闲"), QStringLiteral("IDLE")},
            {QStringLiteral("故障"), QStringLiteral("FAULT")},
            {QStringLiteral("离线"), QStringLiteral("OFFLINE")}};
}

QString labelFor(const QList<EnumOption> &options, const QString &value)
{
    for (const auto &option : options) {
        if (option.value == value) {
            return option.label;
        }
    }
    return value;
}

void fillCombo(QComboBox *combo, const QList<EnumOption> &options)
{
    if (!combo) {
        return;
    }
    combo->clear();
    for (const auto &option : options) {
        combo->addItem(option.label, option.value);  // 显示中文，userData 存协议值。
    }
}

}  // namespace Ui
