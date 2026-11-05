#include "pages/user_login_page.h"

#include "shell/mobile_metrics.h"
#include "theme.h"
#include "widgets/page_state_widget.h"
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

UserLoginPage::UserLoginPage(IUserLoginProvider *provider, QWidget *parent)
    : QWidget(parent), provider_(provider), phone_(new QLineEdit(this)),
      submit_(new QPushButton(QStringLiteral("登录"), this)),
      retry_(new QPushButton(QStringLiteral("重试连接"), this)),
      feedback_(new QLabel(this)), modeBanner_(new QLabel(this)),
      status_(new Ui::PageStateWidget(this))
{
    Q_ASSERT(provider_);
    setObjectName(QStringLiteral("userLoginPage"));
    setWindowTitle(QStringLiteral("EV FLOW · 车主登录"));
    setMinimumSize(MobileMetrics::Minimum);
    resize(MobileMetrics::Baseline);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(Ui::Theme::Margin, Ui::Theme::Margin,
                              Ui::Theme::Margin, Ui::Theme::Margin);
    outer->setSpacing(Ui::Theme::Space);

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("userLoginScroll"));
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("scrollContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, Ui::Theme::Space, 0);
    contentLayout->setSpacing(Ui::Theme::Space);
    contentLayout->setAlignment(Qt::AlignTop);

    auto *brand = new QLabel(QStringLiteral("EV FLOW"), content);
    brand->setProperty("role", "title");
    auto *intro = new QLabel(QStringLiteral("新能源汽车充电服务客户端"), content);
    intro->setProperty("role", "caption");
    intro->setWordWrap(true);
    modeBanner_->setObjectName(QStringLiteral("offlineDemoBanner"));
    modeBanner_->setProperty("role", "caption");
    modeBanner_->setWordWrap(true);
    modeBanner_->hide();
    contentLayout->addWidget(brand);
    contentLayout->addWidget(intro);
    contentLayout->addWidget(modeBanner_);

    auto *hero = new QFrame(content);
    hero->setProperty("role", "hero");
    auto *heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(Ui::Theme::Margin, Ui::Theme::Margin,
                                   Ui::Theme::Margin, Ui::Theme::Margin);
    auto *heroTitle = new QLabel(QStringLiteral("从容找到附近可用充电站"), hero);
    heroTitle->setProperty("role", "section");
    heroTitle->setWordWrap(true);
    auto *heroText = new QLabel(QStringLiteral("登录车主端账户，便捷使用充电服务。"), hero);
    heroText->setWordWrap(true);
    heroLayout->addWidget(heroTitle);
    heroLayout->addWidget(heroText);
    contentLayout->addWidget(hero);

    auto *form = new QFrame(content);
    form->setProperty("role", "card");
    auto *formLayout = new QVBoxLayout(form);
    formLayout->setContentsMargins(Ui::Theme::Margin, Ui::Theme::Margin,
                                   Ui::Theme::Margin, Ui::Theme::Margin);
    formLayout->setSpacing(Ui::Theme::Space);
    auto *title = new QLabel(QStringLiteral("车主登录"), form);
    title->setProperty("role", "section");
    auto *label = new QLabel(QStringLiteral("手机号"), form);
    phone_->setObjectName(QStringLiteral("userLoginPhoneEdit"));
    phone_->setPlaceholderText(QStringLiteral("请输入11位中国大陆手机号"));
    phone_->setInputMethodHints(Qt::ImhDigitsOnly);
    // Keep one extra digit visible so an overlong phone is rejected instead of
    // being silently truncated into a different, apparently valid account.
    phone_->setMaxLength(12);
    phone_->setAccessibleName(QStringLiteral("车主手机号"));
    feedback_->setObjectName(QStringLiteral("userLoginFeedback"));
    feedback_->setProperty("role", "caption");
    feedback_->setWordWrap(true);
    feedback_->setText(QStringLiteral("请输入手机号登录车主端账户。"));
    auto *privacy = new QLabel(QStringLiteral("登录时手机号将提交给服务端进行身份验证。"), form);
    privacy->setProperty("role", "caption");
    privacy->setWordWrap(true);
    formLayout->addWidget(title);
    formLayout->addWidget(label);
    formLayout->addWidget(phone_);
    formLayout->addWidget(feedback_);
    formLayout->addWidget(privacy);
    contentLayout->addWidget(form);
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);

    status_->setObjectName(QStringLiteral("userLoginConnectionStatus"));
    retry_->setObjectName(QStringLiteral("userLoginRetryButton"));
    retry_->setAccessibleName(QStringLiteral("重试登录连接"));
    retry_->setMinimumHeight(Ui::Theme::TouchHeight);
    retry_->hide();
    submit_->setObjectName(QStringLiteral("userLoginSubmitButton"));
    submit_->setProperty("role", "primary");
    submit_->setMinimumHeight(Ui::Theme::TouchHeight);
    outer->addWidget(status_);
    outer->addWidget(retry_);
    outer->addWidget(submit_);

    connect(phone_, &QLineEdit::textEdited, this, [this](const QString &text) {
        QString digits;
        for (const QChar character : text) {
            if (character.isDigit()) digits.append(character);
        }
        if (digits != text) phone_->setText(digits.left(12));
        if (phone_->property("invalid").toBool()) setValidationError({});
    });
    connect(submit_, &QPushButton::clicked, this, &UserLoginPage::submit);
    connect(retry_, &QPushButton::clicked, this, &UserLoginPage::submit);
    connect(provider_, &IUserLoginProvider::loginFinished,
            this, &UserLoginPage::handleResult);
}

