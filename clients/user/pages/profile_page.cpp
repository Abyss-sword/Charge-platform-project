#include "pages/profile_page.h"
#include "adapters/user_order_provider.h"
#include "adapters/user_account_provider.h"
#include "theme.h"
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QFileDialog>
#include <QFileInfo>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>

namespace {
QPixmap circularAvatar(const QPixmap &source)
{
    const int side = qMin(source.width(), source.height());
    if (side <= 0) return {};
    const QPixmap scaled = source.scaled(side, side, Qt::KeepAspectRatioByExpanding,
                                         Qt::SmoothTransformation);
    QPixmap result(80, 80);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addEllipse(0, 0, 80, 80);
    painter.setClipPath(path);
    painter.drawPixmap(0, 0, scaled.scaled(80, 80, Qt::IgnoreAspectRatio,
                                            Qt::SmoothTransformation));
    return result;
}
QPixmap defaultAvatar(const QString &name = {})
{
    QPixmap result(80, 80);
    result.fill(QColor(QStringLiteral("#aab2bd")));
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::white);
    painter.setFont(QFont(QStringLiteral("sans"), 26, QFont::Bold));
    painter.drawText(result.rect(), Qt::AlignCenter,
                     name.trimmed().isEmpty() ? QStringLiteral("用") : name.left(1));
    return result;
}
}
AccountPage::AccountPage(const PagePreview &preview, QWidget *parent) : PreviewPage(preview, parent)
{
    setObjectName("accountPage");
    appendCards(preview.cards);

    setAction(QStringLiteral("查看充电记录"));
    connect(primaryAction(), &QPushButton::clicked, this, [this] { emit navigationRequested(PrimaryPage::History); });
    auto *logout = new QPushButton(QStringLiteral("退出登录"), this);
    logout->setObjectName(QStringLiteral("logoutAction"));
    logout->setMinimumHeight(Ui::Theme::TouchHeight);
    addContent(logout);
    connect(logout, &QPushButton::clicked, this, &AccountPage::logoutRequested);
}

AccountPage::AccountPage(const PagePreview &preview, IUserOrderProvider *provider,
                         qint64 userId, QWidget *parent)
    : AccountPage(preview, provider, nullptr, userId, parent)
{
}

