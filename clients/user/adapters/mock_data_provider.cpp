#include "adapters/mock_data_provider.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>

MockResult MockDataProvider::parse(const QByteArray &json, MockScenario scenario) const
{
    MockResult result; result.state = scenario == MockScenario::Offline ? Ui::PageState::Offline : Ui::PageState::Normal;
    if (scenario == MockScenario::Timeout) { result.valid = false; result.state = Ui::PageState::Error; result.errorCode = QStringLiteral("SIMULATED_TIMEOUT"); result.message = QStringLiteral("模拟请求超时"); return result; }
    if (scenario == MockScenario::ServiceError) { result.valid = false; result.state = Ui::PageState::Error; result.errorCode = QStringLiteral("SIMULATED_SERVICE_ERROR"); result.message = QStringLiteral("模拟服务错误"); return result; }
    if (scenario == MockScenario::Offline) { result.valid = false; result.message = QStringLiteral("模拟数据源离线"); return result; }
    const auto document = QJsonDocument::fromJson(json);
    if (!document.isObject()) { result.valid = false; result.state = Ui::PageState::Error; result.errorCode = QStringLiteral("SIMULATED_MALFORMED_JSON"); result.message = QStringLiteral("模拟 JSON 格式错误"); return result; }
    result.valid = true; result.payload = document.object();
    if (scenario == MockScenario::Empty) { result.payload.insert(QStringLiteral("items"), QJsonArray{}); result.state = Ui::PageState::Empty; }
    return result;
}
MockResult MockDataProvider::load(UserMockDataset dataset, MockScenario scenario) const
{
    Q_INIT_RESOURCE(user_mock_resources);
    const QString path = dataset == UserMockDataset::Login ? QStringLiteral(":/mock/user/login.json") : dataset == UserMockDataset::Stations ? QStringLiteral(":/mock/user/stations.json") : QStringLiteral(":/mock/user/order.json");
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) { MockResult r; r.state = Ui::PageState::Error; r.errorCode = QStringLiteral("SIMULATED_RESOURCE_ERROR"); r.message = QStringLiteral("模拟 JSON 资源缺失"); return r; }
    return parse(file.readAll(), scenario);
}

PagePreview MockDataProvider::preview(PrimaryPage page) const
{
    PagePreview result;
    result.source = QStringLiteral("SIMULATED · 教学模拟数据\n数据时间：2026-09-06 09:00 UTC+08:00");
    switch (page) {
    case PrimaryPage::Home:
        result.title = QStringLiteral("EV FLOW · 充电服务");
        result.subtitle = QStringLiteral("欢迎回来，今天也从容出发");
        result.cards = {
            {QStringLiteral("当前位置"), QStringLiteral("广州 · 天河区（模拟位置）\n○ 未连接服务器，当前使用模拟数据")},
            {QStringLiteral("附近可用站点"), QStringLiteral("天河示范站 · 距离 0.6 km\n可用 3 / 共 4 桩 · 快充"), true},
            {QStringLiteral("活动订单摘要"), QStringLiteral("当前有一笔充电中订单（模拟）\n天河示范站 · 示例桩01")},
            {QStringLiteral("我的余额"), QStringLiteral("100.00 元 · 固定展示值")},
            {QStringLiteral("最近站点"), QStringLiteral("天河示范站 · 当前使用基础排序")}
        };
        break;
    case PrimaryPage::Stations:
        result.title = QStringLiteral("附近站点");
        result.subtitle = QStringLiteral("模拟位置：广州天河 · 当前使用基础排序");
        result.cards = {
            {QStringLiteral("天河示范站"), QStringLiteral("0.6 km · 快充 · 营业中\n可用 3 / 共 4 桩\n1.23 元/kWh（固定展示值）\n地址：示范路1号（模拟）")},
            {QStringLiteral("城市公园站"), QStringLiteral("1.2 km · 慢充 · 营业中\n可用 2 / 共 4 桩\n1.10 元/kWh（固定展示值）\n地址：公园路2号（模拟）")}
        };
        break;
    case PrimaryPage::Orders:
        result.title = QStringLiteral("活动订单");
        result.subtitle = QStringLiteral("订单状态仅为模拟预览，不执行恢复或充电操作");
        result.cards = {
            {QStringLiteral("● 充电中（模拟）"), QStringLiteral("天河示范站 · 示例桩01\n订单：MOCK-ORDER-01\n8.35 kWh · 10.27 元\n以上均为固定展示字符串")}
        };
        break;
    case PrimaryPage::History:
        result.title = QStringLiteral("充电记录");
        result.subtitle = QStringLiteral("最近的充电行程 · 模拟记录");
        result.cards = {
            {QStringLiteral("天河示范站 · 已完成"), QStringLiteral("2026-09-05 18:30 UTC+08:00\n示例桩01 · 35分钟\n8.35 kWh · 10.27 元")},
            {QStringLiteral("城市公园站 · 已完成"), QStringLiteral("2026-09-04 12:00 UTC+08:00\n示例桩02 · 60分钟\n6.00 kWh · 6.60 元")}
        };
        break;
    case PrimaryPage::Account:
        result.title = QStringLiteral("我的账户");
        result.subtitle = QStringLiteral("教学个人中心 · 无真实支付");
        result.cards = {
            {QStringLiteral("用户8000"), QStringLiteral("头像占位 · 138****8000\n账户正常（模拟）")},
            {QStringLiteral("账户余额"), QStringLiteral("100.00 元 · 固定展示值\n模拟充值入口将在后续工作包接入")},
            {QStringLiteral("数据说明"), QStringLiteral("当前资料来自界面模拟数据；未读取或修改服务器账户。")}
        };
        break;
    }
    return result;
}

