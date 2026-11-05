#pragma once
#include "pages/preview_page.h"
#include <QPointer>
class IUserAccountProvider;
class QLabel;
class HomePage final : public PreviewPage
{
    Q_OBJECT
public:
    explicit HomePage(const PagePreview &preview, QWidget *parent = nullptr);
    HomePage(const PagePreview &preview, IUserAccountProvider *provider,
             qint64 userId, QWidget *parent = nullptr);
    void refresh() override;
private:
    QPointer<IUserAccountProvider> provider_;
    qint64 userId_ = 0;
    QLabel *profile_ = nullptr;
};
