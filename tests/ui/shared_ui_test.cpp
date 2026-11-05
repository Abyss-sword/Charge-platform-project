#include <QtTest>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include "theme.h"
#include "widgets/page_state_widget.h"
#include "widgets/status_badge.h"
#include "widgets/page_scaffold.h"
#include "shell/main_window.h"
#include "shell/bottom_navigation_bar.h"
#include "shell/mobile_page_header.h"
#include "widgets/confirm_dialog.h"
#include "widgets/toast.h"
#include "widgets/motion.h"
#include "widgets/enum_labels.h"
#include <QDialogButtonBox>
#include <QFile>

class SharedUiTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { Ui::Theme::apply(*qApp); }
    void supportsEveryPresentationState()
    {
        Ui::PageStateWidget widget;
        widget.resize(300, 80);
        widget.show();
        QSignalSpy spy(&widget, &Ui::PageStateWidget::stateChanged);
        for (auto state : {Ui::PageState::Loading, Ui::PageState::Normal, Ui::PageState::Empty,
                          Ui::PageState::Error, Ui::PageState::Offline, Ui::PageState::Reconnecting,
                          Ui::PageState::Degraded}) {
            widget.setState(state);
            QCOMPARE(widget.state(), state);
            auto *label = widget.findChild<QLabel *>("stateMessage");
            QVERIFY(label && label->isVisible());
            QVERIFY(!label->text().isEmpty());
        }
        QCOMPARE(spy.count(), 7);
    }
    void badgesHaveTextAndTone()
    {
        Ui::StatusBadge badge(Ui::BadgeState::Connected);
        for (auto state : {Ui::BadgeState::Connected, Ui::BadgeState::Disconnected,
                           Ui::BadgeState::Charging, Ui::BadgeState::Available,
                           Ui::BadgeState::Error, Ui::BadgeState::Simulated}) {
            badge.setState(state);
            QVERIFY(!badge.text().isEmpty());
            QVERIFY(badge.text().contains(QStringLiteral("已连接")) || badge.text().contains(QStringLiteral("未连接"))
                    || badge.text().contains(QStringLiteral("充电中")) || badge.text().contains(QStringLiteral("可用"))
                    || badge.text().contains(QStringLiteral("异常")) || badge.text().contains(QStringLiteral("SIMULATED")));
            QVERIFY(badge.accessibleName().contains(QStringLiteral("连接")) || badge.accessibleName().contains(QStringLiteral("充电"))
                    || badge.accessibleName().contains(QStringLiteral("可用")) || badge.accessibleName().contains(QStringLiteral("异常"))
                    || badge.accessibleName().contains(QStringLiteral("SIMULATED")));
            QCOMPARE(badge.property("iconSource").toString(), QStringLiteral(":/ui/icons/status.svg"));
            QVERIFY(!badge.property("tone").toString().isEmpty());
        }
    }
    void previewsEmptyErrorAndRestoresContent()
    {
        UserMainWindow window;
        window.resize(360, 780);
        window.show();
        window.setCurrentPage(PrimaryPage::Orders);
        auto *stack = window.findChild<QStackedWidget *>("primaryPages");
        auto *page = stack->currentWidget();
        auto *selector = page->findChild<QComboBox *>("mockStateSelector");
        auto *state = page->findChild<Ui::PageStateWidget *>();
        auto *content = page->findChild<QWidget *>("scrollContent");
        QVERIFY(selector && state && content);
        for (auto value : {Ui::PageState::Empty, Ui::PageState::Error}) {
            selector->setCurrentIndex(selector->findData(static_cast<int>(value)));
            QCOMPARE(state->state(), value);
            QVERIFY(!content->isVisible());
            QVERIFY(state->isVisible());
        }
        selector->setCurrentIndex(selector->findData(static_cast<int>(Ui::PageState::Normal)));
        QVERIFY(content->isVisible());
    }
    void mapModeIsLocalAndPreservesList()
    {
        UserMainWindow window;
        window.resize(360, 780);
        window.show();
        window.setCurrentPage(PrimaryPage::Stations);
        auto *mode = window.findChild<QComboBox *>("stationMode");
        auto *views = window.findChild<QStackedWidget *>("stationViews");
        QVERIFY(mode && views);
        QCOMPARE(views->currentIndex(), 0);
        auto *list = views->widget(0);
        mode->setCurrentIndex(1);
        QCOMPARE(views->currentIndex(), 1);
        QVERIFY(views->currentWidget()->findChildren<QWidget *>().isEmpty());
        auto *fallback = qobject_cast<QLabel *>(views->currentWidget());
        QVERIFY(fallback && fallback->text().contains(QStringLiteral("离线演示")));
        mode->setCurrentIndex(0);
        QCOMPARE(views->currentWidget(), list);
    }
    void primaryActionsRespectStateAndNavigate()
    {
        UserMainWindow window;
        window.show();
        auto *stack = window.findChild<QStackedWidget *>("primaryPages");
        auto *home = qobject_cast<Ui::PageScaffold *>(stack->widget(0));
        QVERIFY(home);
        home->setPageState(Ui::PageState::Loading);
        QVERIFY(!home->primaryAction()->isEnabled());
        home->setPageState(Ui::PageState::Normal);
        QTest::mouseClick(home->primaryAction(), Qt::LeftButton);
        QCOMPARE(stack->currentIndex(), 1);
        window.setCurrentPage(PrimaryPage::Account);
        auto *account = qobject_cast<Ui::PageScaffold *>(stack->currentWidget());
        QVERIFY(account);
        QTest::mouseClick(account->primaryAction(), Qt::LeftButton);
        QCOMPARE(stack->currentIndex(), 3);
    }
    // 审查 01 规格 · 候选栏改造：固定枚举必须"显示中文、提交协议值"，且协议值可反查中文。
    void enumCandidateBarMapsLabelToProtocolValue()
    {
        const auto chargeTypes = Ui::chargeTypeOptions();
        QCOMPARE(chargeTypes.size(), 2);
        QCOMPARE(Ui::labelFor(chargeTypes, QStringLiteral("FAST")), QStringLiteral("快充"));
        QCOMPARE(Ui::labelFor(chargeTypes, QStringLiteral("SLOW")), QStringLiteral("慢充"));
        for (const auto &option : chargeTypes) {
            QVERIFY(!option.label.isEmpty());
            QVERIFY(option.value == QStringLiteral("FAST") || option.value == QStringLiteral("SLOW"));
        }

        const auto stationStatuses = Ui::stationStatusOptions();
        QCOMPARE(Ui::labelFor(stationStatuses, QStringLiteral("OPEN")), QStringLiteral("营业中"));
        QCOMPARE(Ui::labelFor(stationStatuses, QStringLiteral("MAINTENANCE")), QStringLiteral("维护中"));
        QCOMPARE(Ui::labelFor(stationStatuses, QStringLiteral("CLOSED")), QStringLiteral("已关闭"));

        // ADMIN_PILE_SET_STATUS 服务端仅接受 IDLE/FAULT/OFFLINE 三种协议值。
        const auto pileStatuses = Ui::pileStatusOptions();
        QCOMPARE(pileStatuses.size(), 3);
        QCOMPARE(Ui::labelFor(pileStatuses, QStringLiteral("IDLE")), QStringLiteral("空闲"));
        QCOMPARE(Ui::labelFor(pileStatuses, QStringLiteral("FAULT")), QStringLiteral("故障"));
        QCOMPARE(Ui::labelFor(pileStatuses, QStringLiteral("OFFLINE")), QStringLiteral("离线"));

        // 未登记的协议值原样返回（服务端新增值不至显示为空）。
        QCOMPARE(Ui::labelFor(pileStatuses, QStringLiteral("RESERVED")), QStringLiteral("RESERVED"));

        // fillCombo：显示中文、userData 存协议值，且不可自由输入。
        QComboBox combo;
        Ui::fillCombo(&combo, pileStatuses);
        QCOMPARE(combo.count(), 3);
        QCOMPARE(combo.itemText(0), QStringLiteral("空闲"));
        QCOMPARE(combo.itemData(0).toString(), QStringLiteral("IDLE"));
        QCOMPARE(combo.itemData(1).toString(), QStringLiteral("FAULT"));
        QCOMPARE(combo.itemData(2).toString(), QStringLiteral("OFFLINE"));
        QVERIFY(!combo.isEditable());
    }
    void navigationIgnoresInvalidPageAndSignalsOnce()
    {
        BottomNavigationBar bar;
        QSignalSpy spy(&bar, &BottomNavigationBar::currentPageChanged);
        bar.setCurrentPage(PrimaryPage::Stations);
        bar.setCurrentPage(PrimaryPage::Stations);
        bar.setCurrentPage(static_cast<PrimaryPage>(99));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(bar.currentPage(), PrimaryPage::Stations);
    }
    void headerSupportsFutureBackButton()
    {
        MobilePageHeader header;
        header.setTitle(QStringLiteral("站点详情"));
        header.resize(360, 64);
        header.setBackAvailable(true);
        header.show();
        QSignalSpy spy(&header, &MobilePageHeader::backRequested);
        auto *button = header.findChild<QToolButton *>();
        QVERIFY(button && button->isVisible());
        QTest::mouseClick(button, Qt::LeftButton);
        QCOMPARE(spy.count(), 1);
    }
    void sharedFeedbackAndMotion()
    {
        QCOMPARE(Ui::Theme::MotionFastMs, 150); QCOMPARE(Ui::Theme::MotionNormalMs, 300); QCOMPARE(Ui::Theme::MotionSlowMs, 500);
        Ui::ConfirmDialog normal(QStringLiteral("确认"), QStringLiteral("继续操作"));
        QVERIFY(normal.confirmButton()); QVERIFY(normal.findChild<QPushButton *>("cancelButton"));
        Ui::ConfirmDialog danger(QStringLiteral("远程重启"), QStringLiteral("桩 PILE-001 将暂时停止服务"), Ui::ConfirmMode::Danger);
        QCOMPARE(danger.property("mode").toString(), QStringLiteral("danger"));
        QVERIFY(danger.findChild<QLabel *>("dialogMessage")->text().contains(QStringLiteral("操作对象与影响")));
        QCOMPARE(danger.confirmButton()->property("role").toString(), QStringLiteral("danger"));
        for (auto type : {Ui::ToastType::Success, Ui::ToastType::Info, Ui::ToastType::Warning, Ui::ToastType::Error}) {
            Ui::Toast toast(type, QStringLiteral("操作结果"), 20); QVERIFY(toast.closeButton()); toast.showToast(); QVERIFY(toast.isVisible());
            QTest::mouseClick(toast.closeButton(), Qt::LeftButton); QVERIFY(!toast.isVisible());
        }
        Ui::Toast automatic(Ui::ToastType::Info, QStringLiteral("自动关闭"), 10); automatic.showToast(); QTest::qWait(30); QVERIFY(!automatic.isVisible());
        Ui::Motion::setEnabled(false); QVERIFY(!Ui::Motion::createGeometry(&normal, QRect(0,0,1,1)));
        Ui::Motion::setEnabled(true); auto *animation = Ui::Motion::createGeometry(&normal, QRect(0,0,1,1)); QVERIFY(animation); delete animation;
        Ui::Motion::setEnabled(true);
        QFile qss(QStringLiteral(":/ui/common.qss")); QVERIFY(qss.open(QIODevice::ReadOnly)); const auto text = QString::fromUtf8(qss.readAll());
        QVERIFY(text.contains(QStringLiteral("QTableView"))); QVERIFY(text.contains(QStringLiteral("QDialog#confirmDialog"))); QVERIFY(text.contains(QStringLiteral("QFrame#toast")));
    }
};
QTEST_MAIN(SharedUiTest)
#include "shared_ui_test.moc"
