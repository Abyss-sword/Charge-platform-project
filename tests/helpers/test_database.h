// 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
// 测试基础设施：TestDatabase 为每个用例创建临时目录中的独立 SQLite（自动执行迁移），
// 保证用例互不污染、数据可控可复现。

#pragma once

#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QVariant>

#include "server/repositories/database.h"

class TestDatabase final
{
public:
    TestDatabase()
        : connectionName_(uniqueConnectionName(QStringLiteral("test_main")))
        , path_(directory_.filePath(QStringLiteral("test.sqlite")))
        , database_(Database::openSqlite(connectionName_, path_))
    {
        QString migrationError;
        const QString migration = QStringLiteral(CHARGE_PLATFORM_SOURCE_DIR)
            + QStringLiteral("/database/migrations");
        ready_ = directory_.isValid() && database_.isOpen()
            && Database::migrateDirectory(database_, migration, &migrationError);
        error_ = ready_ ? QString{} : migrationError;
        if (ready_) {
            QSqlQuery pragma(database_);
            ready_ = pragma.exec(QStringLiteral("PRAGMA busy_timeout=3000"));
            if (!ready_) {
                error_ = pragma.lastError().text();
            }
        }
    }

    ~TestDatabase()
    {
        database_.close();
        database_ = {};
        QSqlDatabase::removeDatabase(connectionName_);
    }

    TestDatabase(const TestDatabase &) = delete;
    TestDatabase &operator=(const TestDatabase &) = delete;

    bool isReady() const { return ready_; }
    QString error() const { return error_; }
    QString path() const { return path_; }
    QSqlDatabase database() const { return database_; }

    QSqlDatabase openConnection(const QString &prefix) const
    {
        auto db = Database::openSqlite(uniqueConnectionName(prefix), path_);
        QSqlQuery pragma(db);
        pragma.exec(QStringLiteral("PRAGMA busy_timeout=3000"));
        return db;
    }

    bool exec(const QString &sql, const QVariantList &values = {})
    {
        QSqlQuery query(database_);
        if (!query.prepare(sql)) {
            error_ = query.lastError().text();
            return false;
        }
        for (const auto &value : values) {
            query.addBindValue(value);
        }
        if (!query.exec()) {
            error_ = query.lastError().text();
            return false;
        }
        return true;
    }

    QVariant scalar(const QString &sql, const QVariantList &values = {})
    {
        QSqlQuery query(database_);
        if (!query.prepare(sql)) {
            error_ = query.lastError().text();
            return {};
        }
        for (const auto &value : values) {
            query.addBindValue(value);
        }
        if (!query.exec() || !query.next()) {
            error_ = query.lastError().text();
            return {};
        }
        return query.value(0);
    }

    qint64 addUser(const QString &phone,
                   const QString &balanceYuan = QStringLiteral("100.00"),
                   const QString &status = QStringLiteral("NORMAL"))
    {
        if (!exec(QStringLiteral(
                "INSERT INTO user_info(phone,nickname,balance_yuan,status) VALUES(?,?,?,?)"),
                {phone, QStringLiteral("测试用户"), balanceYuan, status})) {
            return -1;
        }
        return scalar(QStringLiteral("SELECT last_insert_rowid()" )).toLongLong();
    }

    qint64 addStation(const QString &name = QStringLiteral("测试站"),
                      double latitude = 23.1291, double longitude = 113.2644,
                      const QString &priceYuanPerKwh = QStringLiteral("1.20"))
    {
        if (!exec(QStringLiteral(
                "INSERT INTO station_info(name,address,latitude,longitude,price_yuan_per_kwh,status) "
                "VALUES(?,?,?,?,?,'OPEN')"),
                {name, QStringLiteral("测试路1号"), latitude, longitude, priceYuanPerKwh})) {
            return -1;
        }
        return scalar(QStringLiteral("SELECT last_insert_rowid()" )).toLongLong();
    }

    qint64 addPile(qint64 stationId, const QString &serial,
                   const QString &status = QStringLiteral("IDLE"))
    {
        if (!exec(QStringLiteral(
                "INSERT INTO pile_info(station_id,pile_sn,charge_type,rated_power_kw,status) "
                "VALUES(?,?,'FAST',60,?)"), {stationId, serial, status})) {
            return -1;
        }
        return scalar(QStringLiteral("SELECT last_insert_rowid()" )).toLongLong();
    }

private:
    static QString uniqueConnectionName(const QString &prefix)
    {
        return prefix + QStringLiteral("_")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    QTemporaryDir directory_;
    QString connectionName_;
    QString path_;
    QSqlDatabase database_;
    bool ready_ = false;
    QString error_;
};
