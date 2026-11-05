#pragma once

#include "adapters/user_login_provider.h"
#include "network/service_client.h"

#include <QPointer>

class RemoteUserLoginProvider final : public IUserLoginProvider
{
    Q_OBJECT
public:
    explicit RemoteUserLoginProvider(QString host, quint16 port, QObject *parent = nullptr);

    // The injected client is non-owning and must be dedicated to this provider.
    explicit RemoteUserLoginProvider(ServiceClient *client, QObject *parent = nullptr);

    void submit(const LoginRequest &request) override;

private:
    void connectClient();
    void handleResponse(const QJsonObject &response);
    void handleFailure(ServiceClientError error, const QString &message);

    QPointer<ServiceClient> client_;
    bool waitingForResponse_ = false;
};
