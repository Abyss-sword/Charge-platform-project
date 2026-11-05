#pragma once
#include "pages/preview_page.h"
#include <QPointer>
class IUserOrderProvider;
class IUserAccountProvider;
class QLabel;
class QLineEdit;
class QPushButton;
class AccountPage final : public PreviewPage
{
    Q_OBJECT
public:
    explicit AccountPage(const PagePreview &preview, QWidget *parent = nullptr);
    AccountPage(const PagePreview &preview, IUserOrderProvider *provider,
                qint64 userId, QWidget *parent = nullptr);
    AccountPage(const PagePreview &preview, IUserOrderProvider *orderProvider,
                IUserAccountProvider *accountProvider, qint64 userId,
                QWidget *parent = nullptr);
    void refreshBalance();
    void refreshProfile();
    void refresh() override;  // S3：可见页 3 秒轮询入口
signals:
    void logoutRequested();
private:
    QPointer<IUserOrderProvider> provider_;
    QPointer<IUserAccountProvider> accountProvider_;
    QLineEdit *amount_ = nullptr;
    QPushButton *recharge_ = nullptr;
    QLabel *balance_ = nullptr;
    QLabel *avatar_ = nullptr;
    QLabel *phone_ = nullptr;
    QLabel *status_ = nullptr;
    QLineEdit *nickname_ = nullptr;
    QPushButton *editNickname_ = nullptr;
    QPushButton *chooseAvatar_ = nullptr;
    QPushButton *saveProfile_ = nullptr;
    QString avatarPath_;
    QString currentNickname_;
    qint64 userId_ = 0;
};
