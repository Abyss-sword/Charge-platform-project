#include "widgets/peak_panel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QShowEvent>
#include <QTimer>
#include <QPointer>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace Ui {
PeakPanel::PeakPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("peakPanel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    state_ = new QLabel(this); state_->setObjectName(QStringLiteral("peakState")); state_->setWordWrap(true); layout->addWidget(state_);
    peaks_ = new QLabel(this); peaks_->setObjectName(QStringLiteral("peakResults")); peaks_->setWordWrap(true); layout->addWidget(peaks_);
    warning_ = new QLabel(this); warning_->setObjectName(QStringLiteral("peakWarning")); warning_->setWordWrap(true); layout->addWidget(warning_);
    present(QStringLiteral("预测高峰暂不可用"), QStringLiteral("等待正式分析服务返回结果。"));
}
void PeakPanel::configure(const QUrl &url) { baseUrl_ = url; if (!manager_) manager_ = new QNetworkAccessManager(this); if (isVisible()) refresh(); }
void PeakPanel::setHorizonHours(int hours) { horizon_ = hours; if (isVisible()) refresh(); }
void PeakPanel::present(const QString &title, const QString &detail) { state_->setText(title + QStringLiteral("\n") + detail); peaks_->clear(); warning_->clear(); }
void PeakPanel::refresh()
{
    if (!baseUrl_.isValid() || !isVisible()) return;
    ++generation_;
    if (reply_) { reply_->abort(); reply_->deleteLater(); reply_ = nullptr; }
    QUrl url = baseUrl_; auto path = url.path(); if (!path.endsWith('/')) path += '/'; url.setPath(path + QStringLiteral("analysis/peak"));
    QUrlQuery query; query.addQueryItem(QStringLiteral("horizon"), QString::number(horizon_)); url.setQuery(query);
    const auto id = generation_; reply_ = manager_->get(QNetworkRequest(url));
    const QPointer<QNetworkReply> guarded(reply_);
    QTimer::singleShot(800, reply_, [guarded] { if (guarded && !guarded->isFinished()) guarded->abort(); });
    connect(reply_, &QNetworkReply::finished, this, [this, id] {
        auto *reply = reply_; reply_ = nullptr; reply->deleteLater(); if (id != generation_) return;
        if (reply->error() != QNetworkReply::NoError) { present(QStringLiteral("预测高峰暂不可用"), QStringLiteral("无法连接分析服务。")); return; }
        QJsonParseError error; const auto doc = QJsonDocument::fromJson(reply->readAll(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) { present(QStringLiteral("预测响应无效"), QStringLiteral("未展示不符合契约的响应。")); return; }
        const auto root = doc.object(); const auto status = root.value("status").toString();
        if (status != QStringLiteral("OK")) { present(QStringLiteral("预测高峰暂不可用"), root.value("reasons").toVariant().toString()); return; }
        QStringList rows; for (const auto &value : root.value("peaks").toArray()) { const auto item = value.toObject(); rows << QStringLiteral("#%1  %2  %3").arg(item.value("rank").toInt()).arg(item.value("targetStart").toString(), item.value("energyKwh").toString()); }
        peaks_->setText(rows.isEmpty() ? QStringLiteral("暂无未来高峰数据。") : QStringLiteral("未来 Top3：\n") + rows.join('\n'));
        const auto warning = root.value("warning").toObject(); const auto level = warning.value("level").toString(QStringLiteral("UNKNOWN"));
        const auto reasons = warning.value("reasons").toArray(); QStringList reasonTexts; for (const auto &reason : reasons) reasonTexts << reason.toString();
        warning_->setText(QStringLiteral("预测预警：%1 · %2").arg(level, reasonTexts.join(QStringLiteral("、"))));
        state_->setText(QStringLiteral("正式预测响应 · 不代表已发生设备故障；不执行自动控制。"));
    });
}
void PeakPanel::showEvent(QShowEvent *event) { QWidget::showEvent(event); refresh(); }
void PeakPanel::hideEvent(QHideEvent *event) { ++generation_; if (reply_) reply_->abort(); QWidget::hideEvent(event); }
} // namespace Ui
