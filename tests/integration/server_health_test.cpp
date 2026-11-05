// 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
// 服务健康集成测试：启动真实 TCP 服务验证健康响应与协议异常处理——证明网络层可收发请求。

#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QtTest>

#include "server/network/tcp_server.h"
#include "shared/protocol/frame_codec.h"

class ServerHealthTest final : public QObject
{
    Q_OBJECT

private:
    static QJsonObject send(QTcpSocket &socket, const QJsonObject &request)
    {
        socket.write(FrameCodec::encode(QJsonDocument(request).toJson(QJsonDocument::Compact)));
        socket.flush();
        if (!socket.waitForReadyRead(2000)) {
            return {};
        }
        FrameCodec codec;
        auto frames = codec.feed(socket.readAll());
        while (frames.isEmpty() && socket.waitForReadyRead(200)) {
            frames.append(codec.feed(socket.readAll()));
        }
        return frames.isEmpty() ? QJsonObject{}
                                : QJsonDocument::fromJson(frames.first()).object();
    }

    static QJsonObject send(TcpServer &server, const QJsonObject &request)
    {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        if (!socket.waitForConnected(2000)) {
            return {};
        }
        return send(socket, request);
    }

private slots:
    void documentRevisionIsNotTheWireVersion()
    {
        TcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));
        const auto wrong = send(socket, {{"version", "2.0"}, {"requestId", "VERSION-2"},
                                        {"action", "PING"}, {"payload", QJsonObject{}}});
        QVERIFY(!wrong.value("ok").toBool());
        QCOMPARE(wrong.value("code").toString(), QStringLiteral("PARAM_INVALID"));
        const auto correct = send(socket, {{"version", "1.0"}, {"requestId", "VERSION-1"},
                                          {"action", "PING"}, {"payload", QJsonObject{}}});
        QVERIFY(correct.value("ok").toBool());
        QCOMPARE(correct.value("requestId").toString(), QStringLiteral("VERSION-1"));
    }

    void createsOneWorkerThreadPerConnection()
    {
        TcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));

        QTcpSocket first;
        QTcpSocket second;
        first.connectToHost(QHostAddress::LocalHost, server.port());
        second.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(first.waitForConnected(2000));
        QVERIFY(second.waitForConnected(2000));
        QTRY_COMPARE_WITH_TIMEOUT(server.connectionThreadCount(), 2, 2000);

        first.disconnectFromHost();
        second.disconnectFromHost();
        if (first.state() != QAbstractSocket::UnconnectedState) {
            QVERIFY(first.waitForDisconnected(2000));
        }
        if (second.state() != QAbstractSocket::UnconnectedState) {
            QVERIFY(second.waitForDisconnected(2000));
        }
        QTRY_COMPARE_WITH_TIMEOUT(server.connectionThreadCount(), 0, 2000);
    }

    void returnsPongWithSameRequestId()
    {
        TcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QJsonObject response = send(server, {
            {"version", "1.0"}, {"requestId", "REQ-1"},
            {"action", "PING"}, {"payload", QJsonObject{}}});
        QCOMPARE(response.value("requestId").toString(), QStringLiteral("REQ-1"));
        QVERIFY(response.value("ok").toBool());
        QCOMPARE(response.value("data").toObject().value("reply").toString(),
                 QStringLiteral("PONG"));
    }

    void rejectsUnknownActionAndKeepsServing()
    {
        TcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(socket.waitForConnected(2000));

        const auto unknown = send(socket, {
            {"version", "1.0"}, {"requestId", "REQ-2"},
            {"action", "NOT_SUPPORTED"}, {"payload", QJsonObject{}}});
        QVERIFY(!unknown.value("ok").toBool());
        QCOMPARE(unknown.value("error").toObject().value("code").toString(),
                 QStringLiteral("PARAM_INVALID"));

        const auto ping = send(socket, {
            {"version", "1.0"}, {"requestId", "REQ-3"},
            {"action", "PING"}, {"payload", QJsonObject{}}});
        QVERIFY(ping.value("ok").toBool());
    }
};

QTEST_MAIN(ServerHealthTest)
#include "server_health_test.moc"
