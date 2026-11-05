#include "pages/preview_page.h"
#include <QComboBox>
#include <QTimer>

PreviewPage::PreviewPage(const PagePreview &preview, QWidget *parent)
    : Ui::PageScaffold(preview.title, preview.subtitle, parent)
{
    setSource(preview.source);
    auto *selector = new QComboBox(this);
    selector->setObjectName("mockStateSelector");
    selector->setAccessibleName(QStringLiteral("模拟状态预览"));
    for (auto state : {Ui::PageState::Loading, Ui::PageState::Normal, Ui::PageState::Empty,
                       Ui::PageState::Error, Ui::PageState::Offline, Ui::PageState::Reconnecting,
                       Ui::PageState::Degraded}) {
        selector->addItem(QStringLiteral("模拟状态 · ") + Ui::pageStateName(state), static_cast<int>(state));
    }
    selector->setCurrentIndex(selector->findData(static_cast<int>(preview.state)));
    addStateControl(selector);
    connect(selector, &QComboBox::currentIndexChanged, this, [this, selector](int index) {
        setPageState(static_cast<Ui::PageState>(selector->itemData(index).toInt()));
    });
    setPageState(preview.state);
    // S3：定时器延迟创建；只有 enablePolling() 的正式页面才真正轮询。
    pollTimer_ = new QTimer(this);
    pollTimer_->setObjectName(QStringLiteral("pagePollTimer"));
    pollTimer_->setInterval(3000);
    connect(pollTimer_, &QTimer::timeout, this, [this] { if (isVisible()) refresh(); });
}

void PreviewPage::enablePolling()
{
    pollingEnabled_ = true;
    if (isVisible()) pollTimer_->start();  // 已可见则立即开始；否则由 showEvent 启动。
}

void PreviewPage::disablePolling()
{
    pollTimer_->stop();
}

void PreviewPage::markRefreshing()
{
    if (stateWidget()->state() != Ui::PageState::Normal) setPageState(Ui::PageState::Loading);
}

void PreviewPage::showEvent(QShowEvent *event)
{
    Ui::PageScaffold::showEvent(event);
    if (pollingEnabled_ && pollTimer_ && !pollTimer_->isActive()) pollTimer_->start();
}

void PreviewPage::hideEvent(QHideEvent *event)
{
    Ui::PageScaffold::hideEvent(event);
    if (pollTimer_) pollTimer_->stop();  // 离开页面停止定时刷新（S3）。
}

void PreviewPage::appendCards(const QList<PreviewCard> &cards)
{
    for (const auto &card : cards) addCard(card.title, card.body, card.hero);
}
