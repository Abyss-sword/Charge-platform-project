#include "server/services/linkage.h"

#include <QDateTime>
#include <QSqlQuery>

namespace linkage {

qint64 currentVersion(const QSqlDatabase &database)
{
    // 读全局写版本；正常路径由迁移 010 保证表与初值存在。
    QSqlQuery query(database);
    if (query.exec(QStringLiteral(
            "SELECT value FROM linkage_meta WHERE key='changeVersion'"))
        && query.next()) {
        bool ok = false;
        const qint64 version = query.value(0).toLongLong(&ok);
        if (ok && version > 0) {
            return version;
        }
    }
    return 1;
}

qint64 bumpVersion(const QSqlDatabase &database)
{
    // 必须在调用方写事务内执行：UPDATE 与 SELECT 同事务，版本与业务写入同生共死。
    QSqlQuery update(database);
    update.prepare(QStringLiteral(
        "UPDATE linkage_meta SET value = CAST(CAST(value AS INTEGER) + 1 AS TEXT) "
        "WHERE key='changeVersion'"));
    if (!update.exec()) {
        return -1;
    }
    QSqlQuery select(database);
    if (!select.exec(QStringLiteral(
            "SELECT value FROM linkage_meta WHERE key='changeVersion'"))
        || !select.next()) {
        return -1;
    }
    bool ok = false;
    const qint64 version = select.value(0).toLongLong(&ok);
    if (!ok || version <= 0) {
        return -1;
    }
    return version;
}

QString serverTimeUtc()
{
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ssZ"));
}

}  // namespace linkage
