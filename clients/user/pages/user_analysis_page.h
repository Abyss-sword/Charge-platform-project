#pragma once

#include "analysis/forecast_model.h"
#include "widgets/page_scaffold.h"

class QComboBox;
class QDateTime;
class QDateTimeEdit;
class QHideEvent;
class QShowEvent;
class QUrl;
namespace Ui { class ForecastPanel; }
namespace Ui { class RecommendationPanel; }

class UserAnalysisPage final : public Ui::PageScaffold
{
    Q_OBJECT

public:
    explicit UserAnalysisPage(QWidget *parent = nullptr);
    void setAnalysisBaseUrl(const QUrl &url);
    void setAnalysisStatus(charge::analysis::ForecastStatus status);
    charge::analysis::ForecastStatus analysisStatus() const;
    int selectedHorizonHours() const;
    QDateTime selectedArrivalTime() const;
    bool retryAllowed() const;

signals:
    void manualStationSearchRequested();

protected:
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void resetAnalysisContext();

    QDateTimeEdit *arrival_ = nullptr;
    QComboBox *horizon_ = nullptr;
    charge::analysis::ForecastStatus analysisStatus_ =
        charge::analysis::ForecastStatus::DependencyUnavailable;
    bool retryAllowed_ = true;
    bool resetOnNextShow_ = false;
    Ui::ForecastPanel *forecast_ = nullptr;
    Ui::RecommendationPanel *recommendation_ = nullptr;
    bool remoteForecast_ = false;
};
