#include "admin_mock_data_provider.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
AdminMockResult AdminMockDataProvider::parse(const QByteArray &json, AdminMockScenario scenario) const {
    AdminMockResult r; r.state = scenario == AdminMockScenario::Offline ? Ui::PageState::Offline : Ui::PageState::Normal;
    if (scenario == AdminMockScenario::Timeout) { r.state=Ui::PageState::Error; r.errorCode="SIMULATED_TIMEOUT"; r.message="模拟请求超时"; return r; }
    if (scenario == AdminMockScenario::ServiceError) { r.state=Ui::PageState::Error; r.errorCode="SIMULATED_SERVICE_ERROR"; r.message="模拟服务错误"; return r; }
    if (scenario == AdminMockScenario::Offline) { r.message="模拟数据源离线"; return r; }
    const auto d=QJsonDocument::fromJson(json); if(!d.isObject()){r.state=Ui::PageState::Error;r.errorCode="SIMULATED_MALFORMED_JSON";r.message="模拟 JSON 格式错误";return r;} r.valid=true;r.payload=d.object(); if(scenario==AdminMockScenario::Empty){r.payload.insert("items",QJsonArray{});r.state=Ui::PageState::Empty;} return r;
}
AdminMockResult AdminMockDataProvider::load(AdminMockDataset dataset, AdminMockScenario scenario) const {
    Q_INIT_RESOURCE(admin_mock_resources);
    const QString path=dataset==AdminMockDataset::Login?":/mock/admin/login.json":dataset==AdminMockDataset::Overview?":/mock/admin/overview.json":dataset==AdminMockDataset::Devices?":/mock/admin/devices.json":":/mock/admin/orders.json"; QFile f(path); if(!f.open(QIODevice::ReadOnly)){AdminMockResult r;r.state=Ui::PageState::Error;r.errorCode="SIMULATED_RESOURCE_ERROR";r.message="模拟 JSON 资源缺失";return r;} return parse(f.readAll(),scenario);
}
AdminPreview AdminMockDataProvider::preview(AdminPage page) const {
    static const QList<QString> titles{QStringLiteral("运营概览"), QStringLiteral("营收分析"), QStringLiteral("站点资产"), QStringLiteral("电桩设备"), QStringLiteral("充电订单"), QStringLiteral("用户管理"), QStringLiteral("告警工单"), QStringLiteral("操作日志"), QStringLiteral("预测与预警")};
    const int i = static_cast<int>(page);
    AdminPreview p{titles.value(i), QStringLiteral("运营端模拟数据 · SIMULATED"), QStringLiteral("数据来源：MockDataProvider · 非正式接口"), {}, Ui::PageState::Normal};
    p.cards = {QStringLiteral("当前视图：%1").arg(titles.value(i)), QStringLiteral("数据状态：正常 · 可切换共享状态预览")};
    return p;
}
