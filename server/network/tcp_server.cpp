#include "server/network/tcp_server.h"

#include "server/network/request_router.h"
#include "server/repositories/database.h"
#include "server/services/order_service.h"
#include "server/services/telemetry_service.h"
#include "shared/protocol/frame_codec.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QMetaObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <QHash>
#include <QUuid>

#include <functional>
#include <utility>

// ============================================================================
// TCP 网络层实现（成员1 · 第一阶段）。
//
// 职责：监听端口 → 接受连接 → 每连接一个 QThread + ConnectionWorker；
// worker 内：FrameCodec 拆帧 → Message::fromJson 校验 → RequestRouter 分发业务 →
// 统一 Response 回写。并发请求由各连接线程 + SQLite BEGIN IMMEDIATE/条件更新保证一致性。
//
// 线程模型：监听线程持有 ServerBackend（含 5 秒后台定时器）；连接线程各自持有
// 独立 SQLite 连接（连接名 tcp_<uuid>），退出时归位监听线程销毁。
// ============================================================================

namespace {
// QTcpServer 子类：仅把“新连接的 socket 描述符”转交给外部回调，
// 使连接的创建策略（开线程）与监听本身解耦。
class DispatchingTcpServer final : public QTcpServer
{
public:
    using ConnectionHandler = std::function<void(qintptr)>;

    explicit DispatchingTcpServer(ConnectionHandler handler, QObject *parent = nullptr)
        : QTcpServer(parent)
        , handler_(std::move(handler))
    {
    }

protected:
    // 每当有新连接到达（在监听线程回调），把描述符交给 handler 去开连接线程。
    void incomingConnection(qintptr socketDescriptor) override
    {
        handler_(socketDescriptor);
    }

private:
    ConnectionHandler handler_;
};

// ConnectionWorker：单个客户端连接的专属工作对象（QObject，moveToThread 到连接线程）。
// 每个连接拥有：独立 SQLite 连接、QTcpSocket、FrameCodec、RequestRouter——
// 互不共享，天然避免跨线程共用 QSqlDatabase 连接的问题。
class ConnectionWorker final : public QObject
{
public:
    ConnectionWorker(qintptr socketDescriptor, QString databasePath, QThread *listenerThread,
                     std::function<void(QThread *)> finished)
        : socketDescriptor_(socketDescriptor)
        , databasePath_(std::move(databasePath))
        , listenerThread_(listenerThread)
        , finished_(std::move(finished))
    {
    }

    // 连接线程启动后的初始化（通过 QThread::started 信号触发，运行在连接线程内）：
    // 1) 以 "tcp_<uuid>" 命名建立本连接私有的 SQLite 连接（每连接独立连接是并发正确性的前提）；
    // 2) 创建 socket 并接入事件循环。
    void start()
    {
        if (!databasePath_.isEmpty()) {
            databaseConnectionName_ = QStringLiteral("tcp_")
                + QUuid::createUuid().toString(QUuid::WithoutBraces);
            database_ = Database::openSqlite(databaseConnectionName_, databasePath_);
        }
        socket_ = new QTcpSocket(this);
        connect(socket_, &QTcpSocket::readyRead, this, [this] { readAvailableFrames(); });
        connect(socket_, &QTcpSocket::disconnected, this, [this] { finish(); });
        if (!socket_->setSocketDescriptor(socketDescriptor_)) {
            finish();
        }
    }

    // 服务端主动断开：中止 socket 并走统一收尾。
    void stop()
    {
        if (socket_) {
            socket_->abort();
        }
        finish();
    }

private:
    // readyRead 处理：字节流喂给 FrameCodec 拆帧，逐帧交给 handleFrame；
    // 拆包发现协议错误则回协议错误响应并断开连接。
    void readAvailableFrames()
    {
        const QList<QByteArray> frames = codec_.feed(socket_->readAll());
        for (const QByteArray &frame : frames) {
            handleFrame(frame);
        }
        if (codec_.hasProtocolError()) {
            send(Response::failure({}, ErrorCode::ProtocolError, codec_.lastError()));
            socket_->disconnectFromHost();
        }
    }

