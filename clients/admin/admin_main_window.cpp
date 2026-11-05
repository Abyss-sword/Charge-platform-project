#include "admin_main_window.h"
#include "admin_pages.h"
#include "admin_analysis_page.h"
#include "analysis/analysis_http_client.h"
#include "admin_provider.h"
#include "theme.h"
#include <QFrame>
#include <QLabel>
#include <QStackedWidget>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <utility>
AdminMainWindow::AdminMainWindow(QWidget *parent):QMainWindow(parent){ build({}, nullptr); }
AdminMainWindow::AdminMainWindow(QString adminToken, IAdminProvider *provider, QWidget *parent):QMainWindow(parent){ build(std::move(adminToken), provider); }
void AdminMainWindow::build(QString adminToken, IAdminProvider *provider){
    const bool remote = provider != nullptr;
    setWindowTitle(remote ? QStringLiteral("EV FLOW · 运营工作台") : QStringLiteral("EV FLOW · 运营工作台 · OFFLINE_DEMO · SIMULATED")); setMinimumSize(1180,720); resize(1360,820);
    auto *root=new QWidget(this); setCentralWidget(root); auto *outer=new QVBoxLayout(root); outer->setContentsMargins(0,0,0,0); outer->setSpacing(0);
    auto *top=new QFrame(root); top->setObjectName(QStringLiteral("adminTopBar")); auto *tl=new QHBoxLayout(top); tl->setContentsMargins(16,10,16,10); auto *title=new QLabel(QStringLiteral("运营工作台"),top); title->setProperty("role","title"); tl->addWidget(title); tl->addStretch(); auto *identity=new QLabel(remote ? QStringLiteral("管理员：已认证") : QStringLiteral("管理员：operator-demo · 运营员 · SIMULATED"),top); identity->setObjectName("adminIdentity"); tl->addWidget(identity); auto *connection=new QLabel(remote ? QStringLiteral("订单数据：正式服务") : QStringLiteral("连接状态：已连接 · SIMULATED"),top); connection->setObjectName("adminConnectionStatus"); tl->addWidget(connection); outer->addWidget(top);
    auto *body=new QWidget(root); auto *bl=new QHBoxLayout(body); bl->setContentsMargins(0,0,0,0); bl->setSpacing(0); outer->addWidget(body,1);
    auto *sidebar=new QFrame(body); sidebar->setObjectName(QStringLiteral("adminSidebar")); sidebar->setMinimumWidth(196); sidebar->setMaximumWidth(220); auto *sl=new QVBoxLayout(sidebar); sl->setContentsMargins(12,16,12,12); sl->setSpacing(4); auto *brand=new QLabel(QStringLiteral("EV FLOW\n充电运营"),sidebar); brand->setProperty("role","section"); sl->addWidget(brand);
    const QStringList labels{QStringLiteral("总览"),QStringLiteral("营收"),QStringLiteral("站点资产"),QStringLiteral("充电桩"),QStringLiteral("订单"),QStringLiteral("用户"),QStringLiteral("告警工单"),QStringLiteral("日志"),QStringLiteral("预测预警")};
    for(int i=0;i<labels.size();++i){auto *b=new QToolButton(sidebar); b->setObjectName(QStringLiteral("adminNav%1").arg(i)); b->setText(labels[i]); b->setCheckable(true); b->setAutoExclusive(true); b->setToolButtonStyle(Qt::ToolButtonTextOnly); b->setMinimumHeight(40); b->setProperty("role","adminNavigation"); sl->addWidget(b); nav_.append(b); connect(b,&QToolButton::clicked,this,[this,i]{setCurrentPage(static_cast<AdminPage>(i));});} sl->addStretch(); bl->addWidget(sidebar);
    pages_=new QStackedWidget(body); pages_->setObjectName(QStringLiteral("adminPages")); const AdminMockDataProvider mock;
    auto *overview=remote?new OverviewPage(mock.preview(AdminPage::Overview),provider,adminToken,pages_):new OverviewPage(mock.preview(AdminPage::Overview),pages_);
    // S1-B：总览清单行点击 → 跳转对应页（0=总览 1=营收 2=站点 3=电桩 4=订单 5=用户 6=告警工单 7=日志）。
    connect(overview,&OverviewPage::pageRequested,this,[this](int index){ setCurrentPage(static_cast<AdminPage>(index)); });
    pages_->addWidget(overview);
    pages_->addWidget(remote?new RevenuePage(mock.preview(AdminPage::Revenue),provider,adminToken,pages_):new RevenuePage(mock.preview(AdminPage::Revenue),pages_));
    pages_->addWidget(remote?new StationAssetsPage(mock.preview(AdminPage::StationAssets),provider,adminToken,pages_):new StationAssetsPage(mock.preview(AdminPage::StationAssets),pages_));
    pages_->addWidget(remote?new PileDevicesPage(mock.preview(AdminPage::PileDevices),provider,adminToken,pages_):new PileDevicesPage(mock.preview(AdminPage::PileDevices),pages_));
    pages_->addWidget(remote?new OrdersPage(mock.preview(AdminPage::Orders),provider,adminToken,pages_):new OrdersPage(mock.preview(AdminPage::Orders),pages_));
    pages_->addWidget(remote?new UserManagementPage(mock.preview(AdminPage::Users),provider,adminToken,pages_):new UserManagementPage(mock.preview(AdminPage::Users),pages_));
    pages_->addWidget(remote?new AlertsTicketsPage(mock.preview(AdminPage::AlertsTickets),provider,adminToken,pages_):new AlertsTicketsPage(mock.preview(AdminPage::AlertsTickets),pages_));
    pages_->addWidget(remote?new LogsPage(mock.preview(AdminPage::Logs),provider,std::move(adminToken),pages_):new LogsPage(mock.preview(AdminPage::Logs),pages_));
    auto *analysis = new AdminAnalysisPage(pages_);
    if (remote) analysis->setAnalysisBaseUrl(charge::analysis::AnalysisHttpClient::configuredBaseUrl());
    pages_->addWidget(analysis); bl->addWidget(pages_,1); setCurrentPage(AdminPage::Overview);
}
int AdminMainWindow::pageCount() const{return pages_?pages_->count():0;}
void AdminMainWindow::setCurrentPage(AdminPage page){const int i=static_cast<int>(page); if(!pages_||i<0||i>=pages_->count())return; pages_->setCurrentIndex(i); current_=i; if(i<nav_.size())nav_[i]->setChecked(true); QWidget*w=pages_->currentWidget();if(auto*p=dynamic_cast<OverviewPage*>(w))p->refresh();else if(auto*p=dynamic_cast<RevenuePage*>(w))p->refresh();else if(auto*p=dynamic_cast<StationAssetsPage*>(w))p->refresh();else if(auto*p=dynamic_cast<PileDevicesPage*>(w))p->refresh();else if(auto*p=dynamic_cast<OrdersPage*>(w))p->refresh();else if(auto*p=dynamic_cast<UserManagementPage*>(w))p->refresh();else if(auto*p=dynamic_cast<AlertsTicketsPage*>(w))p->refresh();else if(auto*p=dynamic_cast<LogsPage*>(w))p->refresh();}
void AdminMainWindow::setNavigationAuthorized(int index, bool authorized){if(index<0||index>=nav_.size())return; nav_[index]->setEnabled(authorized); nav_[index]->setProperty("accessRule", "ADMIN_UI_ACCESS_RULE"); nav_[index]->setToolTip(authorized?QStringLiteral("允许访问"):QStringLiteral("无权访问"));}
