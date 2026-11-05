#include "widgets/analysis_state_presenter.h"

#include "widgets/page_scaffold.h"

namespace Ui {

AnalysisStatePresentation analysisStatePresentation(
    charge::analysis::ForecastStatus status)
{
    using charge::analysis::ForecastStatus;
    switch (status) {
    case ForecastStatus::Ok:
        return {QStringLiteral("分析数据可用"),
                QStringLiteral("预测结果已按当前数据版本生成。"),
                PageState::Normal, true, false, false};
    case ForecastStatus::NoData:
        return {QStringLiteral("暂无分析数据"),
                QStringLiteral("当前站点与所选时段没有足够数据。"),
                PageState::Empty, false, false, false};
    case ForecastStatus::Expired:
        return {QStringLiteral("分析数据已过期"),
                QStringLiteral("当前结果可能不是最新；可暂时查看并等待更新。"),
                PageState::Stale, true, true, true};
    case ForecastStatus::ModelUnavailable:
        return {QStringLiteral("模型服务暂不可用"),
                QStringLiteral("暂时无法生成预测结果，基础业务功能不受影响。"),
                PageState::Error, false, false, true};
    case ForecastStatus::DependencyUnavailable:
        return {QStringLiteral("数据依赖暂不可用"),
                QStringLiteral("分析数据源或批处理暂不可用，基础业务功能不受影响。"),
                PageState::Error, false, false, true};
    case ForecastStatus::ParamInvalid:
        return {QStringLiteral("请求参数无效"),
                QStringLiteral("请选择受支持的站点、时刻或预测期限。"),
                PageState::Error, false, false, false};
    }
    return {};
}

AnalysisStatePresentation applyAnalysisState(
    PageScaffold *page, charge::analysis::ForecastStatus status)
{
    const AnalysisStatePresentation presentation = analysisStatePresentation(status);
    if (!page) {
        return presentation;
    }
    page->setPageState(presentation.pageState);
    page->stateWidget()->setMessage(
        presentation.title + QLatin1Char('\n') + presentation.description);
    return presentation;
}

}  // namespace Ui
