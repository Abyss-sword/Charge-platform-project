// 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
// 分析导出契约测试：验证导出脚本输出的单位/格式/来源/批次字段符合 charge_event 契约——
// 成员3分析服务的输入有回归约束。

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

class AnalyticsExportContractTest final : public QObject
{
    Q_OBJECT

private slots:
    void definesRequiredFieldsWithoutPersonalIdentifiers()
    {
        QFile file(QStringLiteral(CHARGE_PLATFORM_SOURCE_DIR)
                   + QStringLiteral("/analytics/contracts/charge_event_v2.schema.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto document = QJsonDocument::fromJson(file.readAll());
        QVERIFY(document.isObject());
        const auto root = document.object();
        QCOMPARE(root.value("title").toString(), QStringLiteral("charge_event_v2"));

        const auto required = root.value("required").toArray();
        const QStringList expected{
            "source_record_id", "source_station_id", "station_id",
            "occurred_at", "ingested_at", "timezone", "energy_kwh",
            "duration_seconds", "amount_yuan", "status", "data_mode",
            "dataset_name", "dataset_version", "ingest_batch"};
        for (const QString &field : expected) {
            QVERIFY2(required.contains(field), qPrintable(field));
        }

        const auto properties = root.value("properties").toObject();
        QVERIFY(!properties.contains("phone"));
        QVERIFY(!properties.contains("name"));
        QVERIFY(!properties.contains("payment_account"));
        QVERIFY(!properties.contains("energy_wh"));
        QVERIFY(!properties.contains("amount_cents"));
        QCOMPARE(properties.value("energy_kwh").toObject().value("type").toString(),
                 QStringLiteral("string"));
        QCOMPARE(properties.value("energy_kwh").toObject().value("pattern").toString(),
                 QStringLiteral("^[0-9]+\\.[0-9]{2}$"));
        QCOMPARE(properties.value("amount_yuan").toObject().value("type").toString(),
                 QStringLiteral("string"));
        QCOMPARE(properties.value("amount_yuan").toObject().value("pattern").toString(),
                 QStringLiteral("^[0-9]+\\.[0-9]{2}$"));
        QCOMPARE(properties.value("data_mode").toObject().value("enum").toArray().first().toString(),
                 QStringLiteral("SIMULATED"));
    }
};

QTEST_MAIN(AnalyticsExportContractTest)
#include "analytics_export_contract_test.moc"
