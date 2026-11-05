#include "server/repositories/database.h"

#include <QFile>
#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

namespace {
// SQL 拆分器：按分号切分脚本，但尊重单/双引号字符串字面量（含 '' 转义），
// 避免把字符串里的分号误当语句边界。
QStringList splitStatements(const QString &script)
{
    QStringList statements;
    QString current;
    bool inSingleQuote = false;
    bool inDoubleQuote = false;

    for (qsizetype index = 0; index < script.size(); ++index) {
        const QChar character = script.at(index);
        if (character == QLatin1Char('\'') && !inDoubleQuote) {
            if (inSingleQuote && index + 1 < script.size()
                && script.at(index + 1) == QLatin1Char('\'')) {
                current.append(character);
                current.append(script.at(++index));
                continue;
            }
            inSingleQuote = !inSingleQuote;
        } else if (character == QLatin1Char('"') && !inSingleQuote) {
            inDoubleQuote = !inDoubleQuote;
        }

        if (character == QLatin1Char(';') && !inSingleQuote && !inDoubleQuote) {
            if (!current.trimmed().isEmpty()) {
                statements.append(current.trimmed());
            }
            current.clear();
        } else {
            current.append(character);
        }
    }
    if (!current.trimmed().isEmpty()) {
        statements.append(current.trimmed());
    }
    return statements;
}

bool executeStatements(QSqlDatabase &database, const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    const QString script = QString::fromUtf8(file.readAll());
    for (const QString &statement : splitStatements(script)) {
        QSqlQuery query(database);
        if (!query.exec(statement)) {
            if (error) {
                *error = query.lastError().text();
            }
            return false;
        }
    }
    return true;
}
}

// 打开 SQLite 连接（连接名唯一，允许同名复用），并执行两个关键 PRAGMA：
//   foreign_keys=ON：启用外键约束（SQLite 默认关闭！）；
//   busy_timeout=5000：写锁冲突时最多等 5 秒再报“database is locked”，
//   这是多连接线程并发下防止立即失败的关键设置。
QSqlDatabase Database::openSqlite(const QString &connectionName, const QString &path)
{
    QSqlDatabase database = QSqlDatabase::contains(connectionName)
        ? QSqlDatabase::database(connectionName)
        : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    database.setDatabaseName(path);
    if (database.open()) {
        QSqlQuery pragma(database);
        // S1-E 联动验收实测修复：默认回滚日志下，后台监控写事务会与登录/写请求互斥，
        // 偶发 SQLITE_BUSY 导致登录等操作返回 DATABASE_ERROR（约1/5复现）。
        // WAL 模式下读写不阻塞、单写者按 busy_timeout 排队，消除该故障面。
        pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
        pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
        pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
    }
    return database;
}

// 整文件执行：拆分语句后在单个事务内逐条执行，中途失败回滚整个文件，
// 保证建表/种子要么完整生效要么不留痕迹。
bool Database::executeSqlFile(QSqlDatabase &database, const QString &path, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    if (!database.isOpen()) {
        return fail(database.lastError().text().isEmpty()
                        ? QStringLiteral("database is not open")
                        : database.lastError().text());
    }

    if (!database.transaction()) {
        return fail(database.lastError().text());
    }
    QString statementError;
    if (!executeStatements(database, path, &statementError)) {
        database.rollback();
        return fail(statementError);
    }
    if (!database.commit()) {
        return fail(database.lastError().text());
    }
    if (error) {
        error->clear();
    }
    return true;
}

// 目录迁移执行器（答辩可讲“增量升级”）：
// 1) 建 schema_migration 跟踪表（文件名为主键 + 应用时间）；
// 2) 按文件名字典序（001、002…）逐个检查是否已应用，跳过已应用项——重复启动不会重复执行；
// 3) 每个迁移独立事务：SQL 执行 + 写入跟踪记录一起提交，失败则回滚并中止。
// 注意：迁移必须按编号顺序完整执行，不能只执行最新的 004/005。
bool Database::migrateDirectory(QSqlDatabase &database, const QString &directoryPath,
                                QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    if (!database.isOpen()) {
        return fail(QStringLiteral("database is not open"));
    }

    QSqlQuery tracker(database);
    if (!tracker.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS schema_migration("
            "name TEXT PRIMARY KEY, applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)"))) {
        return fail(tracker.lastError().text());
    }

    const QDir directory(directoryPath);
    const QStringList migrations = directory.entryList(
        {QStringLiteral("*.sql")}, QDir::Files, QDir::Name);
    if (migrations.isEmpty()) {
        return fail(QStringLiteral("no migrations found in %1").arg(directoryPath));
    }

    for (const QString &name : migrations) {
        QSqlQuery existing(database);
        existing.prepare(QStringLiteral("SELECT 1 FROM schema_migration WHERE name=?"));
        existing.addBindValue(name);
        if (!existing.exec()) {
            return fail(existing.lastError().text());
        }
        if (existing.next()) {
            continue;
        }

        if (!database.transaction()) {
            return fail(database.lastError().text());
        }
        QString migrationError;
        if (!executeStatements(database, directory.filePath(name), &migrationError)) {
            database.rollback();
            return fail(QStringLiteral("%1: %2").arg(name, migrationError));
        }
        QSqlQuery record(database);
        record.prepare(QStringLiteral("INSERT INTO schema_migration(name) VALUES(?)"));
        record.addBindValue(name);
        if (!record.exec() || !database.commit()) {
            const QString recordError = record.lastError().text();
            database.rollback();
            return fail(recordError.isEmpty() ? database.lastError().text() : recordError);
        }
    }
    if (error) {
        error->clear();
    }
    return true;
}