    // 单帧处理三步：JSON 解析 → Message::fromJson 封装校验（版本/requestId/action/payload）→
    // 构造携带本连接数据库的 RequestContext 并交给 RequestRouter 分发业务。
    void handleFrame(const QByteArray &frame)
    {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(frame, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            send(Response::failure({}, ErrorCode::ProtocolError,
                                   QStringLiteral("invalid JSON object")));
            return;
        }

        const QJsonObject object = document.object();
        QString validationError;
        const auto message = Message::fromJson(object, &validationError);
        if (!message) {
            send(Response::failure(object.value(QStringLiteral("requestId")).toString(),
                                   ErrorCode::ParamInvalid, validationError));
            return;
        }

        RequestContext context{database_};
        send(router_.route(*message, context));
    }

    // 回写响应：Response → JSON → FrameCodec::encode 加长度头 → 写 socket 并立即冲刷。
    void send(const Response &response)
    {
        const QByteArray json = QJsonDocument(response.toJson()).toJson(QJsonDocument::Compact);
        socket_->write(FrameCodec::encode(json));
        socket_->flush();
    }

    // 连接收尾（断开或出错时触发，只执行一次）：关闭并注销本连接的数据库连接；
    // 把 worker 移回监听线程，由监听线程统一 wait/删除线程（见 existing 注释）。
    void finish()
    {
        if (finishing_) {
            return;
        }
        finishing_ = true;
        if (database_.isValid()) {
            database_.close();
            database_ = {};
            QSqlDatabase::removeDatabase(databaseConnectionName_);
        }
        QThread *connectionThread = QThread::currentThread();

        // Moving the worker (and its socket child) back lets the listener own
        // final destruction after the connection event loop has stopped.
        moveToThread(listenerThread_);
        finished_(connectionThread);
        connectionThread->quit();
    }

    qintptr socketDescriptor_;
    QString databasePath_;
    QString databaseConnectionName_;
    QSqlDatabase database_;
    QThread *listenerThread_;
    std::function<void(QThread *)> finished_;
    QTcpSocket *socket_ = nullptr;
    FrameCodec codec_;
    RequestRouter router_;
    bool finishing_ = false;
};
}

// ServerBackend：真正的服务后端，固定运行在监听线程。
// 持有：监听器、连接表（线程→worker）、5 秒后台定时器及其专属监控数据库连接。
class ServerBackend final : public QObject
{
public:
    explicit ServerBackend(QString databasePath)
        : databasePath_(std::move(databasePath))
    {
    }

    // 启动监听：注册“新连接 → startConnection”回调，并启动 5 秒后台离线/过期监控。
    bool listen(const QHostAddress &address, quint16 port)
    {
        if (!server_) {
            server_ = new DispatchingTcpServer(
                [this](qintptr socketDescriptor) { startConnection(socketDescriptor); }, this);
        }
        if (server_->isListening()) {
            server_->close();
        }
        if (!server_->listen(address, port)) {
            return false;
        }
        startOfflineMonitor();
        return true;
    }

    quint16 port() const
    {
        return server_ && server_->isListening() ? server_->serverPort() : 0;
    }

    // 优雅停机：停定时器 → 关监听 → 逐个通知连接线程停止 → wait 后删除 worker/线程 →
    // 关闭监控数据库连接。全部操作只在监听线程执行，无需额外加锁。
    void close()
    {
        if (offlineTimer_) {
            offlineTimer_->stop();
        }
        if (server_) {
            server_->close();
        }

        const auto threads = connections_.keys();
        for (QThread *thread : threads) {
            ConnectionWorker *worker = connections_.value(thread);
            QMetaObject::invokeMethod(worker, [worker] { worker->stop(); },
                                      Qt::QueuedConnection);
        }
        for (QThread *thread : threads) {
            thread->wait();
            ConnectionWorker *worker = connections_.take(thread);
            delete worker;
            delete thread;
        }
        if (monitorDatabase_.isValid()) {
            monitorDatabase_.close();
            monitorDatabase_ = {};
            QSqlDatabase::removeDatabase(monitorConnectionName_);
            monitorConnectionName_.clear();
        }
    }

