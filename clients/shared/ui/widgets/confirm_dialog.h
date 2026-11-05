#pragma once
#include <QDialog>
class QLabel; class QPushButton;
namespace Ui {
enum class ConfirmMode { Normal, Danger };
class ConfirmDialog final : public QDialog {
    Q_OBJECT
public:
    ConfirmDialog(const QString &title, const QString &message, ConfirmMode mode = ConfirmMode::Normal, QWidget *parent = nullptr);
    QPushButton *confirmButton() const { return confirm_; }
signals:
    void confirmed();
private:
    QPushButton *confirm_{};
};
}
