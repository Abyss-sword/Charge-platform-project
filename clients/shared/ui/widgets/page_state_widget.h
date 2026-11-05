#pragma once
#include <QFrame>
class QLabel;
namespace Ui {
// Presentation only: neither a transport ConnectionState nor an order state.
enum class PageState { Loading, Normal, Empty, Error, Offline, Reconnecting, Degraded, Stale };
QString pageStateName(PageState state);
class PageStateWidget final : public QFrame
{
    Q_OBJECT
public:
    explicit PageStateWidget(QWidget *parent = nullptr);
    void setState(PageState state);
    void setNormalMessage(const QString &message);
    void setMessage(const QString &message);
    QString message() const;
    PageState state() const { return state_; }
signals:
    void stateChanged(Ui::PageState state);
private:
    PageState state_ = PageState::Normal;
    QLabel *message_;
    QString normalMessage_;
};
}
Q_DECLARE_METATYPE(Ui::PageState)
