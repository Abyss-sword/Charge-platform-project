#pragma once
#include <QTcpServer>
#include <QTcpSocket>
#include <QPointer>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <functional>

// Local HTTP fixture server. Delayed responses are intentional contract-test
// input only; this file is never linked into a client application.
class FakeAnalysisHttpServer : public QTcpServer
{
public:
    struct Response { QByteArray body; int status = 200; int delayMs = 0; };
    QStringList requests;
    std::function<Response(const QUrl &)> respond;
    FakeAnalysisHttpServer()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    QByteArray data = socket->property("requestData").toByteArray() + socket->readAll();
                    socket->setProperty("requestData", data);
                    if (!data.contains("\r\n\r\n") || socket->property("handled").toBool()) return;
                    socket->setProperty("handled", true);
                    const QString target = QString::fromUtf8(data.split(' ').value(1));
                    requests.append(target);
                    const Response response = respond ? respond(QUrl(target)) : Response{"{}"};
                    const QPointer<QTcpSocket> guarded(socket);
                    QTimer::singleShot(response.delayMs, this, [guarded, response] {
                        if (!guarded || guarded->state() != QAbstractSocket::ConnectedState) return;
                        guarded->write("HTTP/1.1 " + QByteArray::number(response.status)
                            + " Result\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                            + QByteArray::number(response.body.size()) + "\r\n\r\n" + response.body);
                        guarded->disconnectFromHost();
                    });
                });
            }
        });
    }
    QUrl baseUrl() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }
    static QByteArray forecast(const QString &status = "OK", int horizon = 1, const QString &station = "course:test")
    {
        QFile file(QStringLiteral(CHARGE_PLATFORM_SOURCE_DIR "/tests/fixtures/stage2/forecast/baseline.json"));
        if (!file.open(QIODevice::ReadOnly)) return {};
        auto object = QJsonDocument::fromJson(file.readAll()).object();
        object["sourceMode"] = "LIVE_ARTIFACT";
        object["analysisStationId"] = station;
        object["status"] = status;
        object["horizonHours"] = horizon;
        object["dataMode"] = "REAL_ANONYMIZED";
        object["reasons"] = QJsonArray{"NO_CAPACITY_DATA"};
        QJsonArray points;
        // Match current Member3 window without changing the shared fixtures.
        const auto origin = QDateTime::fromString(object["originTime"].toString(), Qt::ISODate);
        object["targetStart"] = origin.toString("yyyy-MM-ddTHH:mm:ssZ");
        object["targetEnd"] = origin.addSecs(horizon * 3600).toString("yyyy-MM-ddTHH:mm:ssZ");
        for (int i = 0; i < horizon; ++i) {
            points.append(QJsonObject{{"targetStart", origin.addSecs(i * 3600).toString("yyyy-MM-ddTHH:mm:ssZ")},
                {"targetEnd", origin.addSecs((i + 1) * 3600).toString("yyyy-MM-ddTHH:mm:ssZ")},
                {"horizonHours", i + 1}, {"energyKwh", QString::number(i + 1) + ".25"},
                {"meanPowerKw", QString::number(i + 1) + ".25"}, {"freePilesExpected", QJsonValue::Null},
                {"nullReason", "NO_CAPACITY_DATA"}});
        }
        object["points"] = status == "OK" || status == "EXPIRED" ? points : QJsonArray{};
        return QJsonDocument(object).toJson(QJsonDocument::Compact);
    }
};
