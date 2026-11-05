#pragma once
#include <QFrame>
class QLabel;
class QToolButton;
namespace Ui { class StatusBadge; }
class MobilePageHeader final : public QFrame
{
    Q_OBJECT
public:
    explicit MobilePageHeader(QWidget *parent = nullptr);
    void setTitle(const QString &title);
    void setBackAvailable(bool available);
    void setNetworkIssue(bool issue);
signals:
    void backRequested();
private:
    QLabel *title_;
    QToolButton *back_;
    Ui::StatusBadge *networkStatus_;
};
