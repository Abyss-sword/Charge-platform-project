#include "server/network/tcp_server.h"
#include "server/repositories/database.h"
#include "server/services/admin_service.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

// 业务服务端入口（无图形界面）：解析命令行 → 建目录 → 执行迁移 → 初始化演示管理员 →
// 监听进入事件循环。Qt 车主端/运营端、桩模拟器都通过 TCP+JSON 连到本服务。
//
// 参数（M1-01，默认保持旧行为）：
//   --host <addr>  监听地址（默认 0.0.0.0；127.0.0.1 用于隔离测试）
//   --port <n>     监听端口（默认 48080；0=系统分配，就绪后输出实际端口）
//   --db <path>    SQLite 数据库路径（默认取 CHARGE_PLATFORM_DB，否则
//                  <cwd>/runtime/charge_platform.sqlite）
// 非法端口、空路径、监听失败一律返回非 0。
int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Charge platform business server (TCP + JSON)"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption hostOption(
        {QStringLiteral("host")},
        QStringLiteral("listen address, default 0.0.0.0"), QStringLiteral("address"));
    const QCommandLineOption portOption(
        {QStringLiteral("p"), QStringLiteral("port")},
        QStringLiteral("listen port, 0 = system assigned, default 48080"),
        QStringLiteral("port"), QStringLiteral("48080"));
    const QCommandLineOption dbOption(
        QStringLiteral("db"),
        QStringLiteral("SQLite database file path (default: CHARGE_PLATFORM_DB or "
                       "runtime/charge_platform.sqlite under working dir)"),
        QStringLiteral("path"));
    parser.addOption(hostOption);
    parser.addOption(portOption);
    parser.addOption(dbOption);
    parser.process(application);

    bool portOk = false;
    const quint16 listenPort = parser.value(portOption).toUShort(&portOk);
    if (!portOk || (parser.isSet(portOption)
                    && parser.value(portOption) != QStringLiteral("0")
                    && parser.value(portOption).toInt() == 0)) {
        qCritical() << "Invalid --port value:" << parser.value(portOption);
        return 1;
    }

    // 数据库路径优先级：--db > CHARGE_PLATFORM_DB > 默认 runtime 下文件。
    // 显式 --db 但值为空（--db ""）按错误处理，不回退默认库。
    QString databasePath = parser.value(dbOption);
    if (parser.isSet(dbOption) && databasePath.isEmpty()) {
        qCritical() << "Empty database path";
        return 1;
    }
    if (databasePath.isEmpty()) {
        const QString defaultPath = QDir::current().filePath(
            QStringLiteral("runtime/charge_platform.sqlite"));
        databasePath = qEnvironmentVariable("CHARGE_PLATFORM_DB", defaultPath);
    }
    // 环境变量被显式置空时同样拒绝，避免落到不可预期的默认库。
    if (databasePath.isEmpty()) {
        qCritical() << "Empty database path";
        return 1;
    }
    // 数据库所在目录不存在则自动创建。
    if (!QDir().mkpath(QFileInfo(databasePath).absolutePath())) {
        qCritical() << "Unable to create database directory";
        return 1;
    }
    // 启动引导块：打开一次性引导连接，按编号顺序执行 database/migrations 下全部迁移
    // （schema_migration 记录已执行的迁移，重复启动不会重复执行），再确保默认管理员存在。
    {
        QSqlDatabase database = Database::openSqlite(QStringLiteral("server_bootstrap"),
                                                     databasePath);
        QString migrationError;
        if (!Database::migrateDirectory(
                database, QStringLiteral(CHARGE_PLATFORM_SOURCE_DIR "/database/migrations"),
                &migrationError)) {
            qCritical() << "Unable to migrate database:" << migrationError;
            return 1;
        }
        AdminService admin(database);
        if (!admin.ensureDefaultAdmin().ok) {
            qCritical() << "Unable to initialize default administrator";
            return 1;
        }
        database.close();
    }
    // QSqlDatabase 连接必须先 close 再 removeDatabase，防止 “database connection still in use” 警告。
    QSqlDatabase::removeDatabase(QStringLiteral("server_bootstrap"));

    // 启动 TCP 服务。--port 0 时由系统分配，listen 成功后输出实际地址/端口，
    // 供测试与启动器解析；输出不包含任何凭据。
    const QHostAddress listenAddress = parser.isSet(hostOption)
        ? QHostAddress(parser.value(hostOption))
        : QHostAddress(QHostAddress::Any);
    TcpServer server(databasePath);
    if (!server.listen(listenAddress, listenPort)) {
        qCritical() << "Unable to listen on port" << listenPort;
        return 1;
    }
    // 就绪信息：显式 flush，保证经管道读取的测试/启动器能立刻收到，
    // 不受 Qt 流缓冲影响；输出不包含任何凭据。
    QTextStream(stdout) << "Charge platform server listening on "
                        << parser.value(hostOption) << ":" << server.port()
                        << Qt::endl;
    qInfo() << "Charge platform server listening on port" << server.port();
    return application.exec();
}
