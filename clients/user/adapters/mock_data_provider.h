#pragma once
#include "shell/primary_page.h"
#include "widgets/page_state_widget.h"
#include <QList>
#include <QString>
#include <QJsonObject>
#include <QByteArray>

// UI-only strings. These are not JSON keys, server DTOs or authoritative business facts.
struct PreviewCard
{
    QString title;
    QString body;
    bool hero = false;
};
struct PagePreview
{
    QString title;
    QString subtitle;
    QString source;
    QList<PreviewCard> cards;
    Ui::PageState state = Ui::PageState::Normal;
};
enum class MockScenario { Normal, Empty, Timeout, ServiceError, Offline };
enum class UserMockDataset { Login, Stations, Order };
struct MockResult { bool valid = false; Ui::PageState state = Ui::PageState::Normal; QJsonObject payload; QString errorCode; QString message; };
class MockDataProvider final
{
public:
    MockResult load(UserMockDataset dataset, MockScenario scenario = MockScenario::Normal) const;
    MockResult parse(const QByteArray &json, MockScenario scenario = MockScenario::Normal) const;
    PagePreview preview(PrimaryPage page) const;
    PagePreview preview(SecondaryPage page) const;
};
