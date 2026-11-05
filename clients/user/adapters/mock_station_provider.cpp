#include "adapters/mock_station_provider.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

MockStationProvider::MockStationProvider(QObject *parent)
    : IStationProvider(parent)
{
}

void MockStationProvider::queryNearby(const StationQuery &query)
{
    Q_UNUSED(query)
    QTimer::singleShot(0, this, [this] {
        const MockResult mock = data_.load(UserMockDataset::Stations, scenario_);
        if (!mock.valid) {
            emit queryFinished({mock.state == Ui::PageState::Offline
                                    ? StationQueryResultKind::Offline
                                    : StationQueryResultKind::Error,
                                mock.message,
                                {}, QStringLiteral("MOCK_ERROR")});
            return;
        }

        QList<StationSummary> stations;
        if (mock.state != Ui::PageState::Empty) {
            const QJsonArray values = mock.payload.value(QStringLiteral("items")).toArray();
            for (const QJsonValue &value : values) {
                const QJsonObject station = value.toObject();
                stations.append({
                    station.value(QStringLiteral("stationId")).toInteger(),
                    station.value(QStringLiteral("name")).toString(),
                    station.value(QStringLiteral("address")).toString(),
                    station.value(QStringLiteral("latitude")).toDouble(),
                    station.value(QStringLiteral("longitude")).toDouble(),
                    station.value(QStringLiteral("priceYuanPerKwh")).toString(),
                    static_cast<int>(station.value(QStringLiteral("totalPiles")).toInteger()),
                    static_cast<int>(station.value(QStringLiteral("availablePiles")).toInteger()),
                    station.value(QStringLiteral("distanceKm")).toDouble()
                });
            }
        }
        emit queryFinished({StationQueryResultKind::Success, {}, stations,
                            QStringLiteral("OK")});
    });
}

void MockStationProvider::queryDetail(qint64 stationId)
{
    QTimer::singleShot(0, this, [this, stationId] {
        StationDetail detail;
        detail.stationId = stationId > 0 ? stationId : 1;
        detail.name = QStringLiteral("天河演示站");
        detail.address = QStringLiteral("广州市天河区演示路 1 号");
        detail.latitude = 23.1351;
        detail.longitude = 113.3245;
        detail.priceYuanPerKwh = QStringLiteral("1.20");
        detail.status = QStringLiteral("OPEN");
        detail.piles = {{1, QStringLiteral("SIM-PILE-001"), QStringLiteral("FAST"),
                         120.0, QStringLiteral("IDLE"), {}}};
        emit detailFinished({StationQueryResultKind::Success,
                             QStringLiteral("SIMULATED"), QStringLiteral("OK"), detail});
    });
}
