#pragma once

#include "analysis/forecast_parser.h"
#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace charge::analysis {

enum class AnalysisHttpError { None, InvalidRequest, Timeout, Network, Http, Json, Contract, UntrustedSource, TooLarge };
struct AnalysisHealth
{
    QString service;
    QString framework;
    ForecastStatus status = ForecastStatus::DependencyUnavailable;
    bool degraded = true;
    QStringList reasons;
};
struct AnalysisHttpResult
{
    quint64 generation = 0;
    int httpStatus = 0;
    AnalysisHttpError error = AnalysisHttpError::None;
    QString message;
    ForecastParseResult parsed;
    AnalysisHealth health;
};

// One instance per consumer: every request (including health) supersedes the
// previous one. No automatic retries, redirects, or mock/fixture fallback.
class AnalysisHttpClient final : public QObject
{
    Q_OBJECT
public:
    explicit AnalysisHttpClient(const QUrl &baseUrl, QObject *parent = nullptr);
    ~AnalysisHttpClient() override;
    static QUrl configuredBaseUrl();
    void setBaseUrl(const QUrl &baseUrl);
    QUrl baseUrl() const { return baseUrl_; }
    void setTimeoutMs(int milliseconds);
    quint64 generation() const { return generation_; }
    quint64 forecast(int horizonHours, const QString &analysisStationId = {});
    quint64 health();
    void cancel();
signals:
    void forecastFinished(const charge::analysis::AnalysisHttpResult &result);
    void healthFinished(const charge::analysis::AnalysisHttpResult &result);
private:
    quint64 get(bool health, int horizonHours, const QString &station);
    void deliver(bool health, const AnalysisHttpResult &result);
    QUrl baseUrl_;
    QNetworkAccessManager *manager_;
    QPointer<QNetworkReply> reply_;
    quint64 generation_ = 0;
    int timeoutMs_ = 800;
};
} // namespace charge::analysis
Q_DECLARE_METATYPE(charge::analysis::AnalysisHttpResult)
