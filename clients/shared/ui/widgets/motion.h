#pragma once
#include "theme.h"
#include <QAbstractAnimation>
#include <QPropertyAnimation>
namespace Ui {
class Motion final {
public:
    static bool enabled() { return enabled_; }
    static void setEnabled(bool enabled) { enabled_ = enabled; }
    static QAbstractAnimation *createGeometry(QObject *target, const QRect &end, int duration = Theme::MotionNormalMs) {
        if (!enabled_) return nullptr;
        auto *animation = new QPropertyAnimation(target, "geometry", target); animation->setDuration(duration); animation->setEndValue(end); return animation;
    }
private: inline static bool enabled_ = true;
};
}
