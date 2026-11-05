#pragma once
#include "widgets/page_state_widget.h"
#include <QString>
#include <QList>
#include <QJsonObject>
#include <QByteArray>
struct AdminPreview { QString title; QString subtitle; QString source; QList<QString> cards; Ui::PageState state = Ui::PageState::Normal; };
enum class AdminPage { Overview, Revenue, StationAssets, PileDevices, Orders, Users, AlertsTickets, Logs, Analysis };
enum class AdminMockScenario { Normal, Empty, Timeout, ServiceError, Offline };
enum class AdminMockDataset { Login, Overview, Devices, Orders };
struct AdminMockResult { bool valid = false; Ui::PageState state = Ui::PageState::Normal; QJsonObject payload; QString errorCode; QString message; };
class AdminMockDataProvider final { public: AdminPreview preview(AdminPage page) const; AdminMockResult load(AdminMockDataset dataset, AdminMockScenario scenario = AdminMockScenario::Normal) const; AdminMockResult parse(const QByteArray &json, AdminMockScenario scenario = AdminMockScenario::Normal) const; };
