# TCP/JSON 通信协议（消息版本1.0 · 业务字段修订2.0）

## 1. 状态与传输边界

业务字段修订2.0是当前用于新开发和三端联调的字段约定。旧版整数分/Wh字段仅供迁移查阅，不能用于新请求。这里的文档修订号不是JSON消息的`version`值。

每条消息由“4 字节长度头 + JSON 正文”组成。长度头使用网络字节序无符号整数，仅表示正文的 UTF-8 字节数。正文最小 1 字节，最大 1 MiB。接收端必须缓存半包并连续拆分粘包；零长度或超长帧记为 `PROTOCOL_ERROR`，并清空该连接的待解析缓存。

**所有请求固定发送`"version":"1.0"`，金额、电量同时使用本文件规定的十进制字符串。** 两条规则同时生效，不需要客户端发送`"2.0"`。服务端当前会以`PARAM_INVALID`拒绝`version="2.0"`，同一连接之后仍可发送合法请求。不要只因文档修订号变化就修改消息版本；未来若需更改消息封装，必须另行协调所有发送端和接收端。

## 2. 请求与响应

请求固定字段：

```json
{"version":"1.0","requestId":"REQ-20260905-000001","action":"ORDER_TELEMETRY","payload":{"orderId":"ORD-1","energyKwh":"8.35"}}
```

成功响应复用原 `requestId`：

```json
{"requestId":"REQ-20260905-000001","ok":true,"code":"OK","message":"success","data":{"orderId":"ORD-1","energyKwh":"8.35"}}
```

失败响应也复用原 `requestId`：

```json
{"requestId":"REQ-20260905-000002","ok":false,"code":"PARAM_INVALID","message":"PARAM_INVALID","data":{},"error":{"code":"PARAM_INVALID","message":"PARAM_INVALID"}}
```

`requestId` 由调用方生成，并用于识别重复请求；动作名使用大写下划线。登录、预约、支付和充值直接使用外层 `requestId` 完成重复请求识别。

## 3. 十进制字段

新开始的充电订单在`ORDER_START`成功时锁定站点单价，返回的`unitPriceYuanPerKwh`即锁定值；停止结算采用该值，不受充电途中站点调价影响。升级前已处于充电中的订单没有可靠的起始单价记录，仍按原有停止时单价结算；不会猜测或补造历史价格。此规则不增加请求字段，不改变消息版本或状态名。

金额、单价和电量在 JSON 中必须使用普通十进制字符串。服务端接收后按“最接近值，正好一半时远离零”的规则保留两位；响应固定输出两位字符串。

| 字段 | 单位 | 请求示例 | 响应格式 |
| --- | --- | --- | --- |
| `energyKwh` | kWh | `"8.35"` | 固定两位字符串 |
| `amountYuan` | 元 | `"100.00"` | 固定两位字符串 |
| `priceYuanPerKwh` | 元/kWh | `"1.23"` | 固定两位字符串 |
| `unitPriceYuanPerKwh` | 元/kWh | 不作为当前请求输入 | 固定两位字符串 |
| `totalRevenueYuan` | 元 | 不作为请求输入 | 固定两位字符串 |

合法输入可以多于两位小数，统一舍入，例如 `"10.2705"` 输出 `"10.27"`，`"10.275"` 输出 `"10.28"`。数字类型 `8.35`、科学计数法 `"8.35e0"`、带单位文本 `"8.35kWh"`、其他非法文本和负数都返回 `PARAM_INVALID`。任何带负号的输入都按负值拒绝，包括舍入后会成为零的 `"-0.004"` 和 `"-0.00"`。一次参数错误不会关闭连接，调用方可在同一连接继续发送有效请求。

典型结算响应：

```json
{"energyKwh":"8.35","unitPriceYuanPerKwh":"1.23","amountYuan":"10.27"}
```

## 4. v1 到 v2 字段映射

| v1 历史字段 | v2 活动字段 | 换算 |
| --- | --- | --- |
| `energyWh` | `energyKwh` | Wh 除以 1000，按统一规则保留两位 |
| `balanceCents` | `balanceYuan` | 分除以 100 |
| `priceCentsPerKwh` | `priceYuanPerKwh` | 分/kWh 除以 100 |
| `unitPriceCentsPerKwh` | `unitPriceYuanPerKwh` | 分/kWh 除以 100 |
| `amountCents` | `amountYuan` | 分除以 100 |
| 整数 `amountYuan` | 字符串 `amountYuan` | 单位不变，类型改为十进制字符串 |
| `totalRevenueCents` | `totalRevenueYuan` | 分除以 100 |
| `revenueCents` | `revenueYuan` | 分除以 100 |
| `todayRevenueCents` | `todayRevenueYuan` | 分除以 100 |

