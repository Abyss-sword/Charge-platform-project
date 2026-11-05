#include "theme.h"
#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QMap>
#include <QResource>
#include <QDebug>

static void initializeUiResources()
{
    Q_INIT_RESOURCE(ui);
}

namespace Ui {
QFont Theme::font()
{
    QFont result = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    const auto families = QFontDatabase::families();
    for (const auto &name : {QStringLiteral("Source Han Sans SC"),
                             QStringLiteral("Noto Sans CJK SC"), QStringLiteral("Microsoft YaHei")}) {
        if (families.contains(name)) { result.setFamily(name); break; }
    }
    result.setPixelSize(BodyPixels);
    return result;
}

void Theme::apply(QApplication &application)
{
    initializeUiResources();
    QFile file(QStringLiteral(":/ui/common.qss"));
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("Cannot load shared UI stylesheet");
    }
    QString qss = QString::fromUtf8(file.readAll());
    const QMap<QString, QString> values{
        {"Brand", Brand}, {"Action", Action}, {"Charging", Charging},
        {"Energy", Energy}, {"Positive", Positive}, {"Warning", Warning},
        {"Error", Error}, {"Prediction", Prediction}, {"Background", Background},
        {"Card", Card}, {"Text", Text}, {"Secondary", Secondary}, {"Border", Border},
        {"HeroEnd", HeroEnd}, {"Radius", QString::number(Radius)},
        {"CardRadius", QString::number(CardRadius)}, {"HeroRadius", QString::number(HeroRadius)},
        {"Body", QString::number(BodyPixels)}, {"Caption", QString::number(CaptionPixels)},
        {"Title", QString::number(TitlePixels)}, {"Section", QString::number(SectionPixels)}
    };
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        qss.replace("@" + it.key() + "@", it.value());
    application.setFont(font());
    application.setStyleSheet(qss);
}
}
