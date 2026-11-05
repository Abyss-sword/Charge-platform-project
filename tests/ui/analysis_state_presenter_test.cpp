#include <QtTest>

#include "analysis/forecast_model.h"
#include "widgets/analysis_state_presenter.h"
#include "widgets/page_scaffold.h"

#include <QLabel>

using charge::analysis::ForecastStatus;

class AnalysisStatePresenterTest final : public QObject
{
    Q_OBJECT

private slots:
    void mapsForecastStatus_data()
    {
        QTest::addColumn<ForecastStatus>("status");
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("description");
        QTest::addColumn<Ui::PageState>("pageState");
        QTest::addColumn<bool>("showContent");
        QTest::addColumn<bool>("staleWarning");
        QTest::addColumn<bool>("retryAllowed");

        QTest::newRow("ok")
            << ForecastStatus::Ok
            << QStringLiteral("分析数据可用")
            << QStringLiteral("预测结果已按当前数据版本生成。")
            << Ui::PageState::Normal << true << false << false;
        QTest::newRow("no-data")
            << ForecastStatus::NoData
            << QStringLiteral("暂无分析数据")
            << QStringLiteral("当前站点与所选时段没有足够数据。")
            << Ui::PageState::Empty << false << false << false;
        QTest::newRow("expired")
            << ForecastStatus::Expired
            << QStringLiteral("分析数据已过期")
            << QStringLiteral("当前结果可能不是最新；可暂时查看并等待更新。")
            << Ui::PageState::Stale << true << true << true;
        QTest::newRow("model-unavailable")
            << ForecastStatus::ModelUnavailable
            << QStringLiteral("模型服务暂不可用")
            << QStringLiteral("暂时无法生成预测结果，基础业务功能不受影响。")
            << Ui::PageState::Error << false << false << true;
        QTest::newRow("dependency-unavailable")
            << ForecastStatus::DependencyUnavailable
            << QStringLiteral("数据依赖暂不可用")
            << QStringLiteral("分析数据源或批处理暂不可用，基础业务功能不受影响。")
            << Ui::PageState::Error << false << false << true;
        QTest::newRow("param-invalid")
            << ForecastStatus::ParamInvalid
            << QStringLiteral("请求参数无效")
            << QStringLiteral("请选择受支持的站点、时刻或预测期限。")
            << Ui::PageState::Error << false << false << false;
    }

    void mapsForecastStatus()
    {
        QFETCH(ForecastStatus, status);
        QFETCH(QString, title);
        QFETCH(QString, description);
        QFETCH(Ui::PageState, pageState);
        QFETCH(bool, showContent);
        QFETCH(bool, staleWarning);
        QFETCH(bool, retryAllowed);

        const Ui::AnalysisStatePresentation presentation =
            Ui::analysisStatePresentation(status);
        QCOMPARE(presentation.title, title);
        QCOMPARE(presentation.description, description);
        QCOMPARE(presentation.pageState, pageState);
        QCOMPARE(presentation.showContent, showContent);
        QCOMPARE(presentation.staleWarning, staleWarning);
        QCOMPARE(presentation.retryAllowed, retryAllowed);
    }

    void appliesExpiredStateWithoutHidingExistingContent()
    {
        Ui::PageScaffold page(QStringLiteral("预测详情"), QStringLiteral("未来负荷"));
        auto *content = new QLabel(QStringLiteral("已有预测结果"), &page);
        content->setObjectName(QStringLiteral("analysisContent"));
        page.addContent(content);
        page.resize(600, 400);
        page.show();

        const auto presentation =
            Ui::applyAnalysisState(&page, ForecastStatus::Expired);
        QCOMPARE(page.stateWidget()->state(), Ui::PageState::Stale);
        QVERIFY(content->isVisible());
        QVERIFY(page.stateWidget()->message().contains(presentation.title));
        QVERIFY(page.stateWidget()->message().contains(presentation.description));
    }

    void appliesUnavailableStateAndHidesAnalysisContent()
    {
        Ui::PageScaffold page(QStringLiteral("预测详情"), QStringLiteral("未来负荷"));
        auto *content = new QLabel(QStringLiteral("已有预测结果"), &page);
        page.addContent(content);
        page.resize(600, 400);
        page.show();

        Ui::applyAnalysisState(&page, ForecastStatus::DependencyUnavailable);
        QCOMPARE(page.stateWidget()->state(), Ui::PageState::Error);
        QVERIFY(!content->isVisible());
        QVERIFY(page.stateWidget()->message().contains(QStringLiteral("数据依赖暂不可用")));
    }
};

QTEST_MAIN(AnalysisStatePresenterTest)
#include "analysis_state_presenter_test.moc"