v2 响应中的 `STATION_NEARBY.priceYuanPerKwh`、订单动作的 `energyKwh` / `unitPriceYuanPerKwh` / `amountYuan`、`ORDER_TELEMETRY.energyKwh`、`ADMIN_REVENUE.totalRevenueYuan`、营收趋势的 `revenueYuan` 和总览的 `todayRevenueYuan` 均为固定两位字符串。

后台列表中的十进制字段同样使用固定两位字符串：`ADMIN_STATIONS.rows[].priceYuanPerKwh` 的单位为元/kWh，`ADMIN_ORDERS.rows[].energyKwh` 的单位为 kWh，`ADMIN_ORDERS.rows[].amountYuan` 和 `ADMIN_USERS.rows[].balanceYuan` 的单位为元。

## 5. 业务动作

| 动作 | 请求 `payload` | 成功响应 `data` |
| --- | --- | --- |
| `USER_LOGIN` | `phone` | `userId`、`nickname`、`restricted` |
| `USER_PROFILE` | `userId` | `userId`、`phoneMasked`、`nickname`、`balanceYuan`、`status` |
| `USER_PROFILE_UPDATE` | `userId`、`nickname`、可选 `avatarPath` | `userId`、去除首尾空格后的 `nickname` |
| `WALLET_BALANCE` | `userId` | `userId`、`balanceYuan` |
| `STATION_NEARBY` | `latitude`、`longitude` | `stations`，单价使用 `priceYuanPerKwh` |
| `STATION_DETAIL` | `stationId` | 站点字段及 `piles`，包含每个电桩的编号、类型、功率、状态和心跳时间 |
| `ORDER_RESERVE` | `userId`、`pileId` | `orderId` |
| `ORDER_START` / `ORDER_STOP` / `ORDER_CANCEL` | `orderId` | `orderId`、`energyKwh`、`unitPriceYuanPerKwh`、`amountYuan` |
| `ORDER_TELEMETRY` | `orderId`、`energyKwh` | `orderId`、`energyKwh` |
| `ORDER_ACTIVE` | `userId` | `order`；没有活动订单时为空对象 |
| `ORDER_DETAIL` | `orderId` | 状态、时间、`energyKwh`、`unitPriceYuanPerKwh`、`amountYuan` |
| `ORDER_HISTORY` | `userId`、可选 `limit`、`offset` | 已完成、已取消和已过期订单 `rows` |
| `PILE_TELEMETRY` | `pileId`、`recordedAt`、`temperatureC`、`voltageV`、`currentA`、`powerW`、`energyKwh` | 接受后的桩号、时间和累计电量 |
| `WALLET_PAY` | `orderId` | 空对象 |
| `WALLET_RECHARGE` | `userId`、`amountYuan` | 空对象 |
| `ADMIN_LOGIN` | `username`、`password` | `adminToken` |
| `ADMIN_REVENUE` | `adminToken` | `totalRevenueYuan` |
| `ADMIN_REVENUE_TREND` | `adminToken`、`days`、可选 `stationId` | `points`，金额使用 `revenueYuan` |
| `ADMIN_OVERVIEW` | `adminToken` | 今日营收使用 `todayRevenueYuan` |
| `ADMIN_STATIONS` / `ADMIN_PILES` / `ADMIN_ORDERS` / `ADMIN_USERS` / `ADMIN_LOGS` | `adminToken` 和可选筛选、`limit`、`offset` | `rows` |
| `ADMIN_ALARMS` | `adminToken`、可选 `status`、`severity`、`limit`、`offset` | 告警 `rows` |
| `ADMIN_TICKETS` | `adminToken`、可选 `status`、`limit`、`offset` | 工单 `rows` |
| `ADMIN_USER_FREEZE` | `adminToken`、`userId`、`frozen`、`reason` | 空对象 |
| `ADMIN_ALARM_ACK` | `adminToken`、`alarmId` | 空对象 |
| `ADMIN_TICKET_START` | `adminToken`、`ticketId`、`assignee` | 空对象 |
| `ADMIN_TICKET_RESOLVE` | `adminToken`、`ticketId`、`notes` | 空对象 |
| `ADMIN_TICKET_CLOSE` | `adminToken`、`ticketId` | 空对象 |
| `ADMIN_STATION_CREATE` | `adminToken`、站点字段、`priceYuanPerKwh` 和 `piles` | `stationId` |
| `ADMIN_STATION_UPDATE` | `adminToken`、`stationId`、完整站点字段、`priceYuanPerKwh`、`status` | `stationId` |
| `ADMIN_PILE_SET_STATUS` | `adminToken`、`pileId`、`status`、`reason` | `pileId`、更新后状态 |
| `ADMIN_PILE_RESTART` | `adminToken`、`pileId` | `pileId`、按活动订单恢复后的状态和 `commandId` |