    int connectionThreadCount() const
    {
        return connections_.size();
    }

private:
    // 后台定时扫描（实际生产调度入口）：
    // 每 5 秒执行一次 ① 心跳离线扫描（TelemetryService::scanOffline）
    //              ② 过期预约回收（OrderService::expireReservations）。
    // 使用独立连接名 offline_monitor_<uuid> 的数据库连接，避免与连接线程竞争同一连接对象。
    // 答辩口径：某次扫描可能受数据库忙影响而延后，不能承诺绝对毫秒级执行；
    // ReservationExpiryScheduler 的默认 10 秒不是当前生产入口的实际间隔。
    void startOfflineMonitor()
    {
        if (databasePath_.isEmpty()) {
            return;
        }
        if (!monitorDatabase_.isOpen()) {
            monitorConnectionName_ = QStringLiteral("offline_monitor_")
                + QUuid::createUuid().toString(QUuid::WithoutBraces);
            monitorDatabase_ = Database::openSqlite(monitorConnectionName_, databasePath_);
        }
        if (!offlineTimer_) {
            offlineTimer_ = new QTimer(this);
            offlineTimer_->setInterval(5000);
            connect(offlineTimer_, &QTimer::timeout, this, [this] {
                if (monitorDatabase_.isOpen()) {
                    TelemetryService(monitorDatabase_).scanOffline();
                    OrderService(monitorDatabase_).expireReservations();
                }
            });
        }
        offlineTimer_->start();
    }

    // 新连接接入：创建专属 QThread + ConnectionWorker（moveToThread），
    // 注册“线程启动即初始化 worker”，线程退出时通过 finished 回调队列回监听线程清理。
    void startConnection(qintptr socketDescriptor)
    {
        auto *thread = new QThread;
        auto *worker = new ConnectionWorker(
            socketDescriptor, databasePath_, QThread::currentThread(),
            [this](QThread *finishedThread) {
                QMetaObject::invokeMethod(
                    this,
                    [this, finishedThread] { connectionFinished(finishedThread); },
                    Qt::QueuedConnection);
            });
        worker->moveToThread(thread);
        connections_.insert(thread, worker);
        connect(thread, &QThread::started, worker, [worker] { worker->start(); });
        thread->start();
    }

    // 连接线程已退出：wait 后回收 worker 与线程对象，并从连接表移除。
    void connectionFinished(QThread *thread)
    {
        const auto entry = connections_.find(thread);
        if (entry == connections_.end()) {
            return;
        }
        thread->wait();
        ConnectionWorker *worker = entry.value();
        connections_.erase(entry);
        delete worker;
        delete thread;
    }

    DispatchingTcpServer *server_ = nullptr;
    QString databasePath_;
    QHash<QThread *, ConnectionWorker *> connections_;
    QString monitorConnectionName_;
    QSqlDatabase monitorDatabase_;
    QTimer *offlineTimer_ = nullptr;
};

// 对外门面：backend 整体搬到监听线程；线程结束时 deleteLater 回收 backend。
TcpServer::TcpServer(QString databasePath)
    : backend_(new ServerBackend(std::move(databasePath)))
{
    backend_->moveToThread(&workerThread_);
    QObject::connect(&workerThread_, &QThread::finished, backend_, &QObject::deleteLater);
    workerThread_.start();
}

TcpServer::~TcpServer()
{
    close();
    workerThread_.quit();
    workerThread_.wait();
}

// 以下公共方法统一用 BlockingQueuedConnection 把调用串行投递到监听线程：
// 保证对 backend 的访问单线程化（线程安全），调用方拿到返回值后继续。
bool TcpServer::listen(const QHostAddress &address, quint16 requestedPort)
{
    bool result = false;
    QMetaObject::invokeMethod(backend_, [&] {
        result = backend_->listen(address, requestedPort);
    }, Qt::BlockingQueuedConnection);
    return result;
}

quint16 TcpServer::port() const
{
    quint16 result = 0;
    QMetaObject::invokeMethod(backend_, [&] {
        result = backend_->port();
    }, Qt::BlockingQueuedConnection);
    return result;
}

int TcpServer::connectionThreadCount() const
{
    if (!workerThread_.isRunning()) {
        return 0;
    }
    int result = 0;
    QMetaObject::invokeMethod(backend_, [&] {
        result = backend_->connectionThreadCount();
    }, Qt::BlockingQueuedConnection);
    return result;
}

void TcpServer::close()
{
    if (!workerThread_.isRunning()) {
        return;
    }
    // 阻塞等待监听线程完成停机（见上方 BlockingQueuedConnection 说明）。
    QMetaObject::invokeMethod(backend_, [this] { backend_->close(); },
                              Qt::BlockingQueuedConnection);
}
