// 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
// Schema 集成测试：验证迁移执行、约束生效、种子数据，以及重建库后服务能正常使用——
// 数据库层正确性的证据。

#include <QFile>
#include <QMap>
#include <QProcess>
#include <QTemporaryDir>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QtTest>

#include "server/repositories/database.h"

class SchemaTest final : public QObject
{
    Q_OBJECT

private:
    QSqlDatabase openMigrated(const QString &name)
    {
        QSqlDatabase db = Database::openSqlite(name, QStringLiteral(":memory:"));
        QString error;
        const QString migration = QStringLiteral(CHARGE_PLATFORM_SOURCE_DIR)
            + QStringLiteral("/database/migrations");
        if (!Database::migrateDirectory(db, migration, &error)) {
            qWarning().noquote() << "migration failed:" << error;
            db.close();
        }
        return db;
    }

private slots:
    void resetDemoDatabaseSupportsServerMigration()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString root = QStringLiteral(CHARGE_PLATFORM_SOURCE_DIR);
        const QString path = directory.filePath(QStringLiteral("demo.sqlite"));
        QProcess reset;
        reset.start(QStringLiteral("bash"),
                    {root + QStringLiteral("/scripts/reset-demo-database.sh"), path});
        QVERIFY(reset.waitForFinished(10000));
        QCOMPARE(reset.exitStatus(), QProcess::NormalExit);
        QCOMPARE(reset.exitCode(), 0);
        const QString connection = QStringLiteral("reset_demo_bootstrap");
        {
            auto db = Database::openSqlite(connection, path);
            QVERIFY(db.isOpen());
            QString error;
            for (int startup = 0; startup < 2; ++startup) {
                QVERIFY2(Database::migrateDirectory(
                    db, root + QStringLiteral("/database/migrations"), &error),
                    qPrintable(error));
            }
            QSqlQuery query(db);
            QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM schema_migration")));
            QVERIFY(query.next());
            // 010_linkage_meta：S1-A 联动契约迁移加入后共 10 个迁移文件。
            QCOMPARE(query.value(0).toInt(), 10);
            QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM charge_order")));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 30);
        }
        QSqlDatabase::removeDatabase(connection);
    }

    void createsElevenCoreTables()
    {
        auto db = openMigrated(QStringLiteral("schema_tables"));
        QVERIFY(db.isOpen());
        const QStringList expected{
            "admin_user", "user_info", "station_info", "pile_info",
            "charge_order", "wallet_transaction", "pile_telemetry",
            "alarm_event", "maintenance_ticket", "operation_log",
            "request_dedup"};
        for (const QString &table : expected) {
            QVERIFY2(db.tables().contains(table), qPrintable(table));
        }
    }

    void rejectsDuplicatePhoneAndPileSn()
    {
        auto db = openMigrated(QStringLiteral("schema_unique"));
        QVERIFY(db.isOpen());
        QSqlQuery query(db);
        QVERIFY(query.exec("INSERT INTO user_info(phone,nickname,balance_yuan,status) "
                           "VALUES('13800138000','用户8000','100.00','NORMAL')"));
        QVERIFY(!query.exec("INSERT INTO user_info(phone,nickname,balance_yuan,status) "
                            "VALUES('13800138000','重复','100.00','NORMAL')"));

        QVERIFY(query.exec("INSERT INTO station_info(id,name,address,latitude,longitude,status) "
                           "VALUES(1,'测试站','测试路1号',23.1,113.2,'OPEN')"));
        QVERIFY(query.exec("INSERT INTO pile_info(station_id,pile_sn,charge_type,rated_power_kw,status) "
                           "VALUES(1,'PILE-T1','FAST',60,'IDLE')"));
        QVERIFY(!query.exec("INSERT INTO pile_info(station_id,pile_sn,charge_type,rated_power_kw,status) "
                            "VALUES(1,'PILE-T1','FAST',60,'IDLE')"));
    }

    void usesCanonicalDecimalColumns()
    {
        auto db = openMigrated(QStringLiteral("schema_decimal_columns"));
        QVERIFY(db.isOpen());

        const QMap<QString, QStringList> expected{
            {QStringLiteral("user_info"), {QStringLiteral("balance_yuan")}},
            {QStringLiteral("station_info"), {QStringLiteral("price_yuan_per_kwh")}},
            {QStringLiteral("charge_order"), {
                QStringLiteral("energy_kwh"), QStringLiteral("amount_yuan"),
                QStringLiteral("unit_price_yuan_per_kwh")}},
            {QStringLiteral("wallet_transaction"), {QStringLiteral("amount_yuan")}},
            {QStringLiteral("pile_telemetry"), {QStringLiteral("energy_kwh")}}
        };
        const QSet<QString> removed{
            QStringLiteral("balance_cents"), QStringLiteral("price_cents_per_kwh"),
            QStringLiteral("energy_wh"), QStringLiteral("amount_cents"),
            QStringLiteral("unit_price_cents_per_kwh")
        };

        for (auto table = expected.cbegin(); table != expected.cend(); ++table) {
            QSqlQuery query(db);
            QVERIFY(query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table.key())));
            QMap<QString, QString> columns;
            while (query.next()) {
                columns.insert(query.value(1).toString(), query.value(2).toString().toUpper());
            }
            for (const QString &name : table.value()) {
                QVERIFY2(columns.contains(name), qPrintable(table.key() + QLatin1Char('.') + name));
                QCOMPARE(columns.value(name), QStringLiteral("TEXT"));
            }
            for (const QString &name : removed) {
                QVERIFY2(!columns.contains(name), qPrintable(table.key() + QLatin1Char('.') + name));
            }
        }
    }

    void rejectsInvalidOrderStatusAndBrokenForeignKey()
    {
        auto db = openMigrated(QStringLiteral("schema_checks"));
        QVERIFY(db.isOpen());
        QSqlQuery query(db);
        QVERIFY(!query.exec("INSERT INTO pile_info(station_id,pile_sn,charge_type,rated_power_kw,status) "
                            "VALUES(999,'PILE-X','FAST',60,'IDLE')"));
        QVERIFY(!query.exec("INSERT INTO charge_order(id,user_id,pile_id,status) "
                            "VALUES('ORDER-X',999,999,'UNKNOWN')"));
    }

    void addsTelemetryAlarmFoundationConstraints()
    {
        auto db = openMigrated(QStringLiteral("schema_telemetry_alarm"));
        QVERIFY(db.isOpen());
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral("PRAGMA table_info(maintenance_ticket)")));
        QSet<QString> columns;
        while (query.next()) {
            columns.insert(query.value(1).toString());
        }
        QVERIFY(columns.contains(QStringLiteral("resolved_at")));
        QVERIFY(columns.contains(QStringLiteral("closed_at")));

        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO station_info(id,name,address,latitude,longitude,status) "
            "VALUES(1,'测试站','测试路',23.1,113.2,'OPEN')")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO pile_info(id,station_id,pile_sn,charge_type,rated_power_kw,status) "
            "VALUES(1,1,'PILE-ALARM-UNIQUE','FAST',60,'IDLE')")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO alarm_event(pile_id,severity,alarm_type,content,status,occurred_at) "
            "VALUES(1,'WARNING','OFFLINE','首次','OPEN','2026-09-06T10:00:00Z')")));
        QVERIFY(!query.exec(QStringLiteral(
            "INSERT INTO alarm_event(pile_id,severity,alarm_type,content,status,occurred_at) "
            "VALUES(1,'WARNING','OFFLINE','重复','OPEN','2026-09-06T10:00:01Z')")));
    }

    void loadsIdempotentDemoSeed()
    {
        auto db = openMigrated(QStringLiteral("schema_seed"));
        QVERIFY(db.isOpen());
        QString error;
        const QString seed = QStringLiteral(CHARGE_PLATFORM_SOURCE_DIR)
            + QStringLiteral("/database/seeds/demo.sql");
        QVERIFY2(Database::executeSqlFile(db, seed, &error), qPrintable(error));
        QVERIFY2(Database::executeSqlFile(db, seed, &error), qPrintable(error));

        QSqlQuery query(db);
        QVERIFY(query.exec("SELECT COUNT(*) FROM station_info"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 3);
        QVERIFY(query.exec("SELECT COUNT(*) FROM pile_info"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 12);
        QVERIFY(query.exec("SELECT COUNT(*) FROM user_info"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 20);
        QVERIFY(query.exec("SELECT COUNT(*) FROM charge_order"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 30);
        QVERIFY(query.exec("SELECT COUNT(*) FROM alarm_event"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 3);
        QVERIFY(query.exec("SELECT COUNT(*) FROM maintenance_ticket"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 3);
        QVERIFY(query.exec(
            "SELECT COUNT(*) FROM alarm_event a "
            "JOIN maintenance_ticket t ON t.alarm_id=a.id"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 3);
    }

    void upgradesLegacyDatabaseWithoutDeletingData()
    {
        const QString name = QStringLiteral("schema_upgrade");
        QSqlDatabase db = Database::openSqlite(name, QStringLiteral(":memory:"));
        QVERIFY(db.isOpen());
        QString error;
        const QString root = QStringLiteral(CHARGE_PLATFORM_SOURCE_DIR);
        QVERIFY2(Database::executeSqlFile(
            db, root + QStringLiteral("/database/migrations/001_initial.sql"), &error),
            qPrintable(error));
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO user_info(id,phone,nickname,balance_cents) "
            "VALUES(1,'13800138888','旧用户',5000)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO wallet_transaction(user_id,transaction_type,amount_cents,request_id) "
            "VALUES(1,'RECHARGE',5000,'LEGACY-R-1')")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO station_info(id,name,address,latitude,longitude,price_cents_per_kwh) "
            "VALUES(1,'旧站点','旧地址',23.1,113.2,123)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO pile_info(id,station_id,pile_sn,charge_type,rated_power_kw) "
            "VALUES(1,1,'LEGACY-PILE','FAST',60)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO charge_order(id,user_id,pile_id,status,energy_wh,amount_cents) "
            "VALUES('LEGACY-ORDER',1,1,'FINISHED',8345,1027)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO alarm_event(id,pile_id,severity,alarm_type,content,status,occurred_at) "
            "VALUES(1,1,'WARNING','LEGACY_OFFLINE','较早旧告警','ACKNOWLEDGED',"
            "'2026-09-01T08:00:00Z'),"
            "(2,1,'WARNING','LEGACY_OFFLINE','较新旧告警','OPEN',"
            "'2026-09-01T08:01:00Z')")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO maintenance_ticket(alarm_id,status) VALUES(1,'PROCESSING')")));

        QVERIFY2(Database::migrateDirectory(
            db, root + QStringLiteral("/database/migrations"), &error), qPrintable(error));
        QVERIFY(query.exec(QStringLiteral(
            "SELECT balance_yuan FROM user_info WHERE id=1")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("50.00"));
        QVERIFY(query.exec(QStringLiteral(
            "SELECT price_yuan_per_kwh FROM station_info WHERE id=1")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("1.23"));
        QVERIFY(query.exec(QStringLiteral(
            "SELECT energy_kwh,amount_yuan,unit_price_yuan_per_kwh,price_locked "
            "FROM charge_order WHERE id='LEGACY-ORDER'")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("8.35"));
        QCOMPARE(query.value(1).toString(), QStringLiteral("10.27"));
        QCOMPARE(query.value(2).toString(), QStringLiteral("0.00"));
        QCOMPARE(query.value(3).toInt(), 0);
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO wallet_transaction(user_id,transaction_type,amount_yuan,request_id) "
            "VALUES(1,'INITIAL_CREDIT','100.00','LEGACY-I-1')")));
        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM wallet_transaction")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 2);
        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM alarm_event")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 2);
        QVERIFY(query.exec(QStringLiteral(
            "SELECT COUNT(*) FROM alarm_event WHERE pile_id=1 AND alarm_type='LEGACY_OFFLINE' "
            "AND status IN ('OPEN','ACKNOWLEDGED')")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 1);
        QVERIFY(query.exec(QStringLiteral(
            "SELECT COUNT(*) FROM maintenance_ticket WHERE alarm_id IN (1,2)")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 2);
        QVERIFY(query.exec(QStringLiteral(
            "SELECT status FROM maintenance_ticket WHERE alarm_id=1")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("CLOSED"));
        QVERIFY2(Database::migrateDirectory(
            db, root + QStringLiteral("/database/migrations"), &error), qPrintable(error));
    }
};

QTEST_MAIN(SchemaTest)
#include "schema_test.moc"
