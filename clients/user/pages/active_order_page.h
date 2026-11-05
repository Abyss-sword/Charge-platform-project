#pragma once
#include "pages/preview_page.h"
#include "models/user_order.h"
#include <QPointer>
class IUserOrderProvider;
class QLabel;
class QPushButton;
class ActiveOrderPage final : public PreviewPage
{
    Q_OBJECT
public:
    explicit ActiveOrderPage(const PagePreview &preview, QWidget *parent = nullptr);
    ActiveOrderPage(const PagePreview &preview, IUserOrderProvider *provider,
                    qint64 userId, QWidget *parent = nullptr);
    void refresh() override;
signals:
    void continueOrderRequested(const UserOrderSnapshot &order);
    void activeOrderLoaded(const UserOrderQueryResult &result);
private:
    QPointer<IUserOrderProvider> provider_;
    qint64 userId_ = 0;
    QLabel *summary_ = nullptr;
    QPushButton *continue_ = nullptr;
    UserOrderSnapshot order_;
};
