# 第一阶段联调交付样例（成员2 / 成员3）

整理日期：2026-09-09。对应基线：`feature/member1-stage1-integration` @ `f428fbc`。
本文件是对 `member1-tasks.md` 中两项交付的补齐：

- [ ] 向成员2提供完整请求、成功响应和失败响应样例 → 见第 2 节；
- [ ] 向成员3提供合法 `PILE_TELEMETRY` 样例和桩号映射 → 见第 3 节。

样例依据 `server/network/request_router.cpp` 实际代码整理；协议细则以 `docs/protocol.md` 为准，两者冲突时以代码为准并先修订文档。

## 1. 所有动作的公共规则

- 传输：TCP 48080，每帧 = 4 字节大端长度头 + JSON 正文（UTF-8）。
- 请求外壳：`{"version":"1.0","requestId":"<全局唯一>","action":"<动作名>","payload":{...}}`。
- `requestId` 必须唯一；`USER_LOGIN / ORDER_RESERVE / WALLET_RECHARGE / WALLET_PAY` 兼做防重键：同编号且参数一致 → 复放原成功结果，参数不一致 → `DUPLICATE_REQUEST`。其余动作同编号重发按普通请求处理（工单等状态动作会返回状态冲突）。
- 成功响应：`{"requestId":"...","ok":true,"code":"OK","message":"success","data":{...}}`。
- 失败响应：`{"requestId":"...","ok":false,"code":"<规范错误码>","message":"<服务层原始错误码>","error":{"code":"...","message":"..."}}`。注意：`code` 是统一错误码（如 `AUTH_INVALID`），`message` 保留服务层原始码（如 `AUTH_FAILED`）；客户端只应依赖 `code` / `error.code`，不要解析中文文本。
- 金额/电量/单价一律是**字符串**两位小数（如 `"1.23"`），禁止 JSON 数字。
- 除 `ADMIN_LOGIN` 外，所有 `ADMIN_*` 动作必须在 payload 带 `adminToken`（管理员登录获得，8 小时有效）。

## 2. 成员2（Qt 车主端 / 运营端）动作样例

### 2.1 USER_LOGIN（登录或自动注册；新号赠 100.00 元）

```json
请求  {"version":"1.0","requestId":"M2-001","action":"USER_LOGIN","payload":{"phone":"13800001234"}}
成功  {"requestId":"M2-001","ok":true,"code":"OK","message":"success",
      "data":{"userId":1021,"nickname":"用户1234","restricted":false}}
失败  {"requestId":"M2-001","ok":false,"code":"PARAM_INVALID","message":"PARAM_INVALID",
      "error":{"code":"PARAM_INVALID","message":"PARAM_INVALID"}}
```

失败码：`PARAM_INVALID`（手机号非 `1[3-9]` 开头 11 位）、`DUPLICATE_REQUEST`。`restricted=true` 表示账号被冻结，可登录查询但业务操作会被拒。

### 2.2 USER_PROFILE（账户页；手机号打码）

```json
请求  {"action":"USER_PROFILE","payload":{"userId":1021}}
成功  {"ok":true,"data":{"userId":1021,"phoneMasked":"138****1234","nickname":"用户1234",
      "balanceYuan":"100.00","status":"NORMAL"}}
失败  PARAM_INVALID / NOT_FOUND
```

### 2.3 USER_PROFILE_UPDATE（改昵称，可选头像路径）

```json
请求  {"action":"USER_PROFILE_UPDATE","payload":{"userId":1021,"nickname":"小充"}}
成功  {"ok":true,"data":{"userId":1021,"nickname":"小充"}}
失败  PARAM_INVALID（昵称去空格后须 2~20 字符；avatarPath 须为服务端本地 PNG/JPEG 且 ≤2MiB）
```

### 2.4 WALLET_BALANCE（充值/支付后刷新余额）

```json
请求  {"action":"WALLET_BALANCE","payload":{"userId":1021}}
成功  {"ok":true,"data":{"userId":1021,"balanceYuan":"98.77"}}
```

### 2.5 STATION_NEARBY（附近站点，按球面距离升序）

```json
请求  {"action":"STATION_NEARBY","payload":{"latitude":24.48,"longitude":118.10}}
成功  {"ok":true,"data":{"stations":[
      {"stationId":1,"name":"滨海示范站","address":"滨海路1号","latitude":24.4798,
       "longitude":118.0894,"priceYuanPerKwh":"1.20","totalPiles":4,
       "availablePiles":4,"distanceKm":1.57}]}}
失败  PARAM_INVALID（纬度±90、经度±180）
```

