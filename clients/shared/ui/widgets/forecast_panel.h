#pragma once

#include "analysis/analysis_http_client.h"
#include <QWidget>
#include <optional>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QChartView;

namespace Ui {
// Published load data only. The server chooses the origin; a business station
// is never silently mapped to a historical analysis station.
class ForecastPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit ForecastPanel(QWidget *parent = nullptr);
    void configure(const QUrl &baseUrl);
    void setHorizonHours(int hours);
    int horizonHours() const { return horizon_; }
    void refresh();
    charge::analysis::ForecastStatus status() const { return status_; }
    bool retryAllowed() const;
    const std::optional<charge::analysis::ForecastEnvelope> &displayedForecast() const { return displayed_; }
protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
private:
    void resetContext();
    void receive(const charge::analysis::AnalysisHttpResult &result);
    void render(const charge::analysis::ForecastEnvelope &forecast);
    void present(charge::analysis::ForecastStatus status, const QString &detail = {});
    QLineEdit *station_;
    QPushButton *refresh_;
    QLabel *state_;
    QLabel *metadata_;
    QWidget *body_;
    QTableWidget *table_;
    QChartView *chart_;
    charge::analysis::AnalysisHttpClient *client_ = nullptr;
    quint64 generation_ = 0;
    int horizon_ = 1;
    charge::analysis::ForecastStatus status_ = charge::analysis::ForecastStatus::DependencyUnavailable;
    std::optional<charge::analysis::ForecastEnvelope> displayed_;
};
} // namespace Ui
