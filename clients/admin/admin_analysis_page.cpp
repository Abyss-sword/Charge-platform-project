#include "admin_analysis_page.h"

#include "widgets/analysis_state_presenter.h"
#include "widgets/forecast_panel.h"
#include "widgets/peak_panel.h"

#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QShowEvent>
#include <QStackedWidget>
#include <QWidget>
#include <QVBoxLayout>

AdminAnalysisPage::AdminAnalysisPage(QWidget *parent)
    : Ui::PageScaffold(QStringLiteral("预测与预警"),
                       QStringLiteral("未来负荷、空闲桩、高峰与预测风险"), parent)
{
    setObjectName(QStringLiteral("adminAnalysisPage"));
    setSource(QStringLiteral("分析状态 · 以正式数据源为准"));

    auto *selection = new QWidget(this);
    selection->setObjectName(QStringLiteral("adminAnalysisSelection"));
    auto *form = new QFormLayout(selection);
    form->setContentsMargins(0, 0, 0, 0);
    horizon_ = new QComboBox(selection);
    horizon_->setObjectName(QStringLiteral("adminAnalysisHorizon"));
    horizon_->addItem(QStringLiteral("未来 1 小时"), 1);
    horizon_->addItem(QStringLiteral("未来 6 小时"), 6);
    horizon_->addItem(QStringLiteral("未来 24 小时"), 24);
    section_ = new QComboBox(selection);
    section_->setObjectName(QStringLiteral("adminAnalysisSection"));
    section_->addItems({QStringLiteral("未来负荷"), QStringLiteral("空闲桩"),
                        QStringLiteral("Top3 高峰"), QStringLiteral("预测预警")});
    form->addRow(QStringLiteral("预测期限"), horizon_);
    form->addRow(QStringLiteral("查看分区"), section_);

    auto *riskMeaning = new QLabel(QStringLiteral("预测风险：未来态势提示"), selection);
    riskMeaning->setObjectName(QStringLiteral("adminForecastRiskLabel"));
    riskMeaning->setProperty("badge", true);
    riskMeaning->setProperty("tone", "warning");
    auto *faultMeaning = new QLabel(QStringLiteral("已发生设备故障：请前往告警工单处理"), selection);
    faultMeaning->setObjectName(QStringLiteral("adminActualFaultLabel"));
    faultMeaning->setProperty("badge", true);
    faultMeaning->setProperty("tone", "error");
    riskMeaning->setWordWrap(true);
    faultMeaning->setWordWrap(true);
    auto *meaningRow = new QHBoxLayout;
    meaningRow->addWidget(riskMeaning);
    meaningRow->addWidget(faultMeaning);
    form->addRow(QStringLiteral("语义区分"), meaningRow);
    addStateControl(selection);

    sections_ = new QStackedWidget(this);
    sections_->setObjectName(QStringLiteral("adminAnalysisSections"));
    QFrame *load = createCard(
        QStringLiteral("未来负荷"),
        QStringLiteral("当前没有可展示的未来负荷结果。"), false, sections_);
    load->setObjectName(QStringLiteral("adminLoadForecastContainer"));
    forecast_ = new Ui::ForecastPanel(load);
    forecast_->hide();
    load->layout()->addWidget(forecast_);
    QFrame *freePiles = createCard(
        QStringLiteral("空闲桩预测"),
        QStringLiteral("当前没有可展示的空闲桩预测结果。"), false, sections_);
    freePiles->setObjectName(QStringLiteral("adminFreePilesForecastContainer"));
    QFrame *peaks = createCard(
        QStringLiteral("Top3 高峰"),
        QStringLiteral("当前没有可展示的未来高峰结果。"), false, sections_);
    peaks->setObjectName(QStringLiteral("adminPeakForecastContainer"));
    peak_ = new Ui::PeakPanel(peaks);
    peaks->layout()->addWidget(peak_);
    QFrame *warnings = createCard(
        QStringLiteral("预测预警"),
        QStringLiteral("预测风险仅用于值守与调度建议，不代表已发生设备故障，也不会控制设备。"),
        false, sections_);
    warnings->setObjectName(QStringLiteral("adminForecastWarningContainer"));
    sections_->addWidget(load);
    sections_->addWidget(freePiles);
    sections_->addWidget(peaks);
    sections_->addWidget(warnings);
    addContent(sections_);

    connect(horizon_, &QComboBox::currentIndexChanged, this,
            [this] { resetAnalysisContext(); });
    connect(section_, &QComboBox::currentIndexChanged, this,
            [this](int index) {
                sections_->setCurrentIndex(index);
                if (peak_ && index == 2) peak_->setHorizonHours(selectedHorizonHours());
                resetAnalysisContext();
            });

    setAnalysisStatus(charge::analysis::ForecastStatus::DependencyUnavailable);
}

void AdminAnalysisPage::setAnalysisStatus(charge::analysis::ForecastStatus status)
{
    analysisStatus_ = status;
    retryAllowed_ = Ui::applyAnalysisState(this, status).retryAllowed;
}

int AdminAnalysisPage::selectedHorizonHours() const
{
    return horizon_ ? horizon_->currentData().toInt() : 0;
}

charge::analysis::ForecastStatus AdminAnalysisPage::analysisStatus() const
{
    return remoteForecast_ && currentSection() == 0 ? forecast_->status() : analysisStatus_;
}
bool AdminAnalysisPage::retryAllowed() const
{
    return remoteForecast_ && currentSection() == 0 ? forecast_->retryAllowed() : retryAllowed_;
}

int AdminAnalysisPage::currentSection() const
{
    return sections_ ? sections_->currentIndex() : -1;
}

void AdminAnalysisPage::resetAnalysisContext()
{
    if (remoteForecast_ && currentSection() == 0) {
        forecast_->setHorizonHours(selectedHorizonHours());
        setAnalysisStatus(charge::analysis::ForecastStatus::Ok);
        return;
    }
    setAnalysisStatus(charge::analysis::ForecastStatus::DependencyUnavailable);
}

void AdminAnalysisPage::setAnalysisBaseUrl(const QUrl &url)
{
    remoteForecast_ = true;
    auto *container = forecast_->parentWidget();
    for (auto *label : container->findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly)) label->hide();
    forecast_->setHorizonHours(selectedHorizonHours());
    forecast_->configure(url);
    forecast_->show();
    peak_->configure(url);
    setSource(QStringLiteral("已发布负荷预测与高峰分析 · 空闲桩容量数据暂不可用"));
    resetAnalysisContext();
}

void AdminAnalysisPage::hideEvent(QHideEvent *event)
{
    resetOnNextShow_ = true;
    Ui::PageScaffold::hideEvent(event);
}

void AdminAnalysisPage::showEvent(QShowEvent *event)
{
    if (resetOnNextShow_) {
        resetOnNextShow_ = false;
        resetAnalysisContext();
    }
    Ui::PageScaffold::showEvent(event);
}
