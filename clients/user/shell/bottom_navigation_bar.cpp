#include <QVariant>
#include "shell/bottom_navigation_bar.h"
#include "shell/mobile_metrics.h"
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QStyle>
#include <QToolButton>

BottomNavigationBar::BottomNavigationBar(QWidget *parent)
    : QFrame(parent), buttons_(new QButtonGroup(this))
{
    setObjectName("bottomNavigationBar");
    setAccessibleName(QStringLiteral("主导航"));
    setMinimumHeight(MobileMetrics::NavigationMinimumHeight);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    const QStringList names{QStringLiteral("首页"), QStringLiteral("找桩"),
                            QStringLiteral("订单"), QStringLiteral("记录"), QStringLiteral("我的")};
    // TODO: replace these Qt standard icons with the team's consistent SVG set.
    const QStyle::StandardPixmap icons[]{QStyle::SP_DirHomeIcon, QStyle::SP_DialogOpenButton,
        QStyle::SP_FileDialogContentsView, QStyle::SP_FileDialogDetailedView, QStyle::SP_FileIcon};
    for (int i = 0; i < PrimaryPageCount; ++i) {
        auto *button = new QToolButton(this);
        button->setObjectName(QString("nav%1").arg(i));
        button->setProperty("role", "navigation");
        button->setText(names[i]);
        button->setAccessibleName(names[i]);
        button->setIcon(style()->standardIcon(icons[i]));
        button->setIconSize(QSize(MobileMetrics::IconSize, MobileMetrics::IconSize));
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setCheckable(true);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        buttons_->addButton(button, i);
        layout->addWidget(button, 1);
    }
    buttons_->button(0)->setChecked(true);
    connect(buttons_, &QButtonGroup::idClicked, this, [this](int index) {
        setCurrentPage(static_cast<PrimaryPage>(index));
    });
}
void BottomNavigationBar::setCurrentPage(PrimaryPage page)
{
    const int index = static_cast<int>(page);
    if (index < 0 || index >= PrimaryPageCount) return;
    buttons_->button(index)->setChecked(true);
    if (current_ == page) return;
    current_ = page;
    emit currentPageChanged(page);
}