### 2.6 STATION_DETAIL（站点详情，含电桩列表）

```json
请求  {"action":"STATION_DETAIL","payload":{"stationId":1}}
成功  {"ok":true,"data":{"stationId":1,"name":"滨海示范站","address":"滨海路1号",
      "latitude":24.4798,"longitude":118.0894,"priceYuanPerKwh":"1.20","status":"OPEN",
      "piles":[{"pileId":101,"serial":"PILE-DEMO-01","chargeType":"FAST",
                "ratedPowerKw":60.0,"status":"IDLE","lastHeartbeatAt":null}]}}
失败  PARAM_INVALID / NOT_FOUND
```

### 2.7 ORDER_RESERVE（预约；15 分钟期限）

```json
请求  {"action":"ORDER_RESERVE","payload":{"userId":1021,"pileId":101}}
成功  {"ok":true,"data":{"orderId":"ORD-3f2a..."}}
失败  USER_FROZEN / UNFINISHED_ORDER（本人已有活动订单）/
      PILE_UNAVAILABLE（非 IDLE、被占用或站点停运）/ DUPLICATE_REQUEST
```

### 2.8 ORDER_START / ORDER_STOP / ORDER_CANCEL（三动作同构，payload 均为 `{"orderId":"..."}`）

```json
START 成功 {"ok":true,"data":{"orderId":"ORD-...","energyKwh":"0.00",
           "unitPriceYuanPerKwh":"1.23","amountYuan":"0.00"}}
STOP  成功 {"ok":true,"data":{"orderId":"ORD-...","energyKwh":"1.00",
           "unitPriceYuanPerKwh":"1.23","amountYuan":"1.23"}}
失败  ORDER_STATE_CONFLICT（状态不符：开始须 RESERVED、停止须 CHARGING、取消须 RESERVED）、
      USER_FROZEN / PILE_UNAVAILABLE（仅 START）
```

开始充电即锁定单价快照（`unitPriceYuanPerKwh`），之后站点改价不影响本单结算；正常桩在停止后保持占用，支付成功才释放。

### 2.9 ORDER_ACTIVE（恢复页面）/ ORDER_DETAIL（刷新详情）/ ORDER_HISTORY（记录页）

```json
ACTIVE 请求 {"userId":1021}
成功   {"ok":true,"data":{"order":{...订单对象}}}   // 无活动订单时 data.order 为空对象 {}
DETAIL 请求 {"orderId":"ORD-..."}
成功   {"ok":true,"data":{"orderId":"ORD-...","userId":1021,"pileId":101,
        "status":"FINISHED","energyKwh":"1.00","unitPriceYuanPerKwh":"1.23",
        "amountYuan":"1.23","reservedUntil":"...","startedAt":"...","endedAt":"...",
        "createdAt":"..."}}
HISTORY 请求 {"userId":1021,"limit":20,"offset":0}   // limit 1~100
成功   {"ok":true,"data":{"rows":[<同 DETAIL 的订单对象>]}}
```

### 2.10 WALLET_RECHARGE（充值 1.00~10000.00）/ WALLET_PAY（支付）

```json
RECHARGE 请求 {"userId":1021,"amountYuan":"50.00"}        // 成功 data 为空对象 {}
PAY     请求 {"orderId":"ORD-..."}                         // 成功 data 为空对象 {}
RECHARGE 失败 PARAM_INVALID（越界/格式）/ DUPLICATE_REQUEST
PAY     失败 ORDER_STATE_CONFLICT（非待支付）/ BALANCE_INSUFFICIENT / DUPLICATE_REQUEST
```

余额不足时整体回滚，不产生任何成功流水；重复点击支付请**复用同一 requestId**，服务端保证只扣一次。

### 2.11 运营端要点