void UserLoginPage::setOfflineDemoMode(bool enabled)
{
    modeBanner_->setText(enabled
        ? QStringLiteral("OFFLINE_DEMO · 演示模式，不连接正式服务") : QString{});
    modeBanner_->setVisible(enabled);
    setWindowTitle(enabled ? QStringLiteral("EV FLOW · 车主登录 · OFFLINE_DEMO")
                           : QStringLiteral("EV FLOW · 车主登录"));
}

bool UserLoginPage::validatePhone()
{
    if (phone_->text().isEmpty()) {
        setValidationError(QStringLiteral("请输入手机号。"));
        return false;
    }
    static const QRegularExpression mainlandPhone(QStringLiteral("^1[3-9]\\d{9}$"));
    if (!mainlandPhone.match(phone_->text()).hasMatch()) {
        setValidationError(QStringLiteral("请输入以1开头的11位中国大陆手机号。"));
        return false;
    }
    setValidationError({});
    return true;
}

void UserLoginPage::setValidationError(const QString &message)
{
    phone_->setProperty("invalid", !message.isEmpty());
    phone_->style()->unpolish(phone_);
    phone_->style()->polish(phone_);
    feedback_->setText(message.isEmpty()
        ? QStringLiteral("请输入手机号登录车主端账户。") : QStringLiteral("× ") + message);
}

void UserLoginPage::setLoading(bool loading)
{
    phone_->setEnabled(!loading);
    submit_->setEnabled(!loading);
    retry_->setEnabled(!loading);
    submit_->setText(loading ? QStringLiteral("正在登录…") : QStringLiteral("登录"));
    if (loading) {
        retry_->hide();
        status_->setState(Ui::PageState::Loading);
    }
}

void UserLoginPage::submit()
{
    if (!validatePhone()) return;
    setLoading(true);
    provider_->submit(LoginRequest{phone_->text()});
}

void UserLoginPage::handleResult(const LoginResult &result)
{
    setLoading(false);
    feedback_->setText(result.message);
    if (result.kind == LoginResultKind::Success) {
        status_->setState(Ui::PageState::Normal);
        emit loginSucceeded(result);
        return;
    }
    if (result.kind == LoginResultKind::Offline) {
        status_->setState(Ui::PageState::Offline);
        retry_->show();
        return;
    }
    status_->setState(Ui::PageState::Error);
}

void UserLoginPage::resetForNewSession()
{
    phone_->clear();
    setValidationError({});
    setLoading(false);
    retry_->hide();
    status_->setState(Ui::PageState::Normal);
}
