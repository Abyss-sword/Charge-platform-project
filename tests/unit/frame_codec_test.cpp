// 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
// 帧协议单测：验证 4 字节大端长度头编解码、TCP 分段（半包）与粘包拆包、非法帧判定——
// 证明网络拆包算法正确。

#include <QtTest>

#include "shared/protocol/frame_codec.h"

class FrameCodecTest final : public QObject
{
    Q_OBJECT

private slots:
    void waitsForCompleteFrame()
    {
        FrameCodec codec;
        const QByteArray payload = R"({"action":"PING"})";
        const QByteArray frame = FrameCodec::encode(payload);

        QCOMPARE(codec.feed(frame.left(5)).size(), 0);
        QCOMPARE(codec.feed(frame.mid(5)), QList<QByteArray>{payload});
        QVERIFY(!codec.hasProtocolError());
    }

    void extractsTwoFrames()
    {
        FrameCodec codec;
        QCOMPARE(codec.feed(FrameCodec::encode("A") + FrameCodec::encode("B")),
                 QList<QByteArray>({"A", "B"}));
    }

    void rejectsZeroLengthFrame()
    {
        FrameCodec codec;
        codec.feed(QByteArray(4, '\0'));
        QVERIFY(codec.hasProtocolError());
        QCOMPARE(codec.lastError(), QStringLiteral("invalid payload length: 0"));
    }

    void rejectsOversizedFrame()
    {
        FrameCodec codec;
        QByteArray header(4, '\0');
        const quint32 size = FrameCodec::MaxPayload + 1;
        header[0] = static_cast<char>((size >> 24) & 0xff);
        header[1] = static_cast<char>((size >> 16) & 0xff);
        header[2] = static_cast<char>((size >> 8) & 0xff);
        header[3] = static_cast<char>(size & 0xff);

        codec.feed(header);
        QVERIFY(codec.hasProtocolError());
        QCOMPARE(codec.lastError(),
                 QStringLiteral("payload exceeds maximum: 1048577"));
    }
};

QTEST_MAIN(FrameCodecTest)
#include "frame_codec_test.moc"
