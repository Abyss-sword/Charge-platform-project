#pragma once

#include <QUrl>
#include <QWidget>

class QLabel;
class QNetworkAccessManager;
class QNetworkReply;

namespace Ui {
class PeakPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit PeakPanel(QWidget *parent = nullptr);
    void configure(const QUrl &baseUrl);
    void setHorizonHours(int hours);
    void refresh();
protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
private:
    void present(const QString &title, const QString &detail);
    QUrl baseUrl_;
    QNetworkAccessManager *manager_ = nullptr;
    QNetworkReply *reply_ = nullptr;
    QLabel *state_ = nullptr;
    QLabel *peaks_ = nullptr;
    QLabel *warning_ = nullptr;
    int horizon_ = 24;
    quint64 generation_ = 0;
};
} // namespace Ui