PagePreview MockDataProvider::preview(SecondaryPage page) const
{
    PagePreview result;
    result.source = QStringLiteral("SIMULATED · 教学模拟数据\n数据时间：2026-09-06 09:00 UTC+08:00");
    switch (page) {
    case SecondaryPage::StationDetail:
        result.title = QStringLiteral("站点详情"); result.subtitle = QStringLiteral("纵向站点信息与电桩摘要（模拟）");
        result.cards = {{QStringLiteral("天河示范站"), QStringLiteral("地址：示范路1号 · 距离 0.6 km\n营业中 · 可用 3 / 共 4 桩")}, {QStringLiteral("服务与价格"), QStringLiteral("快充 · 1.23 元/kWh（固定展示值）\n导航与预约仅为界面模拟")}}; break;
    case SecondaryPage::Navigation:
        result.title = QStringLiteral("导航"); result.subtitle = QStringLiteral("外部地图不可用时的本地降级预览");
        result.cards = {{QStringLiteral("地图预览 · Normal"), QStringLiteral("天河示范站 · 距离 0.6 km\n示范路1号（模拟地图区域）")}, {QStringLiteral("△ 降级说明"), QStringLiteral("地图服务不可用。地址：示范路1号；坐标：23.1351, 113.3245（模拟）。仍可复制坐标并继续预约。")}}; break;
    case SecondaryPage::ReservationConfirm:
        result.title = QStringLiteral("预约确认"); result.subtitle = QStringLiteral("确认信息仅用于界面流程预览");
        result.cards = {{QStringLiteral("预约摘要"), QStringLiteral("天河示范站 · 示例桩01\n快充 · 60分钟有效期\n费用：固定展示字符串，不执行金额换算")}, {QStringLiteral("取消规则"), QStringLiteral("预约确认、取消和占桩结果均为模拟状态。")}}; break;
    case SecondaryPage::ChargingStatus:
        result.title = QStringLiteral("充电状态"); result.subtitle = QStringLiteral("当前充电状态仅为模拟展示");
        result.cards = {{QStringLiteral("● CHARGING · 充电中"), QStringLiteral("进度：68%\n已充电量：8.35 kWh\n费用：10.27 元（固定展示字符串）")}, {QStringLiteral("桩与连接"), QStringLiteral("天河示范站 · 示例桩01\n连接状态：○ 未连接服务器\n更新时间：2026-09-06 09:00")}}; break;
    case SecondaryPage::ActiveOrderRecovery:
        result.title = QStringLiteral("活动订单恢复"); result.subtitle = QStringLiteral("重新进入客户端后的活动订单模拟恢复");
        result.cards = {{QStringLiteral("发现未完成订单"), QStringLiteral("订单 MOCK-ORDER-01 · 天河示范站\n根据模拟状态进入充电或结算流程")}}; break;
    case SecondaryPage::Settlement:
        result.title = QStringLiteral("结算"); result.subtitle = QStringLiteral("纵向账单摘要 · 模拟支付");
        result.cards = {{QStringLiteral("充电已结束"), QStringLiteral("天河示范站 · 示例桩01\n时长：35分钟 · 电量：8.35 kWh")}, {QStringLiteral("应付金额"), QStringLiteral("10.27 元（固定展示字符串）\n支付成功与失败均不调用正式接口")}}; break;
    case SecondaryPage::Analysis:
        result.source.clear();
        result.title = QStringLiteral("智能推荐与预测");
        result.subtitle = QStringLiteral("分析服务尚未接入");
        result.state = Ui::PageState::Degraded;
        break;
    }
    return result;
}
