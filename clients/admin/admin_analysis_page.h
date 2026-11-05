#pragma once

#include "analysis/forecast_model.h"
#include "widgets/page_scaffold.h"

class QComboBox;
class QHideEvent;
class QShowEvent;
class QStackedWidget;
class QUrl;
namespace Ui { class ForecastPanel; }
namespace Ui { class PeakPanel; }

class AdminAnalysisPage final : public Ui::PageScaffold
{
public:
    explicit AdminAnalysisPage(QWidget *parent = nullptr);
    void setAnalysisBaseUrl(const QUrl &url);
    void setAnalysisStatus(charge::analysis::ForecastStatus status);
    charge::analysis::ForecastStatus analysisStatus() const;
    int selectedHorizonHours() const;
    int currentSection() const;
    bool retryAllowed() const;

protected:
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void resetAnalysisContext();

    QComboBox *horizon_ = nullptr;
    QComboBox *section_ = nullptr;
    QStackedWidget *sections_ = nullptr;
    charge::analysis::ForecastStatus analysisStatus_ =
        charge::analysis::ForecastStatus::DependencyUnavailable;
    bool retryAllowed_ = true;
    bool resetOnNextShow_ = false;
    Ui::ForecastPanel *forecast_ = nullptr;
    Ui::PeakPanel *peak_ = nullptr;
    bool remoteForecast_ = false;
};
