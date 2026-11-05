#include "analysis/analysis_http_client.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>
#include <algorithm>

namespace charge::analysis {
AnalysisHttpClient::AnalysisHttpClient(const QUrl &baseUrl, QObject *parent)
    : QObject(parent), baseUrl_(baseUrl), manager_(new QNetworkAccessManager(this))
{
    qRegisterMetaType<AnalysisHttpResult>();
}
AnalysisHttpClient::~AnalysisHttpClient() { cancel(); }
QUrl AnalysisHttpClient::configuredBaseUrl()
{
    return QUrl(qEnvironmentVariable("CHARGE_ANALYSIS_BASE_URL", "http://127.0.0.1:8001"));
}
void AnalysisHttpClient::setBaseUrl(const QUrl &url) { cancel(); baseUrl_ = url; }
void AnalysisHttpClient::setTimeoutMs(int ms) { timeoutMs_ = std::clamp(ms, 1, 800); }
void AnalysisHttpClient::cancel()
{
    ++generation_;
    if (reply_) {
        auto *old = reply_.data();
        reply_.clear();
        old->disconnect(this);
        old->abort();
        old->deleteLater();
    }
}
quint64 AnalysisHttpClient::forecast(int horizon, const QString &station) { return get(false, horizon, station); }
quint64 AnalysisHttpClient::health() { return get(true, 0, {}); }
void AnalysisHttpClient::deliver(bool healthRequest, const AnalysisHttpResult &result)
{
    if (result.generation != generation_) return;
    if (healthRequest) emit healthFinished(result);
    else emit forecastFinished(result);
}
quint64 AnalysisHttpClient::get(bool healthRequest, int horizon, const QString &station)
{
    cancel();
    const quint64 id = generation_;
    const QRegularExpression stationPattern(QStringLiteral("^[a-z][a-z0-9_-]*:[A-Za-z0-9_.\\-]+$"));
    if (!baseUrl_.isValid() || baseUrl_.host().isEmpty()
        || (baseUrl_.scheme() != "http" && baseUrl_.scheme() != "https")
        || !baseUrl_.userInfo().isEmpty() || baseUrl_.hasQuery() || baseUrl_.hasFragment()
        || (!healthRequest && (horizon != 1 && horizon != 6 && horizon != 24))
        || (!station.isEmpty() && !stationPattern.match(station).hasMatch())) {
        AnalysisHttpResult result;
        result.generation = id;
        result.error = AnalysisHttpError::InvalidRequest;
        result.message = QStringLiteral("Invalid analysis URL, horizon, or namespaced station ID");
        QTimer::singleShot(0, this, [this, healthRequest, result] { deliver(healthRequest, result); });
        return id;
    }
    QUrl url = baseUrl_;
    QString path = url.path();
    if (!path.endsWith('/')) path += '/';
    url.setPath(path + (healthRequest ? "health" : "forecast"));
    if (!healthRequest) {
        QUrlQuery query;
        query.addQueryItem("horizon", QString::number(horizon));
        if (!station.isEmpty()) query.addQueryItem("stationId", station);
        url.setQuery(query);
    }
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto *reply = manager_->get(request);
    reply_ = reply;
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [reply] {
        reply->setProperty("analysisTimeout", true);
        reply->abort();
    });
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 2 * 1024 * 1024) {
            reply->setProperty("analysisTooLarge", true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, timer, id, healthRequest, horizon, station] {
        timer->stop();
        reply->deleteLater();
        if (id != generation_) return;
        reply_.clear();
        AnalysisHttpResult result;
        result.generation = id;
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (reply->property("analysisTimeout").toBool()) {
            result.error = AnalysisHttpError::Timeout;
            result.message = QStringLiteral("Analysis request timed out");
        } else if (reply->property("analysisTooLarge").toBool() || body.size() > 2 * 1024 * 1024) {
            result.error = AnalysisHttpError::TooLarge;
            result.message = QStringLiteral("Analysis response exceeds 2 MiB");
        } else if (result.httpStatus != 0 && (result.httpStatus < 200 || result.httpStatus >= 300)) {
            result.error = AnalysisHttpError::Http;
            result.message = QStringLiteral("HTTP %1").arg(result.httpStatus);
            if (!healthRequest) result.parsed = ForecastParser::parse(body);
        } else if (reply->error() != QNetworkReply::NoError) {
            result.error = AnalysisHttpError::Network;
            result.message = reply->errorString();
        } else if (!healthRequest) {
            result.parsed = ForecastParser::parse(body);
            if (!result.parsed.valid) {
                result.error = AnalysisHttpError::Contract;
                result.message = result.parsed.errorCode + ": " + result.parsed.message;
            }
        } else {
            QJsonParseError error;
            const auto doc = QJsonDocument::fromJson(body, &error);
            const auto object = doc.object();
            const auto status = forecastStatusFromString(object.value("status").toString());
            if (error.error != QJsonParseError::NoError || !doc.isObject()) {
                result.error = AnalysisHttpError::Json;
                result.message = QStringLiteral("Malformed health JSON");
            } else if (object.value("framework").toString() != "Flask" || !status
                       || !object.value("service").isString() || !object.value("degraded").isBool()
                       || !object.value("reasons").isArray()) {
                result.error = AnalysisHttpError::Contract;
                result.message = QStringLiteral("Invalid Flask health response");
            } else {
                result.health = {object.value("service").toString(), object.value("framework").toString(),
                                 *status, object.value("degraded").toBool(), {}};
                for (const auto &reason : object.value("reasons").toArray()) {
                    if (!reason.isString()) {
                        result.error = AnalysisHttpError::Contract;
                        result.message = QStringLiteral("Invalid health reason");
                    } else result.health.reasons.append(reason.toString());
                }
            }
        }
        // Even an HTTP error envelope must belong to the requested context.
        if (result.parsed.valid && (result.parsed.forecast.horizonHours != horizon
            || (!station.isEmpty() && result.parsed.forecast.analysisStationId != station))) {
            result.error = AnalysisHttpError::Contract;
            result.message = QStringLiteral("Response station or horizon does not match the request");
            result.parsed.valid = false;
        }
        if (result.parsed.valid && result.parsed.forecast.sourceMode != ForecastSourceMode::LiveArtifact) {
            result.parsed.valid = false;
            result.error = AnalysisHttpError::UntrustedSource;
            result.message = QStringLiteral("Remote forecast requires LIVE_ARTIFACT");
        }
        deliver(healthRequest, result);
    });
    timer->start(timeoutMs_);
    return id;
}
} // namespace charge::analysis
