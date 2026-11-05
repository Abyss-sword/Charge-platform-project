#pragma once
#include "pages/preview_page.h"
#include <QPointer>
class IUserOrderProvider;
class QTableWidget;
class ChargingHistoryPage final : public PreviewPage
{
    Q_OBJECT
public:
    explicit ChargingHistoryPage(const PagePreview &preview, QWidget *parent = nullptr);
    ChargingHistoryPage(const PagePreview &preview, IUserOrderProvider *provider,
                        qint64 userId, QWidget *parent = nullptr);
    void refresh() override;
private:
    QPointer<IUserOrderProvider> provider_;
    qint64 userId_ = 0;
    QTableWidget *rows_ = nullptr;
};
