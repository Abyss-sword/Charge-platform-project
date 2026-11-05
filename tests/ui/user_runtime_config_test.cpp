#include <QtTest>

#include "config/user_runtime_config.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

class UserRuntimeConfigTest final : public QObject
{
    Q_OBJECT
private slots:
    void missingFileUsesDefaults()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const UserRuntimeConfig config = loadUserRuntimeConfig(
            directory.filePath(QStringLiteral("missing.ini")));
        QCOMPARE(config.serverHost, QStringLiteral("127.0.0.1"));
        QCOMPARE(config.serverPort, quint16(48080));
    }

    void validConfigIsRead()
    {
        const UserRuntimeConfig config = load(QStringLiteral(
            "[server]\nhost=example.test\nport=12345\n"));
        QCOMPARE(config.serverHost, QStringLiteral("example.test"));
        QCOMPARE(config.serverPort, quint16(12345));
    }

    void emptyHostUsesDefault()
    {
        const UserRuntimeConfig config = load(QStringLiteral(
            "[server]\nhost=   \nport=12345\n"));
        QCOMPARE(config.serverHost, QStringLiteral("127.0.0.1"));
        QCOMPARE(config.serverPort, quint16(12345));
    }

    void invalidPortUsesDefault_data()
    {
        QTest::addColumn<QString>("port");
        QTest::newRow("zero") << QStringLiteral("0");
        QTest::newRow("too-large") << QStringLiteral("70000");
        QTest::newRow("not-number") << QStringLiteral("invalid");
    }

    void invalidPortUsesDefault()
    {
        QFETCH(QString, port);
        const UserRuntimeConfig config = load(
            QStringLiteral("[server]\nhost=example.test\nport=%1\n").arg(port));
        QCOMPARE(config.serverHost, QStringLiteral("example.test"));
        QCOMPARE(config.serverPort, quint16(48080));
    }

private:
    static UserRuntimeConfig load(const QString &contents)
    {
        QTemporaryDir directory;
        if (!directory.isValid()) {
            return {};
        }
        const QString path = directory.filePath(QStringLiteral("app.ini"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return {};
        }
        QTextStream(&file) << contents;
        file.close();
        return loadUserRuntimeConfig(path);
    }
};

QTEST_GUILESS_MAIN(UserRuntimeConfigTest)
#include "user_runtime_config_test.moc"
