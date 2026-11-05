#include "widgets/metric_card.h"

#include <QLabel>
#include <QVBoxLayout>

namespace Ui {

MetricCard::MetricCard(const QString &title, QWidget *parent)
    : QFrame(parent),
      title_(new QLabel(title, this)),
      value_(new QLabel(QStringLiteral("--"), this)),
      unit_(new QLabel(this)),
      caption_(new QLabel(this))
{
    setObjectName(QStringLiteral("metricCard"));
    title_->setObjectName(QStringLiteral("metricCardTitle"));
    value_->setObjectName(QStringLiteral("metricCardValue"));
    caption_->setObjectName(QStringLiteral("metricCardCaption"));
    caption_->setWordWrap(true);
    value_->setStyleSheet(QStringLiteral("font-size:20px;font-weight:600;"));
    caption_->setStyleSheet(QStringLiteral("color:#6b7280;font-size:11px;"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->addWidget(title_);
    auto *valueRow = new QHBoxLayout;
    valueRow->addWidget(value_);
    valueRow->addWidget(unit_);
    valueRow->addStretch(1);
    layout->addLayout(valueRow);
    layout->addWidget(caption_);
}

void MetricCard::setValue(const QString &value, const QString &unit)
{
    value_->setText(value);
    unit_->setText(unit);
}

void MetricCard::setCaption(const QString &caption)
{
    caption_->setText(caption);
}

QString MetricCard::value() const
{
    return value_->text();
}

}  // namespace Ui
