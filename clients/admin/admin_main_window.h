#pragma once
#include <QMainWindow>
#include "admin_mock_data_provider.h"
class IAdminProvider;
class QStackedWidget; class QToolButton; class AdminLoginPage;
class AdminMainWindow final : public QMainWindow { public: explicit AdminMainWindow(QWidget *parent=nullptr); AdminMainWindow(QString adminToken, IAdminProvider *provider, QWidget *parent=nullptr); int pageCount() const; void setCurrentPage(AdminPage page); void setNavigationAuthorized(int index, bool authorized); private: void build(QString adminToken, IAdminProvider *provider); QStackedWidget *pages_{}; QList<QToolButton*> nav_; int current_=0; };
