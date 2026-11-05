#pragma once

#include <QHostAddress>
#include <QString>
#include <QThread>

class ServerBackend;

// TcpServer：面向 main() 的 TCP 服务对外门面（charge_server_network 库）。
// 三层结构：TcpServer（本类，公共接口）→ ServerBackend（后台对象， living 在独立监听线程）
//          → DispatchingTcpServer + ConnectionWorker（每个客户端连接一个线程一个 worker）。
// 线程模型要点：所有对 backend 的访问都通过 QMetaObject::invokeMethod 投递到监听线程串行执行，
// 每个连接持有自己独立的 SQLite 连接，避免跨线程共用数据库连接。
// 答辩口径：这是“每连接一个线程”模型，适合课程演示规模，不是线程池/百万连接架构。
class TcpServer final
{
public:
    explicit TcpServer(QString databasePath = {});
    ~TcpServer();

    TcpServer(const TcpServer &) = delete;
    TcpServer &operator=(const TcpServer &) = delete;

    // 在指定地址/端口监听（阻塞式转投监听线程执行，返回是否成功）。
    bool listen(const QHostAddress &address = QHostAddress::Any, quint16 port = 0);
    // 实际监听端口（port=0 时由系统分配，测试里常用它取回真实端口）。
    quint16 port() const;
    // 当前存活连接线程数（供健康检查/测试断言连接释放）。
    int connectionThreadCount() const;
    // 关闭监听并逐个停掉所有连接线程，回收资源。
    void close();

private:
    // 监听线程：backend 及其 QTcpServer/定时器都活在这条线程上，串行处理所有管理操作。
    QThread workerThread_;
    ServerBackend *backend_ = nullptr;
};
