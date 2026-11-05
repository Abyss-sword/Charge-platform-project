#pragma once

#include <QUrl>
#include <QWidget>

class QLabel;
class QNetworkAccessManager;
class QNetworkReply;

namespace Ui {
class RecommendationPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit RecommendationPanel(QWidget *parent = nullptr);
    void configure(const QUrl &baseUrl);
    void refresh();
protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
private:
    QUrl baseUrl_;
    QNetworkAccessManager *manager_ = nullptr;
    QNetworkReply *reply_ = nullptr;
    QLabel *state_ = nullptr;
    QLabel *items_ = nullptr;
    QLabel *meta_ = nullptr;
    quint64 generation_ = 0;
};
} // namespace Ui
