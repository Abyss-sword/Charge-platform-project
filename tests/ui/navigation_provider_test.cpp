#include <QtTest>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include "adapters/navigation_provider.h"
#include "pages/flow_pages.h"

class NavigationProviderTest final : public QObject
{
    Q_OBJECT
private slots:
    void buildsTencentRouteUrl()
    {
        NavigationProvider provider;
        NavigationRequest request{23.1351, 113.3245, 23.1500, 113.3300,
                                  QStringLiteral("广州测试站")};
        const QUrl drive = provider.routeUrl(request, NavigationMode::Driving);
        QVERIFY(drive.toString().startsWith(QStringLiteral("https://apis.map.qq.com/uri/v1/routeplan")));
        QCOMPARE(drive.query(QUrl::FullyDecoded).contains(QStringLiteral("type=drive")), true);
        QVERIFY(drive.toString(QUrl::FullyDecoded).contains(QStringLiteral("广州测试站")));
        const QUrl walk = provider.routeUrl(request, NavigationMode::Walking);
        QVERIFY(walk.query(QUrl::FullyDecoded).contains(QStringLiteral("type=walk")));
        QVERIFY(walk.query(QUrl::FullyDecoded).contains(QStringLiteral("23.150000,113.330000")));
    }

    // S1-C：在线地图不可用时必须以静态站点卡兜底，不允许空白页面。
    // 断言兜底卡存在且含站点/坐标/距离/模拟起点标注/外部地图入口（WebEngine 可用时仍 QSKIP）。
    void unavailableIsExplicit()
    {
        NavigationProvider provider;
        if (provider.isAvailable())
            QSKIP("WebEngine is available; unavailable fallback is build-dependent");
        NavigationPage page({QStringLiteral("导航"), QStringLiteral("路线规划"), {}, {},
                              Ui::PageState::Normal}, &provider);
        page.setRoute({23.1351, 113.3245, 23.15, 113.33, QStringLiteral("测试站")});
        QCOMPARE(page.stateWidget()->state(), Ui::PageState::Degraded);
        QVERIFY(!page.findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly).isEmpty());
        QVERIFY(!page.findChildren<QWidget *>(QStringLiteral("navigationWebView")).size());
        auto *card = page.findChild<QWidget *>(QStringLiteral("navigationStaticCard"));
        QVERIFY2(card, "降级路径应渲染静态站点信息卡");
        const auto labels = card->findChildren<QLabel *>();
        QString text;
        for (auto *label : labels) text += label->text() + QLatin1Char('\n');
        QVERIFY2(text.contains(QStringLiteral("测试站")), "兜底卡应含站点名");
        QVERIFY2(text.contains(QStringLiteral("目的地坐标")), "兜底卡应含目的地坐标");
        QVERIFY2(text.contains(QStringLiteral("距模拟起点")), "兜底卡应含近似距离");
        QVERIFY2(text.contains(QStringLiteral("起点为模拟位置")), "兜底卡应标注模拟起点");
        QVERIFY2(card->findChild<QPushButton *>(QStringLiteral("openExternalMapAction")),
                 "兜底卡应提供外部地图入口");
    }
};

QTEST_MAIN(NavigationProviderTest)
#include "navigation_provider_test.moc"
