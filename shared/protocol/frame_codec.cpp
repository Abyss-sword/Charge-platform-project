#include "shared/protocol/frame_codec.h"

// 帧协议实现：发送侧 encode 加 4 字节大端长度头；接收侧 feed 按长度头累积字节、
// 切出完整 JSON 正文，同时处理 TCP 半包（字节不够先缓存）与粘包（一次拆出多帧）。
QByteArray FrameCodec::encode(const QByteArray &payload)
{
    // 取正文长度，先填 4 字节帧头（大端序：高位字节在前）。
    const quint32 size = static_cast<quint32>(payload.size());
    QByteArray frame(4, '\0');
    frame[0] = static_cast<char>((size >> 24U) & 0xffU);
    frame[1] = static_cast<char>((size >> 16U) & 0xffU);
    frame[2] = static_cast<char>((size >> 8U) & 0xffU);
    frame[3] = static_cast<char>(size & 0xffU);
    // 帧头 + 正文拼成完整帧。
    frame.append(payload);
    return frame;
}

QList<QByteArray> FrameCodec::feed(const QByteArray &bytes)
{
    QList<QByteArray> frames;
    if (!lastError_.isEmpty()) {
        return frames;
    }

    // 1) 收到的字节先追加进缓存：调用方不必保证一次读满一帧。
    buffer_.append(bytes);
    // 2) 缓存里至少有一个帧头就循环拆包；一帧拆完继续，粘包时一次调用可拆出多帧。
    while (buffer_.size() >= 4) {
        // 3) 从缓存前 4 字节还原正文长度（大端）。
        const auto *header = reinterpret_cast<const uchar *>(buffer_.constData());
        const quint32 size = (static_cast<quint32>(header[0]) << 24U)
            | (static_cast<quint32>(header[1]) << 16U)
            | (static_cast<quint32>(header[2]) << 8U)
            | static_cast<quint32>(header[3]);

        // 4) 长度为 0：非法帧，置协议错误并清空缓存，由上层断开/复位。
        if (size == 0U) {
            lastError_ = QStringLiteral("invalid payload length: 0");
            buffer_.clear();
            break;
        }
        // 5) 超过 1MiB 上限：同样按协议错误处理。
        if (size > MaxPayload) {
            lastError_ = QStringLiteral("payload exceeds maximum: %1").arg(size);
            buffer_.clear();
            break;
        }
        // 6) 半包：正文尚未凑齐，保留缓存等待后续字节到来。
        if (buffer_.size() < 4 + static_cast<qsizetype>(size)) {
            break;
        }

        // 7) 凑齐一帧：跳过帧头切出正文；已消费字节从缓存移除，剩余字节进入下一轮循环（粘包处理）。
        frames.append(buffer_.mid(4, static_cast<qsizetype>(size)));
        buffer_.remove(0, 4 + static_cast<qsizetype>(size));
    }
    return frames;
}

bool FrameCodec::hasProtocolError() const noexcept
{
    return !lastError_.isEmpty();
}

QString FrameCodec::lastError() const
{
    return lastError_;
}

void FrameCodec::reset()
{
    // 清空缓存与错误标记，编解码器可复用于新的连接。
    buffer_.clear();
    lastError_.clear();
}
