#pragma once

#include "adapters/mock_data_provider.h"
#include "adapters/station_provider.h"

class MockStationProvider final : public IStationProvider
{
    Q_OBJECT
public:
    explicit MockStationProvider(QObject *parent = nullptr);

    void setScenario(MockScenario scenario) { scenario_ = scenario; }
    void queryNearby(const StationQuery &query) override;
    void queryDetail(qint64 stationId) override;

private:
    MockDataProvider data_;
    MockScenario scenario_ = MockScenario::Normal;
};
