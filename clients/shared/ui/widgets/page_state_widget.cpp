#include "widgets/page_state_widget.h"
#include "theme.h"
#include <QLabel>
#include <QVBoxLayout>
namespace Ui {
QString pageStateName(PageState state)
{
    switch (state) {
    case PageState::Loading: return QStringLiteral("加载中");
    case PageState::Normal: return QStringLiteral("正常");
    case PageState::Empty: return QStringLiteral("无数据");
    case PageState::Error: return QStringLiteral("加载失败");
    case PageState::Offline: return QStringLiteral("离线");
    case PageState::Reconnecting: return QStringLiteral("重连中");
    case PageState::Degraded: return QStringLiteral("降级");
    case PageState::Stale: return QStringLiteral("陈旧数据");
    }
    return {};
}
PageStateWidget::PageStateWidget(QWidget *parent) : QFrame(parent), message_(new QLabel(this))
{
    setObjectName("pageState");
    message_->setObjectName("stateMessage");
    message_->setWordWrap(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(message_);
    setState(PageState::Normal);
}
void PageStateWidget::setState(PageState state)
{
    state_ = state;
    QString message;
    switch (state) {
    case PageState::Loading: message = QStringLiteral("◌ 正在加载，可切换其他页面"); break;
    case PageState::Normal: message = normalMessage_.isEmpty()
        ? QStringLiteral("◇ 服务状态正常") : normalMessage_; break;
    case PageState::Empty: message = QStringLiteral("○ 暂无数据，可查看其他页面"); break;
    case PageState::Error: message = QStringLiteral("× 加载失败，未执行业务操作；请重试"); break;
    case PageState::Offline: message = QStringLiteral("○ 连接已断开，保留内容可能已过期"); break;
    case PageState::Reconnecting: message = QStringLiteral("◌ 重连中，保留内容可能已过期"); break;
    case PageState::Degraded: message = QStringLiteral("△ 服务降级，当前仅显示基础内容"); break;
    case PageState::Stale: message = QStringLiteral("△ 数据已陈旧：保留上次快照，等待下一次刷新"); break;
    }
    message_->setText(message);
    message_->setAccessibleName(message);
    emit stateChanged(state);
}

void PageStateWidget::setNormalMessage(const QString &message)
{
    normalMessage_ = message;
    if (state_ == PageState::Normal) setState(state_);
}

void PageStateWidget::setMessage(const QString &message)
{
    message_->setText(message);
    message_->setAccessibleName(message);
}

QString PageStateWidget::message() const
{
    return message_->text();
}
}
