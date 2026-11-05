#pragma once

#include <QString>
#include <QtGlobal>

struct UserRuntimeConfig
{
    QString serverHost = QStringLiteral("127.0.0.1");
    quint16 serverPort = 48080;
};

// An explicit path is useful for tests. With no path, common runtime locations are checked.
UserRuntimeConfig loadUserRuntimeConfig(const QString &filePath = {});
