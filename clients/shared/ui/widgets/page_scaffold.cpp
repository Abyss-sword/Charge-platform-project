#include <QVariant>
#include "widgets/page_scaffold.h"
#include "theme.h"
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
namespace Ui {
PageScaffold::PageScaffold(const QString &title, const QString &subtitle, QWidget *parent)
    : QWidget(parent), outerLayout_(new QVBoxLayout(this)), content_(new QWidget(this)),
      source_(new QLabel(this)), state_(new PageStateWidget(this)), action_(nullptr)
{
    outerLayout_->setContentsMargins(Theme::Margin, Theme::Space, Theme::Margin, Theme::Space);
    outerLayout_->setSpacing(Theme::Space);
    source_->setProperty("role", "caption");
    source_->setWordWrap(true);
    outerLayout_->addWidget(source_);
    outerLayout_->addWidget(state_);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName("pageScroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    content_->setObjectName("scrollContent");
    contentLayout_ = new QVBoxLayout(content_);
    contentLayout_->setContentsMargins(0, 0, Theme::Space, 0);
    contentLayout_->setSpacing(Theme::Space);
    contentLayout_->setAlignment(Qt::AlignTop);
    auto *heading = new QLabel(title, content_);
    heading->setProperty("role", "section");
    heading->setWordWrap(true);
    contentLayout_->addWidget(heading);
    auto *description = new QLabel(subtitle, content_);
    description->setWordWrap(true);
    description->setProperty("role", "caption");
    contentLayout_->addWidget(description);
    scroll->setWidget(content_);
    outerLayout_->addWidget(scroll, 1);

}
QFrame *PageScaffold::addCard(const QString &title, const QString &body, bool hero)
{
    auto *card = createCard(title, body, hero, content_);
    addContent(card);
    return card;
}
QFrame *PageScaffold::createCard(const QString &title, const QString &body, bool hero, QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setProperty("role", hero ? "hero" : "card");
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(Theme::Margin, Theme::Margin, Theme::Margin, Theme::Margin);
    auto *heading = new QLabel(title, card);
    heading->setWordWrap(true);
    heading->setProperty("role", "section");
    auto *text = new QLabel(body, card);
    text->setWordWrap(true);
    layout->addWidget(heading);
    layout->addWidget(text);
    return card;
}
void PageScaffold::addContent(QWidget *widget) { contentLayout_->addWidget(widget); }
void PageScaffold::setSource(const QString &text) { source_->setText(text); }
void PageScaffold::addStateControl(QWidget *control) { outerLayout_->insertWidget(2, control); }
void PageScaffold::setAction(const QString &text, bool available)
{
    if (!action_) {
        action_ = new QPushButton(this);
        action_->setObjectName("primaryAction");
        action_->setProperty("role", "primary");
        action_->setMinimumHeight(Theme::TouchHeight);
        outerLayout_->addWidget(action_);
    }
    actionAvailable_ = available;
    action_->setText(text);
    action_->setVisible(!text.isEmpty());
    setPageState(state_->state());
}
void PageScaffold::setPageState(PageState state)
{
    state_->setState(state);
    const bool hasContent = state == PageState::Normal || state == PageState::Offline
        || state == PageState::Reconnecting || state == PageState::Degraded
        || state == PageState::Stale;
    content_->setVisible(hasContent);
    if (action_) action_->setEnabled(actionAvailable_ && state == PageState::Normal);
}
void PageScaffold::setNormalStateMessage(const QString &message)
{
    state_->setNormalMessage(message);
}
}
