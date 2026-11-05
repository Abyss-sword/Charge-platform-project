#pragma once

#include "adapters/station_provider.h"
#include "network/service_client.h"

#include <QPointer>

class RemoteStationProvider final : public IStationProvider
{
    Q_OBJECT
public:
    explicit RemoteStationProvider(QString host, quint16 port, QObject *parent = nullptr);

    // The injected client is non-owning and must be dedicated to this provider.
    explicit RemoteStationProvider(ServiceClient *client, QObject *parent = nullptr);

    void queryNearby(const StationQuery &query) override;
    void queryDetail(qint64 stationId) override;

private:
    void connectClient();
    enum class Pending { None, Nearby, Detail };
    void handleResponse(const QJsonObject &response, Pending pending);
    void handleFailure(ServiceClientError error, const QString &message);

    QPointer<ServiceClient> client_;
    Pending pending_ = Pending::None;
};
