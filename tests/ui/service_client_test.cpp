#include <QtTest>

#include "fake_tcp_server.h"
#include "network/service_client.h"

#include <QSignalSpy>

namespace {

ServiceClientError failureType(const QSignalSpy &spy)
{
    return qvariant_cast<ServiceClientError>(spy.constFirst().at(0));
}

void replySuccessfully(FakeTcpServer &server)
{
    server.handler = [](QTcpSocket *socket, const QJsonObject &request) {
        socket->write(FakeTcpServer::frame(
            FakeTcpServer::response(request.value(QStringLiteral("requestId")).toString())));
    };
}

} // namespace

class ServiceClientTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { qRegisterMetaType<ServiceClientError>(); }

    void requestEnvelopeAndUniqueIds()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        replySuccessfully(server);
        ServiceClient client(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy responses(&client, &ServiceClient::responseReceived);

        const QJsonObject payload{{QStringLiteral("phone"), QStringLiteral("13812345678")}};
        const QString firstId = client.sendRequest(QStringLiteral("USER_LOGIN"), payload);
        QVERIFY(!firstId.isEmpty());
        QTRY_COMPARE(responses.count(), 1);
        QCOMPARE(server.requests.size(), 1);
        const QJsonObject first = server.requests.first();
        QCOMPARE(first.value(QStringLiteral("version")).toString(), QStringLiteral("1.0"));
        QCOMPARE(first.value(QStringLiteral("requestId")).toString(), firstId);
        QCOMPARE(first.value(QStringLiteral("action")).toString(), QStringLiteral("USER_LOGIN"));
        QCOMPARE(first.value(QStringLiteral("payload")).toObject(), payload);

        const QString secondId = client.sendRequest(QStringLiteral("USER_LOGIN"), payload);
        QVERIFY(!secondId.isEmpty());
        QVERIFY(secondId != firstId);
        QTRY_COMPARE(responses.count(), 2);
        QCOMPARE(server.requests.size(), 2);
    }

    void normalResponse()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        replySuccessfully(server);
        ServiceClient client(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy responses(&client, &ServiceClient::responseReceived);
        QSignalSpy failures(&client, &ServiceClient::requestFailed);
        client.sendRequest(QStringLiteral("USER_LOGIN"), {});
        QTRY_COMPARE(responses.count(), 1);
        QCOMPARE(failures.count(), 0);
        QVERIFY(!client.hasPendingRequest());
    }

    void partialFrame()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        QByteArray responseFrame;
        server.handler = [&responseFrame](QTcpSocket *socket, const QJsonObject &request) {
            responseFrame = FakeTcpServer::frame(
                FakeTcpServer::response(request.value(QStringLiteral("requestId")).toString()));
            socket->write(responseFrame.first(2));
        };
        ServiceClient client(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy responses(&client, &ServiceClient::responseReceived);
        client.sendRequest(QStringLiteral("USER_LOGIN"), {});
        QTRY_COMPARE(server.requests.size(), 1);
        QCOMPARE(responses.count(), 0);
        server.client()->write(responseFrame.mid(2, 5));
        QCoreApplication::processEvents();
        QCOMPARE(responses.count(), 0);
        server.client()->write(responseFrame.mid(7));
        QTRY_COMPARE(responses.count(), 1);
    }

    void stickyFramesCompleteOnlyOnce()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &request) {
            const QByteArray frame = FakeTcpServer::frame(
                FakeTcpServer::response(request.value(QStringLiteral("requestId")).toString()));
            socket->write(frame + frame);
        };
        ServiceClient client(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy responses(&client, &ServiceClient::responseReceived);
        QSignalSpy failures(&client, &ServiceClient::requestFailed);
        client.sendRequest(QStringLiteral("USER_LOGIN"), {});
        QTRY_COMPARE(responses.count(), 1);
        QTest::qWait(20);
        QCOMPARE(responses.count(), 1);
        QCOMPARE(failures.count(), 0);
    }

    void malformedJson()
    {
        verifyFailure(FrameCodec::encode(QByteArrayLiteral("not-json")),
                      ServiceClientError::MalformedJson);
    }

    void malformedEnvelope()
    {
        verifyFailure(FakeTcpServer::frame({
                          {QStringLiteral("ok"), QStringLiteral("yes")}
                      }),
                      ServiceClientError::MalformedEnvelope);
    }

    void requestIdMismatch()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &) {
            socket->write(FakeTcpServer::frame(
                FakeTcpServer::response(QStringLiteral("different-id"))));
        };
        ServiceClient client(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy failures(&client, &ServiceClient::requestFailed);
        client.sendRequest(QStringLiteral("USER_LOGIN"), {});
        QTRY_COMPARE(failures.count(), 1);
        QCOMPARE(failureType(failures), ServiceClientError::RequestIdMismatch);
    }

    void invalidFrameLength()
    {
        QByteArray invalid(4, '\0');
        const quint32 size = FrameCodec::MaxPayload + 1U;
        invalid[0] = static_cast<char>((size >> 24U) & 0xffU);
        invalid[1] = static_cast<char>((size >> 16U) & 0xffU);
        invalid[2] = static_cast<char>((size >> 8U) & 0xffU);
        invalid[3] = static_cast<char>(size & 0xffU);
        verifyFailure(invalid, ServiceClientError::ProtocolError);
    }

    void timeout()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        ServiceClient client(QStringLiteral("127.0.0.1"), server.port());
        client.setRequestTimeout(150);
        QSignalSpy failures(&client, &ServiceClient::requestFailed);
        client.sendRequest(QStringLiteral("USER_LOGIN"), {});
        QTRY_COMPARE(server.requests.size(), 1);
        QTRY_COMPARE(failures.count(), 1);
        QCOMPARE(failureType(failures), ServiceClientError::Timeout);
        QTest::qWait(20);
        QCOMPARE(failures.count(), 1);
    }

    void disconnected()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [](QTcpSocket *socket, const QJsonObject &) { socket->abort(); };
        ServiceClient client(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy failures(&client, &ServiceClient::requestFailed);
        client.sendRequest(QStringLiteral("USER_LOGIN"), {});
        QTRY_COMPARE(failures.count(), 1);
        QCOMPARE(failureType(failures), ServiceClientError::Disconnected);
    }

    void offline()
    {
        QTcpServer portReservation;
        QVERIFY(portReservation.listen(QHostAddress::LocalHost, 0));
        const quint16 unusedPort = portReservation.serverPort();
        portReservation.close();

        ServiceClient client(QStringLiteral("127.0.0.1"), unusedPort);
        QSignalSpy failures(&client, &ServiceClient::requestFailed);
        client.sendRequest(QStringLiteral("USER_LOGIN"), {});
        QTRY_COMPARE(failures.count(), 1);
        QCOMPARE(failureType(failures), ServiceClientError::Offline);
    }

    void singleInFlight()
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        ServiceClient client(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy responses(&client, &ServiceClient::responseReceived);
        const QString first = client.sendRequest(QStringLiteral("USER_LOGIN"), {});
        const QString second = client.sendRequest(QStringLiteral("USER_LOGIN"), {});
        QVERIFY(!first.isEmpty());
        QVERIFY(second.isEmpty());
        QTRY_COMPARE(server.requests.size(), 1);
        server.client()->write(FakeTcpServer::frame(FakeTcpServer::response(first)));
        QTRY_COMPARE(responses.count(), 1);
        QCOMPARE(server.requests.size(), 1);
    }

    void reconnectDoesNotReplayCompletedWrite()
    {
        FakeTcpServer first;
        QVERIFY(first.listen());
        const quint16 port = first.port();
        first.handler = [&first](QTcpSocket *socket, const QJsonObject &) {
            socket->abort();
            first.close();
        };
        ServiceClient client(QStringLiteral("127.0.0.1"), port);
        client.setReconnectInterval(50);
        QSignalSpy failures(&client, &ServiceClient::requestFailed);
        QSignalSpy states(&client, &ServiceClient::connectionStateChanged);
        client.sendRequest(QStringLiteral("WALLET_PAY"),
                           {{QStringLiteral("orderId"),QStringLiteral("ORD-1")}});
        QTRY_COMPARE(failures.count(), 1);

        FakeTcpServer replacement;
        QVERIFY(replacement.listen(port));
        QTRY_VERIFY_WITH_TIMEOUT(states.count() >= 2, 1000);
        QCOMPARE(states.last().at(0).toBool(), true);
        QTest::qWait(80);
        QCOMPARE(replacement.requests.size(), 0);
    }

private:
    static void verifyFailure(const QByteArray &response,
                              ServiceClientError expected)
    {
        FakeTcpServer server;
        QVERIFY(server.listen());
        server.handler = [&response](QTcpSocket *socket, const QJsonObject &) {
            socket->write(response);
        };
        ServiceClient client(QStringLiteral("127.0.0.1"), server.port());
        QSignalSpy failures(&client, &ServiceClient::requestFailed);
        client.sendRequest(QStringLiteral("USER_LOGIN"), {});
        QTRY_COMPARE(failures.count(), 1);
        QCOMPARE(failureType(failures), expected);
        QTest::qWait(20);
        QCOMPARE(failures.count(), 1);
    }
};

QTEST_GUILESS_MAIN(ServiceClientTest)
#include "service_client_test.moc"
