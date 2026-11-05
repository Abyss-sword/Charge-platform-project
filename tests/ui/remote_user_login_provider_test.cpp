#include <QtTest>

#include "adapters/remote_user_login_provider.h"
#include "fake_tcp_server.h"

#include <QSignalSpy>

namespace {

LoginResult resultAt(const QSignalSpy &spy, int index = 0)
{
    return qvariant_cast<LoginResult>(spy.at(index).at(0));
}

QJsonObject loginData(bool restricted)
{
    return {
        {QStringLiteral("userId"), QJsonValue(qint64(1234567890123LL))},
        {QStringLiteral("nickname"), QStringLiteral("测试用户")},
        {QStringLiteral("restricted"), restricted},
        {QStringLiteral("userToken"), QStringLiteral("user-token-123")},
        {QStringLiteral("expiresAt"), QStringLiteral("2026-09-15T18:00:00Z")}
    };
}

} // namespace

class RemoteUserLoginProviderTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { qRegisterMetaType<LoginResult>(); }

    void success()
    {
        const LoginResult result = runSuccess(false);
        QCOMPARE(result.kind, LoginResultKind::Success);
        QCOMPARE(result.userId, qint64(1234567890123LL));
        QCOMPARE(result.nickname, QStringLiteral("测试用户"));
        QVERIFY(!result.restricted);
        QCOMPARE(result.userToken, QStringLiteral("user-token-123"));
        QCOMPARE(result.expiresAt, QStringLiteral("2026-09-15T18:00:00Z"));
    }

    void restrictedUser()
    {
        const LoginResult result = runSuccess(true);
        QCOMPARE(result.kind, LoginResultKind::Success);
        QVERIFY(result.restricted);
    }

    void serverFailureUsesInnerMessage()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            socket->write(FakeTcpServer::frame({
                {QStringLiteral("requestId"), request.value(QStringLiteral("requestId"))},
                {QStringLiteral("ok"), false},
                {QStringLiteral("code"), QStringLiteral("AUTH_INVALID")},
                {QStringLiteral("message"), QStringLiteral("outer")},
                {QStringLiteral("data"), QJsonObject{}},
                {QStringLiteral("error"), QJsonObject{
                    {QStringLiteral("code"), QStringLiteral("AUTH_INVALID")},
                    {QStringLiteral("message"), QStringLiteral("inner")}
                }}
            }));
        };
        RemoteUserLoginProvider provider(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy results(&provider, &IUserLoginProvider::loginFinished);
        provider.submit({QStringLiteral("13812345678")});
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(resultAt(results).kind, LoginResultKind::Error);
        QCOMPARE(resultAt(results).message, QStringLiteral("inner"));
    }

    void malformedSuccessData()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            QJsonObject data = loginData(false);
            data.insert(QStringLiteral("userId"), 1.5);
            socket->write(FakeTcpServer::frame(FakeTcpServer::response(
                request.value(QStringLiteral("requestId")).toString(), data)));
        };
        RemoteUserLoginProvider provider(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy results(&provider, &IUserLoginProvider::loginFinished);
        provider.submit({QStringLiteral("13812345678")});
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(resultAt(results).kind, LoginResultKind::Error);
        QVERIFY(resultAt(results).message.contains(QStringLiteral("userId")));
    }

    void offlineMapsToOffline()
    {
        QTcpServer reservation;
        QVERIFY(reservation.listen(QHostAddress::LocalHost, 0));
        const quint16 unusedPort = reservation.serverPort();
        reservation.close();
        RemoteUserLoginProvider provider(QStringLiteral("127.0.0.1"), unusedPort);
        QSignalSpy results(&provider, &IUserLoginProvider::loginFinished);
        provider.submit({QStringLiteral("13812345678")});
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(resultAt(results).kind, LoginResultKind::Offline);
    }

    void disconnectedMapsToOffline()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &) { socket->abort(); };
        RemoteUserLoginProvider provider(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy results(&provider, &IUserLoginProvider::loginFinished);
        provider.submit({QStringLiteral("13812345678")});
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(resultAt(results).kind, LoginResultKind::Offline);
    }

    void timeoutMapsToError()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        ServiceClient client(QStringLiteral("127.0.0.1"), server.port());
        client.setRequestTimeout(150);
        RemoteUserLoginProvider provider(&client);
        QSignalSpy results(&provider, &IUserLoginProvider::loginFinished);
        provider.submit({QStringLiteral("13812345678")});
        QTRY_COMPARE(server.requests.size(), 1);
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(resultAt(results).kind, LoginResultKind::Error);
    }

    void duplicateSubmitIsRejected()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        ServiceClient client(QStringLiteral("127.0.0.1"), server.port());
        RemoteUserLoginProvider provider(&client);
        QSignalSpy results(&provider, &IUserLoginProvider::loginFinished);
        const LoginRequest request{QStringLiteral("13812345678")};
        provider.submit(request);
        provider.submit(request);
        QCOMPARE(results.count(), 1);
        QCOMPARE(resultAt(results).kind, LoginResultKind::Error);
        QTRY_COMPARE(server.requests.size(), 1);
        const QString requestId = server.requests.first()
                                      .value(QStringLiteral("requestId")).toString();
        server.client()->write(FakeTcpServer::frame(
            FakeTcpServer::response(requestId, loginData(false))));
        QTRY_COMPARE(results.count(), 2);
        QCOMPARE(resultAt(results, 1).kind, LoginResultKind::Success);
        QCOMPARE(server.requests.size(), 1);
    }

private:
    static LoginResult runSuccess(bool restricted)
    {
        FakeTcpServer server;
        if (!server.listen()) {
            return {};
        }
        server.handler = [restricted](QTcpSocket *socket, const QJsonObject &request) {
            QJsonObject response = FakeTcpServer::response(
                request.value(QStringLiteral("requestId")).toString(),
                loginData(restricted));
            response.insert(QStringLiteral("message"), QStringLiteral("登录成功"));
            socket->write(FakeTcpServer::frame(response));
        };
        RemoteUserLoginProvider provider(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy results(&provider, &IUserLoginProvider::loginFinished);
        provider.submit({QStringLiteral("13812345678")});
        if (!results.wait(3000) || results.count() != 1) {
            return {};
        }
        return resultAt(results);
    }
};

QTEST_GUILESS_MAIN(RemoteUserLoginProviderTest)
#include "remote_user_login_provider_test.moc"
