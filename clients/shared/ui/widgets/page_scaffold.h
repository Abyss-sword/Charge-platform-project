#pragma once
#include <QWidget>
#include "widgets/page_state_widget.h"
class QVBoxLayout;
class QFrame;
class QLabel;
class QPushButton;
namespace Ui {
class PageScaffold : public QWidget
{
    Q_OBJECT
public:
    explicit PageScaffold(const QString &title, const QString &subtitle, QWidget *parent = nullptr);
    QFrame *addCard(const QString &title, const QString &body, bool hero = false);
    void addContent(QWidget *widget);
    QPushButton *primaryAction() const { return action_; }
    PageStateWidget *stateWidget() const { return state_; }
    void setPageState(PageState state);
    void setNormalStateMessage(const QString &message);
    void setAction(const QString &text, bool available = true);
    void setSource(const QString &text);
    void addStateControl(QWidget *control);
protected:
    static QFrame *createCard(const QString &title, const QString &body, bool hero, QWidget *parent);
private:
    QVBoxLayout *contentLayout_;
    QVBoxLayout *outerLayout_;
    QWidget *content_;
    QLabel *source_;
    PageStateWidget *state_;
    QPushButton *action_;
    bool actionAvailable_ = true;
};
}
