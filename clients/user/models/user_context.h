#pragma once

#include <QString>
#include <QtGlobal>

struct UserContext
{
    qint64 userId = 0;
    QString nickname;
    bool restricted = false;
};
