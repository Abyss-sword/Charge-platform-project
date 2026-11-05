#include "widgets/recommendation_panel.h"

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
RecommendationPanel::RecommendationPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("recommendationPanel"));
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    state_ = new QLabel(this); state_->setObjectName(QStringLiteral("recommendationState")); state_->setWordWrap(true); layout->addWidget(state_);
    items_ = new QLabel(this); items_->setObjectName(QStringLiteral("recommendationItems")); items_->setWordWrap(true); layout->addWidget(items_);
    meta_ = new QLabel(this); meta_->setObjectName(QStringLiteral("recommendationMeta")); meta_->setWordWrap(true); layout->addWidget(meta_);
    state_->setText(QStringLiteral("推荐服务暂不可用\n可继续手动查找站点。"));
}
void RecommendationPanel::configure(const QUrl &url) { baseUrl_ = url; if (!manager_) manager_ = new QNetworkAccessManager(this); if (isVisible()) refresh(); }
void RecommendationPanel::refresh()
{
    if (!baseUrl_.isValid() || !isVisible()) return;
    ++generation_; if (reply_) { reply_->abort(); reply_->deleteLater(); reply_ = nullptr; }
    QUrl url = baseUrl_; auto path = url.path(); if (!path.endsWith('/')) path += '/'; url.setPath(path + QStringLiteral("recommend"));
    const auto id = generation_; reply_ = manager_->get(QNetworkRequest(url));
    const QPointer<QNetworkReply> guarded(reply_);
    QTimer::singleShot(800, reply_, [guarded] { if (guarded && !guarded->isFinished()) guarded->abort(); });
    connect(reply_, &QNetworkReply::finished, this, [this, id] {
        auto *reply = reply_; reply_ = nullptr; reply->deleteLater(); if (id != generation_) return;
        if (reply->error() != QNetworkReply::NoError) { state_->setText(QStringLiteral("推荐服务暂不可用\n无法连接正式分析服务。")); return; }
        QJsonParseError error; const auto doc = QJsonDocument::fromJson(reply->readAll(), &error); if (error.error != QJsonParseError::NoError || !doc.isObject()) { state_->setText(QStringLiteral("推荐响应无效\n未展示不符合契约的响应。")); return; }
        const auto root = doc.object(); const auto status = root.value("status").toString();
        if (status != QStringLiteral("OK")) { state_->setText(QStringLiteral("推荐服务暂不可用\n%1").arg(root.value("reasons").toVariant().toString())); items_->clear(); return; }
        QStringList rows; for (const auto &value : root.value("recommendations").toArray()) { const auto item = value.toObject(); QStringList reasons; for (const auto &reason : item.value("reasons").toArray()) reasons << reason.toString(); rows << QStringLiteral("#%1 %2 · score %3\n%4").arg(item.value("rank").toInt()).arg(item.value("name").toString()).arg(item.value("score").toString()).arg(reasons.join(QStringLiteral("、"))); }
        state_->setText(QStringLiteral("正式推荐结果")); items_->setText(rows.isEmpty() ? QStringLiteral("暂无推荐站点。") : rows.join('\n'));
        const auto origin = root.value("origin").toObject(); meta_->setText(QStringLiteral("来源：%1 · 数据版本：%2\n到达时刻当前未参与推荐评分：%3").arg(root.value("source").toString(), root.value("datasetVersion").toString(), origin.value("arrivalTimeUsed").toBool() ? QStringLiteral("否") : QStringLiteral("是")));
    });
}
void RecommendationPanel::showEvent(QShowEvent *event) { QWidget::showEvent(event); refresh(); }
void RecommendationPanel::hideEvent(QHideEvent *event) { ++generation_; if (reply_) reply_->abort(); QWidget::hideEvent(event); }
} // namespace Ui