- 全部 `ADMIN_*` 动作先 `ADMIN_LOGIN`（`{"username":"admin","password":"123456"}` 演示账号）→ `data.adminToken`，之后每个动作 payload 带上 `adminToken`。
- 查询类：`ADMIN_OVERVIEW`、`ADMIN_REVENUE`、`ADMIN_REVENUE_TREND`（`days` 1~366、可选 `stationId`）、`ADMIN_STATIONS`（`search`）、`ADMIN_PILES`（`stationId`、`status`）、`ADMIN_ORDERS`（`status`）、`ADMIN_USERS`（`status`、`search`）、`ADMIN_LOGS`（`action`）、`ADMIN_ALARMS`（`status`、`severity`）、`ADMIN_TICKETS`（`status`）；均支持 `limit`（1~500）/`offset`，返回 `data.rows`。
- 操作类：`ADMIN_USER_FREEZE`（`userId, frozen, reason` 必填）、`ADMIN_ALARM_ACK`（`alarmId`）、`ADMIN_TICKET_START`（`ticketId, assignee` 必填）、`ADMIN_TICKET_RESOLVE`（`ticketId, notes` 必填）、`ADMIN_TICKET_CLOSE`（`ticketId`；要求告警已确认、说明非空、电桩非 FAULT/OFFLINE，否则 `STATE_CONFLICT` / `DEVICE_NOT_RECOVERED`）、`ADMIN_STATION_CREATE / UPDATE`、`ADMIN_PILE_SET_STATUS`（仅 `IDLE/FAULT/OFFLINE`，预约/充电中的桩拒绝）、`ADMIN_PILE_RESTART`（返回 `commandId` 与恢复后状态）。所有写操作自动写 `operation_log`。

## 3. 成员3（桩模拟器 / 分析 / 大屏）交付

### 3.1 桩号 → pileId 映射（演示种子库）

协议**只认正整数 `pileId`**，`serial`（pile_sn）仅是展示字段。演示种子（`database/seeds/demo.sql`）的固定映射：

| 桩号（pile_sn） | pileId | 所属站点 stationId | 类型 / 额定功率 |
| --- | --- | --- | --- |
| PILE-DEMO-01 ~ 04 | 101 ~ 104 | 1（滨海示范站） | n%3==0 为 SLOW/7kW，其余 FAST/60kW |
| PILE-DEMO-05 ~ 08 | 105 ~ 108 | 2（软件园站） | 同上规则 |
| PILE-DEMO-09 ~ 12 | 109 ~ 112 | 3（会展中心站） | 同上规则 |

- 快速查询映射：`SELECT id, pile_sn, station_id, status FROM pile_info ORDER BY id;`
- 演示用户：手机号 `13800000001` ~ `13800000020`（userId 1001~1020，余额 105.00~195.00）。
- 新建站点产生的桩以 `ADMIN_STATION_CREATE` 响应与 `ADMIN_PILES` 查询结果为准；模拟器不要自行拼接桩号映射。

### 3.2 合法 PILE_TELEMETRY 样例

```json
{"version":"1.0","requestId":"M3-T-001","action":"PILE_TELEMETRY","payload":{
    "pileId": 101,
    "recordedAt": "2026-09-09T08:30:00Z",
    "temperatureC": 55.5,
    "voltageV": 220.0,
    "currentA": 16.0,
    "powerW": 3520,
    "energyKwh": "10.27"
}}
```

成功响应：`{"ok":true,"data":{"pileId":101,"recordedAt":"2026-09-09T08:30:00Z","energyKwh":"10.27"}}`

**约束（违反即 PARAM_INVALID / TELEMETRY_OUT_OF_ORDER）：**

- `recordedAt` 必须是**带时区**的 ISO 8601（建议 UTC `Z` 结尾）；不得超前服务端时间 5 分钟。
- 范围：温度 -60~200℃；电压、电流 0~1000；功率 0~1000000 且为**整数**（JSON 数字，不带小数）；`energyKwh` 为非负十进制**字符串**。
- 同一桩的 `recordedAt` 与 `energyKwh` 都必须**单调不减**（乱序/倒退整条拒绝）。
- 温度 ≥70℃：桩转 FAULT，并生成（或复用）一条高温告警 + 工单；重复上报不会产生告警风暴。
- 心跳：**没有独立心跳动作**，`PILE_TELEMETRY` 即刷新心跳；15 秒无上报判离线（从未上报的桩不参与判离线）。
- 重要：`PILE_TELEMETRY` **不会**更新订单电量。要推进 `charge_order.energy_kwh`（车主端显示电量/费用），必须由模拟器侧对充电中的订单另行调用 `ORDER_TELEMETRY`（`orderId` + 递增 `energyKwh` 字符串），或与成员1确认新的联动方案后再改代码。
