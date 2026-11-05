// P1-C 轮询回归：可见页每 3 秒刷新、离开页面停止定时器（审查 01 规格 · 公共联动 S3）。
// 覆盖率缺口背景：此前车主端仅充电页有 3 秒轮询，且无 hideEvent 停止；其余可见页无周期刷新。
#include <QtTest>
#include <QLabel>
#include <QTimer>

#include "adapters/user_account_provider.h"
#include "adapters/user_order_provider.h"
#include "models/user_context.h"
#include "pages/home_page.h"
#include "pages/profile_page.h"
#include "pages/flow_pages.h"

namespace {
// 最小桩：记录查询次数，不依赖 TCP。
class StubAccountProvider final : public IUserAccountProvider
{
public:
    using IUserAccountProvider::IUserAccountProvider;
    void queryProfile(qint64) override { ++profiles; }
    void queryBalance(qint64) override { ++balances; }
    int profiles = 0;
    int balances = 0;
};

class StubOrderProvider final : public IUserOrderProvider
{
public:
    using IUserOrderProvider::IUserOrderProvider;
    void reserve(qint64, qint64) override {}
    void start(const QString &) override {}
    void stop(const QString &) override {}
    void cancel(const QString &) override {}
    void pay(const QString &) override {}
    void recharge(qint64, const QString &) override {}
    void queryActive(qint64) override { ++activeQueries; }
    void queryDetail(const QString &) override { ++detailQueries; }
    void queryHistory(qint64, int, int) override { ++historyQueries; }
    int activeQueries = 0;
    int detailQueries = 0;
    int historyQueries = 0;
};

PagePreview preview(const QString &title)
{
    return {title, QStringLiteral("测试"), {}, {}, Ui::PageState::Normal};
}

// 找到页面内的轮询定时器（PreviewPage 统一命名为 pagePollTimer）。
QTimer *pollTimer(QWidget *page)
{
    return page->findChild<QTimer *>(QStringLiteral("pagePollTimer"));
}
}

class UserPollingTest final : public QObject
{
    Q_OBJECT
private slots:
    // 进入可见页应启动 3 秒定时器；离开应停止（S3）。
    void visiblePagePollsAndStopsOnHide()
    {
        StubAccountProvider provider;
        HomePage page(preview(QStringLiteral("首页")), &provider, 42);
        auto *timer = pollTimer(&page);
        QVERIFY2(timer, "正式页面应存在 3 秒轮询定时器");
        QVERIFY2(!timer->isActive(), "未显示时不应轮询");
        page.show();
        QTRY_VERIFY2(timer->isActive(), "可见页应启动轮询");
        page.hide();
        QTRY_VERIFY2(!timer->isActive(), "离开页面应停止轮询");
    }

    // 轮询入口 refresh() 必须真正发起查询（首页=资料；账户页=资料+余额）。
    void refreshIssuesQueries()
    {
        StubAccountProvider provider;
        StubOrderProvider orderProvider;
        HomePage home(preview(QStringLiteral("首页")), &provider, 42);
        home.refresh();
        QCOMPARE(provider.profiles, 1);

        AccountPage account(preview(QStringLiteral("账户")), &orderProvider, &provider, 42);
        account.refresh();
        QVERIFY2(provider.profiles >= 2, "账户页 refresh 应查询资料");
        QVERIFY2(provider.balances >= 1, "账户页 refresh 应查询余额");
    }

    // 充电状态页改用基类轮询：离开页面停止（修复“充电中离开仍轮询”）。
    void chargingPageStopsPollingOnHide()
    {
        StubOrderProvider provider;
        ChargingStatusPage page(preview(QStringLiteral("充电")), &provider);
        page.setOrderId(QStringLiteral("ORD-POLL-1"));
        // setOrderId 会启动轮询并立即查询一次详情。
        page.show();
        QTRY_VERIFY2(provider.detailQueries >= 1, "进入充电页应查询订单详情");
        auto *timer = pollTimer(&page);
        QVERIFY(timer);
        QTRY_VERIFY2(timer->isActive(), "充电中应保持轮询");
        page.hide();
        QTRY_VERIFY2(!timer->isActive(), "离开充电页应停止轮询（此前不会停止）");
    }
};

QTEST_MAIN(UserPollingTest)
#include "user_polling_test.moc"
