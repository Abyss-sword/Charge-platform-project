#pragma once
#include <QFrame>
class QLabel; class QTimer; class QToolButton;
namespace Ui {
enum class ToastType { Success, Info, Warning, Error };
class Toast final : public QFrame {
    Q_OBJECT
public:
    explicit Toast(ToastType type, const QString &message, int timeoutMs = 2500, QWidget *parent = nullptr);
    void showToast();
    QToolButton *closeButton() const { return close_; }
private:
    QToolButton *close_{};
    QTimer *timer_{};
};
}
