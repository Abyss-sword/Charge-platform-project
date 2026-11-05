#include <QVariant>
#include "shell/mobile_page_header.h"
#include "shell/mobile_metrics.h"
#include "theme.h"
#include "widgets/status_badge.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QStyle>

MobilePageHeader::MobilePageHeader(QWidget *parent)
    : QFrame(parent), title_(new QLabel(this)), back_(new QToolButton(this)),
      networkStatus_(new Ui::StatusBadge(Ui::BadgeState::Error, this))
{
    setObjectName("mobilePageHeader");
    setMinimumHeight(MobileMetrics::HeaderMinimumHeight);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(Ui::Theme::Margin, Ui::Theme::Space, Ui::Theme::Margin, Ui::Theme::Space);
    back_->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    back_->setAccessibleName(QStringLiteral("返回"));
    back_->setMinimumSize(Ui::Theme::TouchHeight, Ui::Theme::TouchHeight);
    back_->hide();
    title_->setObjectName("headerTitle");
    title_->setProperty("role", "title");
    title_->setWordWrap(true);
    layout->addWidget(back_);
    layout->addWidget(title_, 1);
    networkStatus_->setToolTip(QStringLiteral("网络异常，页面数据可能已过期"));
    networkStatus_->setAccessibleName(QStringLiteral("网络异常"));
    networkStatus_->hide();
    layout->addWidget(networkStatus_);
    connect(back_, &QToolButton::clicked, this, &MobilePageHeader::backRequested);
}
void MobilePageHeader::setTitle(const QString &title) { title_->setText(title); }
void MobilePageHeader::setBackAvailable(bool available) { back_->setVisible(available); }
void MobilePageHeader::setNetworkIssue(bool issue)
{
    if (issue) {
        networkStatus_->setState(Ui::BadgeState::Error);
        networkStatus_->setText(QStringLiteral("× 网络异常"));
        networkStatus_->setAccessibleName(QStringLiteral("网络异常"));
    }
    networkStatus_->setVisible(issue);
}
