#pragma once
#include <QLabel>
namespace Ui {
enum class BadgeState { Connected, Disconnected, Charging, Available, Error, Simulated };
class StatusBadge final : public QLabel
{
    Q_OBJECT
public:
    explicit StatusBadge(BadgeState state, QWidget *parent = nullptr);
    void setState(BadgeState state);
};
}
