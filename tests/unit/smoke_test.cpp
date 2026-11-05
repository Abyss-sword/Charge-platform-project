// 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
// 冒烟测试：验证 Qt 工具链与测试框架可正常构建运行（最先确认环境正常）。

#include <QtTest>

class SmokeTest final : public QObject
{
    Q_OBJECT

private slots:
    void qtRuntimeIsAvailable()
    {
        QCOMPARE(QStringLiteral("qt"), QStringLiteral("qt"));
    }
};

QTEST_MAIN(SmokeTest)
#include "smoke_test.moc"
