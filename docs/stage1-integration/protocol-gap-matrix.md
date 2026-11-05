# 第一阶段协议与功能差异矩阵

## 1. 已有动作

| 业务 | 已有动作 | 第一阶段用途 |
| --- | --- | --- |
| 用户 | `USER_LOGIN` | 登录或自动注册 |
| 站点 | `STATION_NEARBY` | 查询附近站点和单价 |
| 订单 | `ORDER_RESERVE`、`ORDER_START`、`ORDER_STOP`、`ORDER_CANCEL` | 预约和充电状态流转 |
| 遥测 | `ORDER_TELEMETRY`、`PILE_TELEMETRY` | 更新订单电量和桩遥测 |
| 钱包 | `WALLET_RECHARGE`、`WALLET_PAY` | 模拟充值和支付 |
| 管理登录 | `ADMIN_LOGIN` | 获取管理员令牌 |
| 管理查询 | `ADMIN_OVERVIEW`、`ADMIN_REVENUE`、`ADMIN_REVENUE_TREND` | 概览和营收 |
| 管理列表 | `ADMIN_STATIONS`、`ADMIN_PILES`、`ADMIN_ORDERS`、`ADMIN_USERS`、`ADMIN_LOGS` | 后台列表 |
| 管理操作 | `ADMIN_USER_FREEZE`、`ADMIN_ALARM_ACK`、`ADMIN_TICKET_START`、`ADMIN_TICKET_RESOLVE`、`ADMIN_TICKET_CLOSE`、`ADMIN_STATION_CREATE` | 用户、告警、工单和站点操作 |

## 2. 必须补齐或确认的动作

| 动作 | 请求关键字段 | 响应关键字段 | 负责人 | 验收方式 |
| --- | --- | --- | --- | --- |
| `USER_PROFILE` | `userId` | 昵称、手机号掩码、状态、`balanceYuan` | 成员1 | 登录后账户页显示数据库数据 |
| `USER_PROFILE_UPDATE` | `userId`、昵称、可选头像路径 | 更新后的用户ID和昵称 | 成员1 | 修改昵称后重新查询仍能恢复 |
| `WALLET_BALANCE` | `userId` | `balanceYuan` | 成员1 | 充值和支付后重新查询余额 |
| `STATION_DETAIL` | `stationId` | 地址、坐标、价格、电桩列表和状态 | 成员1 | 站点详情不使用本地卡片 |
| `ORDER_ACTIVE` | `userId` | 活动订单或空 | 成员1 | 重启客户端恢复订单 |
| `ORDER_DETAIL` | `orderId` | 状态、时间、电量、单价、金额 | 成员1 | 充电页定时刷新 |
| `ORDER_HISTORY` | `userId`、分页 | 历史订单列表 | 成员1 | 支付后在记录页查询到订单 |
| `ADMIN_ALARMS` | `adminToken`、筛选和分页 | 告警列表 | 成员1 | 过热后运营端出现告警 |
| `ADMIN_TICKETS` | `adminToken`、筛选和分页 | 工单列表 | 成员1 | 告警处置后显示工单状态 |
| `ADMIN_STATION_UPDATE` | `adminToken`、站点字段 | 更新后的站点 | 成员1 | 运营端修改后车主端重查可见 |
| `ADMIN_PILE_SET_STATUS` | `adminToken`、`pileId`、目标状态 | 更新后的电桩状态 | 成员1 | 禁用后不能预约 |
| `ADMIN_PILE_RESTART` | `adminToken`、`pileId` | 模拟命令编号和结果 | 成员1 | 运营端显示成功或失败及日志 |

## 3. 成员2接入要求

| 页面 | 正式数据动作 | 当前状态 | 第一阶段目标 |
| --- | --- | --- | --- |
| 登录 | `USER_LOGIN` | 无实际登录 | 完成手机号输入、请求和会话保存 |
| 首页/账户 | `USER_PROFILE`、`WALLET_BALANCE` | 固定100.00元 | 显示数据库余额和用户状态 |
| 附近站点 | `STATION_NEARBY` | 固定JSON | 查询数据库站点 |
| 站点详情 | `STATION_DETAIL` | 固定卡片 | 显示数据库电桩及状态 |
| 预约 | `ORDER_RESERVE` | 只跳转 | 实际创建预约订单 |
| 活动订单 | `ORDER_ACTIVE`、`ORDER_DETAIL` | 固定订单 | 恢复和刷新数据库订单 |
| 充电 | `ORDER_START`、`ORDER_STOP` | 只跳转 | 实际改变状态并显示遥测 |
| 充值/支付 | `WALLET_RECHARGE`、`WALLET_PAY` | 无真实扣款 | 实际改变模拟余额并防重 |
| 历史 | `ORDER_HISTORY` | 固定记录 | 查询数据库记录 |
| 运营端 | 全部`ADMIN_*` | 页面内容有限 | 登录、查询、操作和反馈全部接入 |

## 4. 成员3接入要求

| 范围 | 当前差异 | 第一阶段目标 |
| --- | --- | --- |
| 桩号 | 类似`PILE-001`字符串 | 映射为服务端可识别的正整数`pileId` |
| 消息类型 | 存在`PILE_HEARTBEAT` | 统一使用服务端支持的`PILE_TELEMETRY`，或由成员1明确增加心跳动作 |
| 遥测字段 | 缺少部分时间、电压、电流 | 完整发送协议要求字段 |
| 分析来源 | 主要读取独立CSV | 增加SQLite只读适配器 |
| Web大屏 | 展示分析样例 | 读取同一模拟数据库的订单、营收、遥测和告警 |

任何字段变更先修改本文并由成员1确认，再修改代码和测试。
