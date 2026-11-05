#pragma once
#include <QFrame>
#include "shell/primary_page.h"
class QButtonGroup;
class BottomNavigationBar final : public QFrame
{
    Q_OBJECT
public:
    explicit BottomNavigationBar(QWidget *parent = nullptr);
    void setCurrentPage(PrimaryPage page);
    PrimaryPage currentPage() const { return current_; }
signals:
    void currentPageChanged(PrimaryPage page);
private:
    QButtonGroup *buttons_;
    PrimaryPage current_ = PrimaryPage::Home;
};