除 `ADMIN_LOGIN` 外，全部 `ADMIN_*` 动作必须携带 `payload.adminToken`。管理员身份由凭证确定，不接受调用方自行填写。

站点修改、电桩状态修改和模拟重启均与一条`operation_log`记录在同一事务提交。人工设置电桩状态只接受`IDLE`、`FAULT`、`OFFLINE`；处于预约或充电中的电桩不能被直接改写。模拟重启只接受故障或离线电桩；有活动订单时恢复为`RESERVED`或`CHARGING`，否则恢复为`IDLE`，并返回本次命令编号。

### 5.1 电桩遥测约束

`PILE_TELEMETRY.recordedAt` 使用带时区的 ISO 8601 时间。温度范围为 -60～200 ℃，电压和电流范围均为 0～1000，`powerW` 必须是 0～1000000 的整数，`energyKwh` 必须是非负十进制字符串。记录时间不能比服务端当前时间晚超过5分钟，同一电桩的记录时间和累计电量都不能倒退。

每次有效遥测会在同一事务中写入明细，并以服务端接收时间刷新心跳；设备上报时间仅用于明细排序和防倒退。温度达到70 ℃时，充电桩进入 `FAULT`，同时创建一条 `OVERHEAT` 告警和对应工单；同一电桩同一类型的未关闭告警只保留一条。后台每5秒检查一次心跳，连续15秒未上报的电桩进入 `OFFLINE`，并创建 `OFFLINE` 告警和工单。离线设备重新上报，或高温设备重新上报低于阈值的正常遥测后，根据活动订单恢复为 `RESERVED`、`CHARGING` 或 `IDLE`。

### 5.2 告警与工单状态

- 告警只能按 `OPEN → ACKNOWLEDGED → CLOSED` 推进。
- 工单只能按 `OPEN → PROCESSING → RESOLVED → CLOSED` 推进。
- 开始处理必须填写负责人，解决工单必须填写处理说明。
- 工单关闭前，告警必须已确认，设备必须不再处于 `FAULT` 或 `OFFLINE`；关闭工单与关闭告警在同一事务完成。
- 状态跳步或重复操作返回 `STATE_CONFLICT`，设备尚未恢复返回 `DEVICE_NOT_RECOVERED`。

## 6. 固定错误码

| 错误码 | 含义 | 调用方处理 |
| --- | --- | --- |
| `OK` | 成功 | 正常读取 `data` |
| `AUTH_INVALID` | 身份无效 | 返回登录页 |
| `PARAM_INVALID` | 参数、十进制表示或动作无效 | 保留页面并提示修改输入 |
| `USER_FROZEN` | 用户冻结 | 禁止创建新业务 |
| `UNFINISHED_ORDER` | 存在未完成订单 | 跳转活动订单 |
| `PILE_UNAVAILABLE` | 电桩不可用 | 刷新桩状态 |
| `ORDER_STATE_CONFLICT` | 订单状态冲突 | 重新查询权威状态 |
| `BALANCE_INSUFFICIENT` | 余额不足 | 引导模拟充值 |
| `DUPLICATE_REQUEST` | 重复请求 | 查询原请求结果，不重复执行 |
| `MAP_UNAVAILABLE` | 地图不可用 | 显示地址和坐标降级 |
| `DEVICE_TIMEOUT` | 设备超时 | 标记离线并允许重试 |
| `TELEMETRY_OUT_OF_ORDER` | 遥测时间或累计电量倒退 | 丢弃本条并检查设备时钟或累计值 |
| `STATE_CONFLICT` | 告警或工单状态跳转不合法 | 刷新当前状态后按顺序操作 |
| `DEVICE_NOT_RECOVERED` | 设备仍处于故障或离线状态 | 恢复设备后再关闭工单 |
| `NOT_FOUND` | 指定用户、站点、订单、电桩等不存在 | 刷新列表并停止操作 |
| `DATABASE_ERROR` | 数据库错误 | 结束当前事务并记录日志 |
| `MODEL_UNAVAILABLE` | 分析服务不可用 | 使用本地基础推荐 |
| `PROTOCOL_ERROR` | 帧或 JSON 不合法 | 拒绝本帧；必要时断开连接 |

## 7. 成员2、3迁移要求

成员2客户端须停止发送旧整数和 JSON 数字，改为发送普通十进制字符串，并把上述响应字段按元、元/kWh、kWh 显示；不得再次进行分或 Wh 换算。成员3分析端须切换到 v2 的元和 kWh 字段，按字符串读取后使用十进制类型计算，不能用二进制浮点解析金额。

三端联调必须覆盖 `8.35 kWh × 1.23 元/kWh = 10.27 元`、充值、支付重复请求、营收查询及非法表示后连接复用。成员2、3完成前，不得把团队整体迁移标记为完成。
