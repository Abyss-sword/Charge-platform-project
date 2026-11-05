#include "widgets/toast.h"
#include <QLabel>
#include <QTimer>
#include <QToolButton>
#include <QHBoxLayout>
namespace Ui {
Toast::Toast(ToastType type, const QString &message, int timeoutMs, QWidget *parent) : QFrame(parent), timer_(new QTimer(this)) {
    setObjectName(QStringLiteral("toast")); setProperty("toastType", static_cast<int>(type));
    auto *layout = new QHBoxLayout(this); auto *label = new QLabel(this); label->setObjectName(QStringLiteral("toastMessage")); label->setWordWrap(true);
    const QString icon = type == ToastType::Success ? QStringLiteral("✓") : type == ToastType::Info ? QStringLiteral("ⓘ") : type == ToastType::Warning ? QStringLiteral("⚠") : QStringLiteral("×");
    label->setText(icon + QStringLiteral(" ") + message); label->setAccessibleName(label->text()); layout->addWidget(label, 1);
    close_ = new QToolButton(this); close_->setText(QStringLiteral("关闭")); close_->setAccessibleName(QStringLiteral("关闭提示")); layout->addWidget(close_);
    timer_->setSingleShot(true); timer_->setInterval(timeoutMs); connect(close_, &QToolButton::clicked, this, &Toast::close); connect(timer_, &QTimer::timeout, this, &Toast::close);
}
void Toast::showToast() { show(); timer_->start(); }
}
