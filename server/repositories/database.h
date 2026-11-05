#pragma once

#include <QSqlDatabase>
#include <QString>

// Database：SQLite 连接与迁移工具（charge_repository 库的基础设施）。
// 三个静态方法覆盖：打开连接（含 PRAGMA）、整文件执行 SQL、按目录顺序执行迁移。
class Database
{
public:
    // 打开（或复用同名）SQLite 连接，并开启外键与 5 秒忙等待。
    static QSqlDatabase openSqlite(const QString &connectionName, const QString &path);
    // 在一个事务内执行整个 .sql 文件，任一语句失败则整体回滚。
    static bool executeSqlFile(QSqlDatabase &database, const QString &path,
                               QString *error = nullptr);
    // 按文件名顺序执行目录下未应用的迁移（schema_migration 记录已应用项）。
    static bool migrateDirectory(QSqlDatabase &database, const QString &directoryPath,
                                 QString *error = nullptr);
};
