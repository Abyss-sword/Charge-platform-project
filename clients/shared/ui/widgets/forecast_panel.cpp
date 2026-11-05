#include "widgets/forecast_panel.h"
#include "widgets/analysis_state_presenter.h"
#include <QChart>
#include <QChartView>
#include <QDateTimeAxis>
#include <QFormLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLineSeries>
#include <QPushButton>
#include <QShowEvent>
#include <QStyle>
#include <QTableWidget>
#include <QValueAxis>
#include <QVBoxLayout>
#include <cmath>

using namespace charge::analysis;
namespace Ui {
ForecastPanel::ForecastPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName("forecastPanel");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *scope = new QLabel(QStringLiteral("已发布产物预测 · 预测原点由服务端决定。历史分析站点与当前业务站点独立，结果不用于预约或推荐。"), this);
    scope->setObjectName("forecastScope");
    scope->setWordWrap(true);
    layout->addWidget(scope);
    station_ = new QLineEdit(this);
    station_->setObjectName("forecastStation");
    station_->setPlaceholderText(QStringLiteral("分析站点 ID；留空使用服务端发布站点"));
    station_->setMaxLength(128);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->addRow(QStringLiteral("分析站点"), station_);
    layout->addLayout(form);
    refresh_ = new QPushButton(QStringLiteral("查询 / 刷新"), this);
    refresh_->setObjectName("forecastRefresh");
    layout->addWidget(refresh_);
    state_ = new QLabel(this);
    state_->setObjectName("forecastState");
    state_->setWordWrap(true);
    layout->addWidget(state_);
    body_ = new QWidget(this);
    body_->setObjectName("forecastBody");
    auto *bodyLayout = new QVBoxLayout(body_);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    metadata_ = new QLabel(body_);
    metadata_->setObjectName("forecastMetadata");
    metadata_->setWordWrap(true);
    bodyLayout->addWidget(metadata_);
    chart_ = new QChartView(new QChart, body_);
    chart_->setObjectName("forecastLoadChart");
    chart_->setMinimumHeight(210);
    chart_->setMinimumWidth(0);
    chart_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    chart_->chart()->setAnimationOptions(QChart::NoAnimation);
    bodyLayout->addWidget(chart_);
    table_ = new QTableWidget(0, 3, body_);
    table_->setObjectName("forecastPoints");
    table_->setHorizontalHeaderLabels({QStringLiteral("时间区间"), QStringLiteral("电量 kWh"), QStringLiteral("平均 kW")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->horizontalHeader()->setMinimumSectionSize(30);
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setMinimumHeight(180);
    table_->setMinimumWidth(0);
    bodyLayout->addWidget(table_);
    layout->addWidget(body_);
    connect(refresh_, &QPushButton::clicked, this, &ForecastPanel::refresh);
    connect(station_, &QLineEdit::textChanged, this, [this] { resetContext(); });
    connect(station_, &QLineEdit::returnPressed, this, &ForecastPanel::refresh);
    present(ForecastStatus::DependencyUnavailable);
}
void ForecastPanel::configure(const QUrl &url)
{
    if (!client_) {
        client_ = new AnalysisHttpClient(url, this);
        connect(client_, &AnalysisHttpClient::forecastFinished, this, &ForecastPanel::receive);
    } else client_->setBaseUrl(url);
    resetContext();
    if (isVisible()) refresh();
}
void ForecastPanel::setHorizonHours(int hours)
{
    if (horizon_ == hours) return;
    horizon_ = hours;
    resetContext();
    if (isVisible()) refresh();
}
bool ForecastPanel::retryAllowed() const { return analysisStatePresentation(status_).retryAllowed; }
void ForecastPanel::resetContext()
{
    if (client_) client_->cancel();
    generation_ = 0;
    displayed_.reset();
    table_->setRowCount(0);
    chart_->chart()->removeAllSeries();
    metadata_->clear();
    state_->setToolTip({});
    present(ForecastStatus::DependencyUnavailable);
}
void ForecastPanel::refresh()
{
    if (!client_ || !isVisible()) return;
    generation_ = client_->forecast(horizon_, station_->text().trimmed());
    state_->setText(displayed_ ? QStringLiteral("正在更新；下方为上次成功获取的发布结果。")
                               : QStringLiteral("正在获取预测数据…"));
    // Manual query remains enabled so rapid repeated requests exercise the
    // same last-request-wins rule as horizon changes. No automatic retry.
}
void ForecastPanel::present(ForecastStatus status, const QString &detail)
{
    status_ = status;
    const auto state = analysisStatePresentation(status);
    state_->setText(state.title + '\n' + state.description + (detail.isEmpty() ? QString() : '\n' + detail));
    state_->setProperty("tone", state.staleWarning ? "warning" : state.pageState == PageState::Error ? "error" : "info");
    state_->style()->unpolish(state_);
    state_->style()->polish(state_);
    body_->setVisible(state.showContent && displayed_.has_value());
    refresh_->setProperty("retryAllowed", state.retryAllowed);
    // PARAM_INVALID never retries: the explicit query button allows corrected input.
}
void ForecastPanel::receive(const AnalysisHttpResult &result)
{
    if (!isVisible() || result.generation != generation_) return;
    const bool businessError = result.parsed.valid
        && result.parsed.forecast.status != ForecastStatus::Ok;
    if (result.error != AnalysisHttpError::None && !businessError) {
        QString description;
        switch (result.error) {
        case AnalysisHttpError::Timeout: description = QStringLiteral("请求超时，请手动重试。"); break;
        case AnalysisHttpError::Network: description = QStringLiteral("无法连接分析服务，请检查网络或服务地址。"); break;
        case AnalysisHttpError::InvalidRequest: description = QStringLiteral("请检查服务地址、分析站点 ID 和预测期限。"); break;
        case AnalysisHttpError::Http: description = QStringLiteral("分析服务返回 HTTP %1。").arg(result.httpStatus); break;
        default: description = QStringLiteral("分析响应不符合数据契约，未展示本次结果。"); break;
        }
        state_->setToolTip(result.message);
        present(result.error == AnalysisHttpError::InvalidRequest ? ForecastStatus::ParamInvalid
                                                                 : ForecastStatus::DependencyUnavailable, description);
        return;
    }
    const auto &forecast = result.parsed.forecast;
    if (forecast.status == ForecastStatus::Ok
        || (forecast.status == ForecastStatus::Expired && !displayed_ && !forecast.points.isEmpty())) {
        displayed_ = forecast;
        render(forecast);
    }
    if (forecast.status == ForecastStatus::NoData) {
        displayed_.reset();
        table_->setRowCount(0);
        metadata_->clear();
        chart_->chart()->removeAllSeries();
    }
    state_->setToolTip({});
    present(forecast.status, forecast.reasons.join(QStringLiteral("；")));
}
void ForecastPanel::render(const ForecastEnvelope &forecast)
{
    const QString mode = forecast.modelMode == ForecastModelMode::Trained ? "TRAINED"
        : forecast.modelMode == ForecastModelMode::Baseline ? "BASELINE" : "UNAVAILABLE";
    const QString dataMode = forecast.dataMode == ForecastDataMode::RealAnonymized ? "REAL_ANONYMIZED"
        : forecast.dataMode == ForecastDataMode::PublicAggregated ? "PUBLIC_AGGREGATED" : "SIMULATED";
    metadata_->setText(QStringLiteral("站点：%1\n数据版本：%2 · %3 · %4\n预测原点（UTC）：%5\n发布预测窗口（UTC）：%6 — %7\n响应生成（UTC）：%8\n来源：LIVE_ARTIFACT；时间标签按服务端返回展示。")
        .arg(forecast.analysisStationId, forecast.datasetVersion, dataMode, mode,
             forecast.originTime.toString(Qt::ISODate), forecast.targetStart.toString(Qt::ISODate),
             forecast.targetEnd.toString(Qt::ISODate), forecast.generatedAt.toString(Qt::ISODate)));
    auto *chart = chart_->chart();
    chart->removeAllSeries();
    const auto axes = chart->axes();
    for (auto *axis : axes) { chart->removeAxis(axis); delete axis; }
    chart->legend()->hide();
    chart->setTitle(QStringLiteral("小时平均负荷（kW）"));
    auto *time = new QDateTimeAxis(chart);
    time->setFormat("MM-dd HH:mm");
    time->setTickCount(2);
    time->setTitleText(QStringLiteral("本地时间 · 服务端目标区间"));
    auto *power = new QValueAxis(chart);
    power->setTitleText("kW");
    chart->addAxis(time, Qt::AlignBottom);
    chart->addAxis(power, Qt::AlignLeft);
    if (forecast.targetEnd > forecast.targetStart) time->setRange(forecast.targetStart, forecast.targetEnd);
    double maximum = 1;
    table_->setRowCount(forecast.points.size());
    // Separate per-hour segments preserve null gaps and use the actual returned
    // intervals; no client-side hour alignment or synthetic prediction.
    for (int i = 0; i < forecast.points.size(); ++i) {
        const auto &point = forecast.points.at(i);
        auto *range = new QTableWidgetItem(point.targetStart.toLocalTime().toString("MM-dd HH:mm")
            + '\n' + point.targetEnd.toLocalTime().toString("MM-dd HH:mm"));
        range->setToolTip(point.targetStart.toString(Qt::ISODate) + " — " + point.targetEnd.toString(Qt::ISODate));
        table_->setItem(i, 0, range);
        table_->setItem(i, 1, new QTableWidgetItem(point.energyKwh.value_or(QStringLiteral("无数据"))));
        table_->setItem(i, 2, new QTableWidgetItem(point.meanPowerKw.value_or(QStringLiteral("无数据"))));
        if (point.meanPowerKw && point.targetEnd > point.targetStart) {
            bool valid = false;
            const double value = point.meanPowerKw->toDouble(&valid);
            if (!valid || !std::isfinite(value)) continue;
            maximum = std::max(maximum, value);
            auto *series = new QLineSeries(chart);
            series->append(point.targetStart.toMSecsSinceEpoch(), value);
            series->append(point.targetEnd.toMSecsSinceEpoch(), value);
            chart->addSeries(series);
            series->attachAxis(time); series->attachAxis(power);
        }
    }
    power->setRange(0, maximum * 1.1);
    table_->resizeRowsToContents();
}
void ForecastPanel::hideEvent(QHideEvent *event)
{
    resetContext();
    QWidget::hideEvent(event);
}
void ForecastPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (client_) refresh();
}
} // namespace Ui
