#pragma once

#include "adapters/user_account_provider.h"
#include "network/service_client.h"

#include <QPointer>

class RemoteUserAccountProvider final : public IUserAccountProvider
{
    Q_OBJECT
public:
    explicit RemoteUserAccountProvider(QString host, quint16 port,
                                       QObject *parent = nullptr);
    explicit RemoteUserAccountProvider(ServiceClient *client, QObject *parent = nullptr);

    void queryProfile(qint64 userId) override;
    void queryBalance(qint64 userId) override;
    void setUserToken(const QString &userToken) override;
    void updateProfile(qint64 userId, const QString &nickname,
                       const QString &avatarPath = {}) override;

private:
    enum class Pending { None, Profile, Balance, ProfileUpdate };

    void connectClient();
    void send(Pending pending, const QString &action, qint64 userId);
    void sendProfileUpdate(qint64 userId, const QString &nickname, const QString &avatarPath);
    void handleResponse(const QJsonObject &response);
    void handleFailure(ServiceClientError error, const QString &message);
    void emitError(Pending pending, UserAccountResultKind kind,
                   const QString &code, const QString &message);

    QPointer<ServiceClient> client_;
    Pending pending_ = Pending::None;
    QString userToken_;
};
