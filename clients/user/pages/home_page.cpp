#include "pages/home_page.h"
#include "adapters/user_account_provider.h"
#include "theme.h"
#include <QLabel>
#include <QPushButton>
HomePage::HomePage(const PagePreview &preview, QWidget *parent) : PreviewPage(preview, parent)
{
    setObjectName("homePage");
    auto *shortcuts = new QLabel(QStringLiteral("快捷入口"), this);
    shortcuts->setProperty("role", "section");
    addContent(shortcuts);
    appendCards(preview.cards);

    auto *order = new QPushButton(QStringLiteral("继续处理 · 活动订单"), this);
    order->setMinimumHeight(Ui::Theme::TouchHeight);
    addContent(order);
    connect(order, &QPushButton::clicked, this, [this] { emit navigationRequested(PrimaryPage::Orders); });
    auto *account = new QPushButton(QStringLiteral("我的账户"), this);
    account->setMinimumHeight(Ui::Theme::TouchHeight);
    addContent(account);
    connect(account, &QPushButton::clicked, this, [this] { emit navigationRequested(PrimaryPage::Account); });
    setAction(QStringLiteral("查找附近充电站"));
    connect(primaryAction(), &QPushButton::clicked, this, [this] { emit navigationRequested(PrimaryPage::Stations); });
    primaryAction()->setEnabled(true);
}

HomePage::HomePage(const PagePreview &preview, IUserAccountProvider *provider,
                   qint64 userId, QWidget *parent)
    : PreviewPage({preview.title, QStringLiteral("车主账户概览"), {}, {},
                   Ui::PageState::Normal}, parent)
    , provider_(provider), userId_(userId)
{
    setObjectName("homePage");
    enablePolling();  // S3：可见页每 3 秒刷新
    setNormalStateMessage(QStringLiteral("服务已连接，数据状态正常"));
    setSource(QStringLiteral("正式用户服务 · USER_PROFILE"));
    profile_ = new QLabel(this);
    profile_->setWordWrap(true);
    addContent(profile_);
    auto *shortcuts = new QLabel(QStringLiteral("快捷入口"), this);
    shortcuts->setProperty("role", "section");
    addContent(shortcuts);
    auto *order = new QPushButton(QStringLiteral("继续处理 · 活动订单"), this);
    order->setMinimumHeight(Ui::Theme::TouchHeight);
    addContent(order);
    connect(order, &QPushButton::clicked, this,
            [this] { emit navigationRequested(PrimaryPage::Orders); });
    auto *account = new QPushButton(QStringLiteral("我的账户"), this);
    account->setMinimumHeight(Ui::Theme::TouchHeight);
    addContent(account);
    connect(account, &QPushButton::clicked, this,
            [this] { emit navigationRequested(PrimaryPage::Account); });
    setAction(QStringLiteral("查找附近充电站"));
    connect(primaryAction(), &QPushButton::clicked, this,
            [this] { emit navigationRequested(PrimaryPage::Stations); });
    connect(provider_, &IUserAccountProvider::profileFinished, this,
            [this](const UserProfileQueryResult &result) {
        if (result.kind != UserAccountResultKind::Success) {
            setSource(result.message);
            setPageState(result.kind == UserAccountResultKind::Offline
                             ? Ui::PageState::Offline : Ui::PageState::Error);
            return;
        }
        profile_->setText(QStringLiteral("欢迎回来，%1\n手机号：%2\n当前余额：%3 元\n账户状态：%4")
                              .arg(result.profile.nickname, result.profile.phoneMasked,
                                   result.profile.balanceYuan, result.profile.status));
        setSource(QStringLiteral("正式用户服务 · 数据已更新"));
        setPageState(Ui::PageState::Normal);
    });
    connect(provider_, &IUserAccountProvider::connectivityLost, this, [this] {
        setSource(QStringLiteral("连接已断开，当前用户资料可能已过期。"));
        setPageState(Ui::PageState::Offline);
    });
    connect(provider_, &IUserAccountProvider::connectivityRestored, this, [this] {
        if (isVisible()) refresh();
    });
}

void HomePage::refresh()
{
    if (!provider_ || userId_ <= 0) return;
    markRefreshing();
    provider_->queryProfile(userId_);
}
