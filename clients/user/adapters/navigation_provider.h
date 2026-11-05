#pragma once

#include <QObject>
#include <QUrl>

struct NavigationRequest
{
    double originLatitude = 0.0;
    double originLongitude = 0.0;
    double destinationLatitude = 0.0;
    double destinationLongitude = 0.0;
    QString stationName;
};

enum class NavigationMode { Driving, Walking };

class NavigationProvider final : public QObject
{
    Q_OBJECT
public:
    explicit NavigationProvider(QObject *parent = nullptr);
    bool isAvailable() const noexcept;
    QUrl routeUrl(const NavigationRequest &request, NavigationMode mode) const;
};
