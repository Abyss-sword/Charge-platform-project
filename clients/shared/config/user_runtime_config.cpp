#include "config/user_runtime_config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>

namespace {

QString findConfigFile()
{
    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QString currentDir = QDir::currentPath();
    const QStringList candidates{
        QDir(applicationDir).filePath(QStringLiteral("app.ini")),
        QDir(applicationDir).filePath(QStringLiteral("config/app.ini")),
        QDir(currentDir).filePath(QStringLiteral("app.ini")),
        QDir(currentDir).filePath(QStringLiteral("config/app.ini"))
    };
    for (const QString &candidate : candidates) {
        const QFileInfo file(candidate);
        if (file.isFile() && file.isReadable()) {
            return file.absoluteFilePath();
        }
    }
    return {};
}

} // namespace

UserRuntimeConfig loadUserRuntimeConfig(const QString &filePath)
{
    UserRuntimeConfig config;
    const QString resolvedPath = filePath.isEmpty() ? findConfigFile() : filePath;
    if (resolvedPath.isEmpty() || !QFileInfo::exists(resolvedPath)) {
        return config;
    }

    QSettings settings(resolvedPath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("server"));

    const QString host = settings.value(QStringLiteral("host"), config.serverHost)
                             .toString()
                             .trimmed();
    if (!host.isEmpty()) {
        config.serverHost = host;
    }

    bool validPort = false;
    const int port = settings.value(QStringLiteral("port"), config.serverPort)
                         .toInt(&validPort);
    if (validPort && port > 0 && port <= 65535) {
        config.serverPort = static_cast<quint16>(port);
    }
    settings.endGroup();
    return config;
}
