#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

// FrameCodec：TCP 应用层帧编解码器（成员1 · 第一阶段 · 公共协议库 charge_protocol）。
// 为什么 TCP 还要长度头：TCP 只保证有序字节流，不保证“一次发送对应一次接收”。
// 帧格式约定：4 字节大端长度头 + JSON 正文；正文长度非法（0 或超上限）即判定协议错误。
class FrameCodec
{
public:
    // 单帧正文上限 1MiB：超出立即置协议错误，防止异常超长帧拖垮内存。
    static constexpr quint32 MaxPayload = 1024U * 1024U;

    // 发送端：为 JSON 正文加 4 字节大端长度头，返回可直接写入 socket 的完整帧。
    static QByteArray encode(const QByteArray &payload);
    // 接收端：把 socket 读到的任意长度字节流喂进来，内部缓存并循环拆包，
    // 返回本次凑齐的完整正文（可能 0 帧或多帧——分别对应半包与粘包）。
    QList<QByteArray> feed(const QByteArray &bytes);

    // 一旦出现非法帧，错误置位后 feed() 不再工作，需要 reset() 才能复用。
    bool hasProtocolError() const noexcept;
    QString lastError() const;
    void reset();

private:
    // 未凑齐帧的剩余字节缓存：跨多次 readyRead 累积，这就是半包的解决方式。
    QByteArray buffer_;
    QString lastError_;
};
