#pragma once
#include "adapters/station_provider.h"
#include "adapters/navigation_provider.h"
#include "pages/preview_page.h"

#include <QPointer>

class QPushButton;
class QComboBox;
class QVBoxLayout;

class NearbyStationsPage final : public PreviewPage
{
    Q_OBJECT
public:
    explicit NearbyStationsPage(const PagePreview &preview, QWidget *parent = nullptr);
    NearbyStationsPage(const PagePreview &preview, IStationProvider *provider,
                       QWidget *parent = nullptr);

    void refresh() override;

signals:
    void stationDetailRequested(qint64 stationId);
    void navigationRequested(const NavigationRequest &request);

private:
    void handleResult(const StationQueryResult &result);
    void renderStations(const QList<StationSummary> &stations);

    QPointer<IStationProvider> provider_;
    QVBoxLayout *stationLayout_ = nullptr;
    QPushButton *refresh_ = nullptr;
    QPushButton *detail_ = nullptr;
    QComboBox *stationSelector_ = nullptr;
    bool mockMode_ = false;
};
