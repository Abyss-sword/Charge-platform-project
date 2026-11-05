#include "pages/user_analysis_page.h"

#include "theme.h"
#include "widgets/analysis_state_presenter.h"
#include "widgets/forecast_panel.h"
#include "widgets/recommendation_panel.h"

#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QFormLayout>
#include <QFrame>
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QWidget>
#include <QVBoxLayout>

UserAnalysisPage::UserAnalysisPage(QWidget *parent)
    : Ui::PageScaffold(QStringLiteral("智能推荐与预测"),
                       QStringLiteral("站点建议与已发布负荷预测"), parent)
{
    setObjectName(QStringLiteral("userAnalysisPage"));
    setSource(QStringLiteral("分析状态 · 以正式数据源为准"));

    auto *manualSearch = new QPushButton(QStringLiteral("返回手动查找站点"), this);
    manualSearch->setObjectName(QStringLiteral("manualStationSearchAction"));
    manualSearch->setMinimumHeight(Ui::Theme::TouchHeight);
    addStateControl(manualSearch);
    connect(manualSearch, &QPushButton::clicked, this,
            &UserAnalysisPage::manualStationSearchRequested);

    auto *selection = new QWidget(this);
    selection->setObjectName(QStringLiteral("userAnalysisSelection"));
    auto *form = new QFormLayout(selection);
    form->setContentsMargins(0, 0, 0, 0);
    arrival_ = new QDateTimeEdit(QDateTime::currentDateTime(), selection);
    arrival_->setObjectName(QStringLiteral("userAnalysisArrivalTime"));
    arrival_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    arrival_->setCalendarPopup(true);
    horizon_ = new QComboBox(selection);
    horizon_->setObjectName(QStringLiteral("userAnalysisHorizon"));
    horizon_->addItem(QStringLiteral("未来 1 小时"), 1);
    horizon_->addItem(QStringLiteral("未来 6 小时"), 6);
    horizon_->addItem(QStringLiteral("未来 24 小时"), 24);
    form->addRow(QStringLiteral("计划到达时刻"), arrival_);
    form->addRow(QStringLiteral("预测期限"), horizon_);
    addStateControl(selection);

    connect(arrival_, &QDateTimeEdit::dateTimeChanged, this,
            [this] { resetAnalysisContext(); });
    connect(horizon_, &QComboBox::currentIndexChanged, this,
            [this] { resetAnalysisContext(); });

    auto *recommendation = addCard(
        QStringLiteral("站点推荐"),
        QStringLiteral("当前没有可展示的推荐结果；可继续手动查找站点。"));
    recommendation->setObjectName(QStringLiteral("userRecommendationContainer"));
    recommendation_ = new Ui::RecommendationPanel(recommendation);
    recommendation->layout()->addWidget(recommendation_);
    auto *forecast = addCard(
        QStringLiteral("预测详情"),
        QStringLiteral("当前没有可展示的预测结果。"));
    forecast->setObjectName(QStringLiteral("userForecastContainer"));
    forecast_ = new Ui::ForecastPanel(forecast);
    forecast_->hide();
    forecast->layout()->addWidget(forecast_);

    setAnalysisStatus(charge::analysis::ForecastStatus::DependencyUnavailable);
}

void UserAnalysisPage::setAnalysisStatus(charge::analysis::ForecastStatus status)
{
    analysisStatus_ = status;
    retryAllowed_ = Ui::applyAnalysisState(this, status).retryAllowed;
}

int UserAnalysisPage::selectedHorizonHours() const
{
    return horizon_ ? horizon_->currentData().toInt() : 0;
}

charge::analysis::ForecastStatus UserAnalysisPage::analysisStatus() const
{
    return remoteForecast_ ? forecast_->status() : analysisStatus_;
}
bool UserAnalysisPage::retryAllowed() const
{
    return remoteForecast_ ? forecast_->retryAllowed() : retryAllowed_;
}

QDateTime UserAnalysisPage::selectedArrivalTime() const
{
    return arrival_ ? arrival_->dateTime() : QDateTime{};
}

void UserAnalysisPage::resetAnalysisContext()
{
    if (remoteForecast_) {
        forecast_->setHorizonHours(selectedHorizonHours());
        setAnalysisStatus(charge::analysis::ForecastStatus::Ok);
        return;
    }
    setAnalysisStatus(charge::analysis::ForecastStatus::DependencyUnavailable);
}

void UserAnalysisPage::setAnalysisBaseUrl(const QUrl &url)
{
    remoteForecast_ = true;
    arrival_->setEnabled(false);
    arrival_->setToolTip(QStringLiteral("到达时刻尚未参与预测请求；当前展示服务端已发布窗口。"));
    setSource(QStringLiteral("已发布负荷预测 · 到达时刻暂不参与请求，站点推荐暂不可用"));
    auto *container = forecast_->parentWidget();
    for (auto *label : container->findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly)) label->hide();
    forecast_->setHorizonHours(selectedHorizonHours());
    forecast_->configure(url);
    // Recommendation requires a location origin that this page does not own;
    // keep the formal unavailable panel visible without issuing an invalid request.
    forecast_->show();
    resetAnalysisContext();
}

void UserAnalysisPage::hideEvent(QHideEvent *event)
{
    resetOnNextShow_ = true;
    Ui::PageScaffold::hideEvent(event);
}

void UserAnalysisPage::showEvent(QShowEvent *event)
{
    if (resetOnNextShow_) {
        resetOnNextShow_ = false;
        resetAnalysisContext();
    }
    Ui::PageScaffold::showEvent(event);
}
