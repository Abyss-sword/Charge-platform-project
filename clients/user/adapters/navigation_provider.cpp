#include "adapters/navigation_provider.h"
#include <QUrlQuery>

NavigationProvider::NavigationProvider(QObject *parent) : QObject(parent) {}

bool NavigationProvider::isAvailable() const noexcept
{
#if defined(CHARGE_HAS_WEBENGINE)
    return true;
#else
    return false;
#endif
}

QUrl NavigationProvider::routeUrl(const NavigationRequest &request, NavigationMode mode) const
{
    QUrl url(QStringLiteral("https://apis.map.qq.com/uri/v1/routeplan"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("type"), mode == NavigationMode::Driving
                           ? QStringLiteral("drive") : QStringLiteral("walk"));
    query.addQueryItem(QStringLiteral("from"), QStringLiteral("当前模拟位置"));
    query.addQueryItem(QStringLiteral("fromcoord"), QStringLiteral("%1,%2")
                       .arg(request.originLatitude, 0, 'f', 6)
                       .arg(request.originLongitude, 0, 'f', 6));
    query.addQueryItem(QStringLiteral("to"), request.stationName);
    query.addQueryItem(QStringLiteral("tocoord"), QStringLiteral("%1,%2")
                       .arg(request.destinationLatitude, 0, 'f', 6)
                       .arg(request.destinationLongitude, 0, 'f', 6));
    query.addQueryItem(QStringLiteral("referer"), QStringLiteral("charge-platform"));
    url.setQuery(query);
    return url;
}
