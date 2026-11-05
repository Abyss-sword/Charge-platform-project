#pragma once

#include "analysis/forecast_model.h"
#include "widgets/page_state_widget.h"

#include <QString>

namespace Ui {

class PageScaffold;

struct AnalysisStatePresentation
{
    QString title;
    QString description;
    PageState pageState = PageState::Error;
    bool showContent = false;
    bool staleWarning = false;
    bool retryAllowed = false;
};

AnalysisStatePresentation analysisStatePresentation(
    charge::analysis::ForecastStatus status);

AnalysisStatePresentation applyAnalysisState(
    PageScaffold *page, charge::analysis::ForecastStatus status);

}  // namespace Ui