AccountPage::AccountPage(const PagePreview &preview, IUserOrderProvider *provider,
                         IUserAccountProvider *accountProvider, qint64 userId,
                         QWidget *parent)
    : PreviewPage({preview.title, QStringLiteral("账户充值"), {}, {},
                   Ui::PageState::Normal}, parent)
    , provider_(provider), accountProvider_(accountProvider), userId_(userId)
{
    setObjectName("accountPage");
    setNormalStateMessage(QStringLiteral("服务已连接，数据状态正常"));
    setSource(QStringLiteral("正式钱包服务 · 充值结果由服务端裁决"));
    avatar_ = new QLabel(this);
    avatar_->setObjectName(QStringLiteral("profileAvatar"));
    avatar_->setFixedSize(80, 80);
    avatar_->setPixmap(defaultAvatar());
    addContent(avatar_);
    nickname_ = new QLineEdit(this);
    nickname_->setObjectName(QStringLiteral("profileNickname"));
    nickname_->setReadOnly(true);
    addContent(nickname_);
    phone_ = new QLabel(QStringLiteral("手机号：加载中…"), this);
    phone_->setObjectName(QStringLiteral("profilePhone"));
    addContent(phone_);
    status_ = new QLabel(QStringLiteral("账户状态：加载中…"), this);
    status_->setObjectName(QStringLiteral("profileStatus"));
    addContent(status_);
    editNickname_ = new QPushButton(QStringLiteral("编辑昵称"), this);
    editNickname_->setObjectName(QStringLiteral("editNicknameAction"));
    addContent(editNickname_);
    chooseAvatar_ = new QPushButton(QStringLiteral("更换头像"), this);
    chooseAvatar_->setObjectName(QStringLiteral("chooseAvatarAction"));
    addContent(chooseAvatar_);
    saveProfile_ = new QPushButton(QStringLiteral("保存个人资料"), this);
    saveProfile_->setObjectName(QStringLiteral("saveProfileAction"));
    saveProfile_->setEnabled(false);
    addContent(saveProfile_);
    balance_ = new QLabel(QStringLiteral("余额加载中…"), this);
    balance_->setObjectName(QStringLiteral("walletBalance"));
    addContent(balance_);
    amount_=new QLineEdit(this);
    amount_->setObjectName(QStringLiteral("rechargeAmount"));
    amount_->setPlaceholderText(QStringLiteral("充值金额，例如 100.00"));
    amount_->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("^(0|[1-9]\\d*)\\.\\d{2}$")), amount_));
    addContent(amount_);
    recharge_ = new QPushButton(QStringLiteral("提交正式充值"), this);
    recharge_->setObjectName(QStringLiteral("rechargeAction"));
    recharge_->setMinimumHeight(Ui::Theme::TouchHeight);
    addContent(recharge_);
    connect(recharge_, &QPushButton::clicked, this, [this] {
        if (!provider_ || userId_ <= 0 || !amount_->hasAcceptableInput()) {
            setSource(QStringLiteral("请输入两位小数格式的有效充值金额。"));
            stateWidget()->setState(Ui::PageState::Error);
            return;
        }
        recharge_->setEnabled(false);
        provider_->recharge(userId_, amount_->text());
        setPageState(Ui::PageState::Loading);
    });
    connect(editNickname_, &QPushButton::clicked, this, [this] {
        nickname_->setReadOnly(false);
        nickname_->setFocus();
        saveProfile_->setEnabled(true);
    });
    connect(chooseAvatar_, &QPushButton::clicked, this, [this] {
        // S1-C：仅允许 PNG/JPEG/WebP，且不超过 5MB（审查 01 规格 · 头像约束）。
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择头像"), {},
                                                          QStringLiteral("Images (*.png *.jpg *.jpeg *.webp)"));
        if (path.isEmpty() || !QFileInfo::exists(path)) return;
        const QFileInfo info(path);
        if (info.size() > 5 * 1024 * 1024) {
            setSource(QStringLiteral("头像文件超过 5MB 限制，请压缩后重试。"));
            setPageState(Ui::PageState::Error);
            return;
        }
        const QPixmap pixmap(path);
        if (pixmap.isNull()) {
            setSource(QStringLiteral("头像文件无法读取（格式须为 PNG/JPEG/WebP）。"));
            setPageState(Ui::PageState::Error);
            return;
        }
        avatarPath_ = path;
        avatar_->setPixmap(circularAvatar(pixmap));
        avatar_->setToolTip(QStringLiteral("已本地预览；点击保存后由服务端确认。"));
        saveProfile_->setEnabled(true);
    });
    connect(saveProfile_, &QPushButton::clicked, this, [this] {
        if (!accountProvider_ || nickname_->text().trimmed().isEmpty()) {
            setSource(QStringLiteral("昵称不能为空。"));
            setPageState(Ui::PageState::Error);
            return;
        }
        saveProfile_->setEnabled(false);
        editNickname_->setEnabled(false);
        chooseAvatar_->setEnabled(false);
        setPageState(Ui::PageState::Loading);
        accountProvider_->updateProfile(userId_, nickname_->text(), avatarPath_);
    });
    if (accountProvider_) connect(accountProvider_, &IUserAccountProvider::profileUpdateFinished, this,
            [this](const UserProfileUpdateResult &result) {
        editNickname_->setEnabled(true);
        chooseAvatar_->setEnabled(true);
        if (result.kind != UserAccountResultKind::Success) {
            setSource(result.message);
            setPageState(result.kind == UserAccountResultKind::Offline
                             ? Ui::PageState::Offline : Ui::PageState::Error);
            saveProfile_->setEnabled(true);
            return;
        }
        nickname_->setReadOnly(true);
        saveProfile_->setEnabled(false);
        setSource(QStringLiteral("个人资料已保存（服务端确认）。"));
        accountProvider_->queryProfile(userId_);
    });
    if (accountProvider_) connect(accountProvider_, &IUserAccountProvider::profileFinished, this,
            [this](const UserProfileQueryResult &result) {
        if (result.kind != UserAccountResultKind::Success) return;
        currentNickname_ = result.profile.nickname;
        nickname_->setText(currentNickname_);
        phone_->setText(QStringLiteral("手机号：%1").arg(result.profile.phoneMasked));
        balance_->setText(QStringLiteral("当前余额：%1 元").arg(result.profile.balanceYuan));
        status_->setText(result.profile.status == QStringLiteral("FROZEN")
                         ? QStringLiteral("账户状态：账户受限")
                         : QStringLiteral("账户状态：账户正常"));
        // S1-C：头像以服务端回读为准——重登录/重启后从这里恢复；路径无效时保留当前头像。
        const QString serverAvatar = result.profile.avatarPath;
        if (!serverAvatar.isEmpty() && QFileInfo::exists(serverAvatar)) {
            const QPixmap serverPixmap(serverAvatar);
            if (!serverPixmap.isNull()) {
                avatar_->setPixmap(circularAvatar(serverPixmap));
            }
        }
    });
    connect(provider_, &IUserOrderProvider::operationFinished, this,
            [this](const UserOrderResult &result) {
        if (result.action != UserOrderAction::Recharge) return;
        setPageState(Ui::PageState::Normal);
        if (result.kind == UserOrderResultKind::Success) {
            setSource(QStringLiteral("服务端已确认充值成功。"));
            refreshBalance();
        } else {
            setSource(result.message);
            stateWidget()->setState(result.kind == UserOrderResultKind::Offline
                                        ? Ui::PageState::Offline
                                        : Ui::PageState::Error);
            recharge_->setEnabled(result.kind != UserOrderResultKind::Offline);
        }
    });
    if (accountProvider_) {
        connect(accountProvider_, &IUserAccountProvider::balanceFinished, this,
                [this](const WalletBalanceQueryResult &result) {
            if (result.kind != UserAccountResultKind::Success) {
                setSource(result.message);
                setPageState(result.kind == UserAccountResultKind::Offline
                                 ? Ui::PageState::Offline : Ui::PageState::Error);
                recharge_->setEnabled(result.kind != UserAccountResultKind::Offline);
                return;
            }
            balance_->setText(QStringLiteral("当前余额：%1 元").arg(result.balanceYuan));
            setSource(QStringLiteral("正式钱包服务 · 余额已更新"));
            setPageState(Ui::PageState::Normal);
            recharge_->setEnabled(true);
        });
        connect(accountProvider_, &IUserAccountProvider::connectivityLost, this, [this] {
            setSource(QStringLiteral("连接已断开，当前余额可能已过期。"));
            setPageState(Ui::PageState::Offline);
            recharge_->setEnabled(false);
        });
        connect(accountProvider_, &IUserAccountProvider::connectivityRestored, this, [this] {
            if (isVisible()) refreshBalance();
        });
    }
    connect(provider_, &IUserOrderProvider::connectivityLost, this, [this] {
        setSource(QStringLiteral("连接已断开，当前账户数据可能已过期。"));
        setPageState(Ui::PageState::Offline);
        recharge_->setEnabled(false);
    });
    connect(provider_, &IUserOrderProvider::connectivityRestored, this, [this] {
        setSource(QStringLiteral("正式钱包服务 · 连接已恢复"));
        setPageState(Ui::PageState::Normal);
        recharge_->setEnabled(true);
    });
    setAction(QStringLiteral("查看充电记录"));
    connect(primaryAction(), &QPushButton::clicked, this,
            [this] { emit navigationRequested(PrimaryPage::History); });
    auto *logout = new QPushButton(QStringLiteral("退出登录"), this);
    logout->setObjectName(QStringLiteral("logoutAction"));
    logout->setMinimumHeight(Ui::Theme::TouchHeight);
    addContent(logout);
    connect(logout, &QPushButton::clicked, this, &AccountPage::logoutRequested);
    enablePolling();  // S3：可见页每 3 秒刷新（资料 + 余额）
}

// S3 轮询入口：同时刷新资料与余额，均按状态防闪烁。
void AccountPage::refresh()
{
    if (!accountProvider_ || userId_ <= 0) return;
    markRefreshing();
    accountProvider_->queryProfile(userId_);
    accountProvider_->queryBalance(userId_);
}

void AccountPage::refreshBalance()
{
    if (!accountProvider_ || userId_ <= 0) return;
    setPageState(Ui::PageState::Loading);
    accountProvider_->queryBalance(userId_);
}

void AccountPage::refreshProfile()
{
    if (accountProvider_ && userId_ > 0) accountProvider_->queryProfile(userId_);
}
