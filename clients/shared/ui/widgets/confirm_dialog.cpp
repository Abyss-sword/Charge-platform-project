#include "widgets/confirm_dialog.h"
#include "theme.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
namespace Ui {
ConfirmDialog::ConfirmDialog(const QString &title, const QString &message, ConfirmMode mode, QWidget *parent) : QDialog(parent) {
    setObjectName(QStringLiteral("confirmDialog")); setWindowTitle(title);
    auto *layout = new QVBoxLayout(this); layout->addWidget(new QLabel(title, this));
    auto *body = new QLabel(message, this); body->setWordWrap(true); body->setObjectName(QStringLiteral("dialogMessage")); layout->addWidget(body);
    if (mode == ConfirmMode::Danger) {
        setProperty("mode", "danger"); body->setText(QStringLiteral("危险操作\n操作对象与影响：%1\n请确认后继续。\n%2").arg(title, message));
    }
    auto *box = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
    confirm_ = box->button(QDialogButtonBox::Ok); confirm_->setObjectName(QStringLiteral("confirmButton")); confirm_->setProperty("role", mode == ConfirmMode::Danger ? "danger" : "primary");
    box->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("cancelButton")); layout->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, [this]{ emit confirmed(); accept(); });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
}
