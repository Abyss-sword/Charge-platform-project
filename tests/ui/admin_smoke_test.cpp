#include <QtTest>
#include <QPointer>
#include <QStackedWidget>
#include <QToolButton>
#include <QLabel>
#include <QLineEdit>
#include <QFrame>
#include <QScrollArea>
#include <QScrollBar>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QPalette>
#include <QColor>
#include <QSet>
#include "widgets/page_state_widget.h"
#include "widgets/motion.h"
#include "theme.h"
#include "admin_main_window.h"
#include "admin_pages.h"
#include "admin_provider.h"
class UiAdminProvider final : public IAdminProvider
{
public:
 using IAdminProvider::IAdminProvider;
 void login(const QString &,const QString &) override {}
 void queryOrders(const QString &,const QString &,int,int) override {}
 void queryLogs(const QString &,const QString &,int,int) override {}
 // 记录告警/工单查询次数：用于断言"进入页面即加载数据"（P1-A 防回归）。
 void queryAlarms(const QString &,const QString &,const QString &,int,int) override { ++alarmQueries; }
 void queryTickets(const QString &,const QString &,int,int) override { ++ticketQueries; }
 int alarmQueries=0;
 int ticketQueries=0;
};
class AdminSmokeTest final : public QObject
{
    Q_OBJECT
private slots:
 void initTestCase(){Ui::Theme::apply(*qApp);}
 void createsLogin(){AdminLoginPage p; QVERIFY(p.findChild<QLineEdit*>("adminAccount")); QVERIFY(p.findChild<QLineEdit*>("adminPassword")); QVERIFY(p.primaryAction());}
 void remoteUiHasNoDevelopmentCopy(){UiAdminProvider provider;AdminMainWindow w(QStringLiteral("TOKEN"),&provider);w.show();QCoreApplication::processEvents();const QStringList forbidden{QStringLiteral("SIMULATED"),QStringLiteral("mock"),QStringLiteral("fixture"),QStringLiteral("教学"),QStringLiteral("演示数据"),QStringLiteral("界面预览"),QStringLiteral("测试数据"),QStringLiteral("preview"),QStringLiteral("fallback")};for(auto*label:w.findChildren<QLabel*>())for(const auto&word:forbidden)QVERIFY2(!label->text().contains(word,Qt::CaseInsensitive),qPrintable(label->text()));}
 void offlineDemoIsExplicitlyMarked(){AdminLoginPage login;AdminMainWindow w;for(const auto*widget:{static_cast<QWidget*>(&login),static_cast<QWidget*>(&w)}){QVERIFY(widget->windowTitle().contains(QStringLiteral("OFFLINE_DEMO")));QVERIFY(widget->windowTitle().contains(QStringLiteral("SIMULATED")));}QVERIFY(w.findChild<QLabel*>(QStringLiteral("adminIdentity"))->text().contains(QStringLiteral("SIMULATED")));}
 void createsAndNavigates(){AdminMainWindow w; QCOMPARE(w.size(),QSize(1360,820)); QCOMPARE(w.minimumSize(),QSize(1180,720)); QCOMPARE(w.pageCount(),9); auto *stack=w.findChild<QStackedWidget*>("adminPages"); QVERIFY(stack); auto *side=w.findChild<QFrame*>("adminSidebar"); QVERIFY(side); QCOMPARE(side->findChildren<QToolButton*>().size(),9); QList<QPointer<QWidget>> pages; for(int i=0;i<9;++i)pages.append(stack->widget(i)); for(int i=0;i<9;++i){auto *b=side->findChild<QToolButton*>(QString("adminNav%1").arg(i)); QVERIFY(b); QTest::mouseClick(b,Qt::LeftButton); QCOMPARE(stack->currentIndex(),i); QVERIFY(b->isChecked()); for(int j=0;j<9;++j)QCOMPARE(stack->widget(j),pages[j].data());}}
 void fitsMinimum(){AdminMainWindow w; w.resize(1180,720); w.show(); QTest::qWait(30); auto *side=w.findChild<QFrame*>("adminSidebar"); auto *top=w.findChild<QFrame*>("adminTopBar"); auto *stack=w.findChild<QStackedWidget*>("adminPages"); QVERIFY(side&&top&&stack); QVERIFY(w.rect().contains(QRect(side->mapTo(&w,QPoint()),side->size()))); QVERIFY(w.rect().contains(QRect(top->mapTo(&w,QPoint()),top->size()))); for(int i : {3,4}){w.setCurrentPage(static_cast<AdminPage>(i)); auto *table=stack->widget(i)->findChild<QTableWidget*>("adminTable"); QVERIFY2(table&&table->horizontalHeader()->isVisible(),qPrintable(QStringLiteral("page %1 table header hidden").arg(i))); QVERIFY(w.rect().contains(QRect(table->horizontalHeader()->mapTo(&w,QPoint()),table->horizontalHeader()->size())));}}
 void allPagesFitSupportedSizes_data(){QTest::addColumn<QSize>("size");QTest::newRow("minimum")<<QSize(1180,720);QTest::newRow("stage2")<<QSize(1366,768);QTest::newRow("full-hd")<<QSize(1920,1080);}
 void allPagesFitSupportedSizes(){QFETCH(QSize,size);AdminLoginPage login;login.resize(size);login.show();QCoreApplication::processEvents();QCOMPARE(login.size(),size);auto*loginScroll=login.findChild<QScrollArea*>("pageScroll");QVERIFY(loginScroll&&login.primaryAction()->isVisible());QCOMPARE(loginScroll->horizontalScrollBar()->maximum(),0);QVERIFY(login.rect().contains(QRect(login.primaryAction()->mapTo(&login,QPoint()),login.primaryAction()->size())));AdminMainWindow w;w.resize(size);w.show();QTest::qWait(30);QCOMPARE(w.size(),size);auto*side=w.findChild<QFrame*>("adminSidebar");auto*top=w.findChild<QFrame*>("adminTopBar");auto*stack=w.findChild<QStackedWidget*>("adminPages");QVERIFY(side&&top&&stack);QVERIFY(w.rect().contains(QRect(side->mapTo(&w,QPoint()),side->size())));QVERIFY(w.rect().contains(QRect(top->mapTo(&w,QPoint()),top->size())));QList<QPointer<QWidget>>pages;for(int i=0;i<stack->count();++i)pages.append(stack->widget(i));for(int i=0;i<stack->count();++i){w.setCurrentPage(static_cast<AdminPage>(i));QCoreApplication::processEvents();QCOMPARE(stack->currentIndex(),i);QCOMPARE(stack->widget(i),pages[i].data());auto*scroll=stack->currentWidget()->findChild<QScrollArea*>("pageScroll");QVERIFY2(scroll,qPrintable(QStringLiteral("page %1 missing scroll area").arg(i)));QCOMPARE(scroll->horizontalScrollBarPolicy(),Qt::ScrollBarAlwaysOff);QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);for(auto*button:stack->currentWidget()->findChildren<QPushButton*>())if(button->isVisible())QVERIFY2(w.rect().intersects(QRect(button->mapTo(&w,QPoint()),button->size())),qPrintable(button->text()));}for(int i=0;i<9;++i){auto*b=side->findChild<QToolButton*>(QStringLiteral("adminNav%1").arg(i));QVERIFY(b&&side->rect().contains(b->geometry()));}}
 void pagesAndStates(){AdminMainWindow w; auto *stack=w.findChild<QStackedWidget*>("adminPages"); QVERIFY(stack); for(int i=0;i<9;++i){auto *p=stack->widget(i); QVERIFY(p); QVERIFY(p->findChild<QLabel*>("stateMessage")); QVERIFY(p->findChild<QScrollArea*>("pageScroll"));}}
 void tablesIdentityAndAccess(){AdminMainWindow w; QVERIFY(w.findChild<QLabel*>("adminIdentity")); QVERIFY(w.findChild<QLabel*>("adminConnectionStatus")); auto *stack=w.findChild<QStackedWidget*>("adminPages"); QVERIFY(stack); for(int i : {3,4}) { auto *table=stack->widget(i)->findChild<QTableWidget*>("adminTable"); auto *page=qobject_cast<Ui::PageScaffold*>(stack->widget(i)); QVERIFY(table&&page); QVERIFY(table->rowCount()>0); const int rows=table->rowCount(); page->setPageState(Ui::PageState::Empty); QCOMPARE(table->rowCount(),0); QVERIFY(page->findChild<QLabel*>("stateMessage")->text().contains("无数据")); page->setPageState(Ui::PageState::Normal); QCOMPARE(table->rowCount(),rows); } auto *side=w.findChild<QFrame*>("adminSidebar"); QSet<QString> ids; for(auto *b:side->findChildren<QToolButton*>()){ QVERIFY(!ids.contains(b->objectName())); ids.insert(b->objectName()); } w.setNavigationAuthorized(6,false); QVERIFY(!side->findChild<QToolButton*>("adminNav6")->isEnabled()); QCOMPARE(side->findChild<QToolButton*>("adminNav6")->toolTip(),QStringLiteral("无权访问")); }
 void motionDisabledSidebarRegression(){Ui::Motion::setEnabled(false); AdminMainWindow w; auto *stack=w.findChild<QStackedWidget*>("adminPages"); auto *side=w.findChild<QFrame*>("adminSidebar"); QList<QPointer<QWidget>> pages; for(int i=0;i<9;++i)pages.append(stack->widget(i)); for(int i=0;i<9;++i){QTest::mouseClick(side->findChild<QToolButton*>(QString("adminNav%1").arg(i)),Qt::LeftButton);QCOMPARE(stack->currentIndex(),i);} for(int i=0;i<9;++i)QCOMPARE(stack->widget(i),pages[i].data()); Ui::Motion::setEnabled(true);}
 // P1-A 防回归：切换到"告警工单"页必须真正发起 ADMIN_ALARMS/ADMIN_TICKETS 查询。
 // 修复前 AlertsTicketsPage 未覆写 refresh() 且不在页面分发链，进入页面与 3 秒定时器都不加载数据。
 void alertsPageLoadsOnEntry(){UiAdminProvider provider; AdminMainWindow w(QStringLiteral("TOKEN"),&provider); w.show(); QCoreApplication::processEvents(); QVERIFY2(provider.alarmQueries==0&&provider.ticketQueries==0,qPrintable(QStringLiteral("未进入该页前不应查询"))); w.setCurrentPage(AdminPage::AlertsTickets); QCoreApplication::processEvents(); QVERIFY2(provider.alarmQueries>=1,qPrintable(QStringLiteral("进入告警工单页应查询告警"))); QVERIFY2(provider.ticketQueries>=1,qPrintable(QStringLiteral("进入告警工单页应查询工单"))); int alarms=provider.alarmQueries; int tickets=provider.ticketQueries; w.setCurrentPage(AdminPage::AlertsTickets); QCoreApplication::processEvents(); QVERIFY(provider.alarmQueries>alarms||provider.ticketQueries>tickets);}
 void sidebarNavigationColors(){AdminMainWindow w; w.show(); QTest::qWait(20); auto *side=w.findChild<QFrame*>("adminSidebar"); QVERIFY(side); auto *selected=side->findChild<QToolButton*>("adminNav0"); auto *unselected=side->findChild<QToolButton*>("adminNav1"); QVERIFY(selected&&unselected); QCOMPARE(selected->palette().color(QPalette::ButtonText), QColor(Ui::Theme::Card)); QCOMPARE(unselected->palette().color(QPalette::ButtonText), QColor(Ui::Theme::Card)); w.setNavigationAuthorized(1,false); QCOMPARE(unselected->palette().color(QPalette::ButtonText), QColor(Ui::Theme::Border)); }
};
QTEST_MAIN(AdminSmokeTest)
#include "admin_smoke_test.moc"
