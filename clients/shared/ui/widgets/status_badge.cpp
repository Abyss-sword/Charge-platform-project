#include <QVariant>
#include "widgets/status_badge.h"
#include <QStyle>
namespace Ui {
StatusBadge::StatusBadge(BadgeState state, QWidget *parent) : QLabel(parent)
{
    setProperty("badge", true);
    setWordWrap(true);
    setState(state);
}
void StatusBadge::setState(BadgeState state)
{
    QString text, tone;
    switch (state) {
    case BadgeState::Connected: text = QStringLiteral("● 已连接"); tone = "success"; break;
    case BadgeState::Disconnected: text = QStringLiteral("○ 未连接"); tone = "warning"; break;
    case BadgeState::Charging: text = QStringLiteral("● 充电中"); tone = "success"; break;
    case BadgeState::Available: text = QStringLiteral("✓ 可用"); tone = "success"; break;
    case BadgeState::Error: text = QStringLiteral("× 异常"); tone = "error"; break;
    case BadgeState::Simulated: text = QStringLiteral("◇ SIMULATED"); tone = "info"; break;
    }
    const QString plainText = text;
    setText(QStringLiteral("<img src=\":/ui/icons/status.svg\" width=\"14\" height=\"14\"> ") + text);
    setProperty("iconSource", ":/ui/icons/status.svg");
    setAccessibleName(plainText);
    setProperty("tone", tone);
    style()->unpolish(this);
    style()->polish(this);
}
}
