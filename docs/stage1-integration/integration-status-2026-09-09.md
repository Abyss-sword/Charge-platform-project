# Stage1 Integration Status - 2026-09-09

> 本文档用于记录 Stage1 当前联调状态、Member2 已完成内容、跨成员依赖及后续联调前置条件。
>
> 本文档是阶段性状态快照，不修改既有 Stage1 baseline 定义。

## 1. Current Summary

当前 Stage1 状态：

- Member2：READY FOR INTEGRATION
- Member1 gaps：BLOCKING
- Member3 integration：UNKNOWN / PENDING CONFIRMATION
- Full Stage1 E2E：NOT YET COMPLETE

Member2 当前可独立完成的 Qt 客户端正式链路已基本完成。

剩余未完成项主要依赖：

1. Member1 补齐正式 protocol / server action；
2. Member1 提供统一数据库与 reset/seed 方式；
3. Member3 提供 simulator / FastAPI / Dashboard 实际联调证据；
4. 三端基于同一 SQLite 完成最终 E2E。

---

## 2. Member2 Completed

### 2.1 Shared Network Infrastructure

已完成：

- `ServiceClient` 迁移至 `clients/shared/network/`
- User/Admin 共用正式 TCP Client
- 4-byte network-order length prefix
- JSON request / response
- `requestId` 匹配
- timeout 处理
- disconnect 处理
- malformed response 处理
- stale data 保留
- reconnect 后重新查询
- 写请求不自动 replay

正常运行默认使用 Remote Provider。

仅在显式设置：

```bash
OFFLINE_DEMO=1
```

时启用 Mock Provider。

网络失败不会自动降级为 Mock success。

## 3. Owner Qt Client

当前已接入正式服务端 action：

- USER_LOGIN
- STATION_NEARBY
- ORDER_RESERVE
- ORDER_CANCEL
- ORDER_START
- ORDER_STOP
- WALLET_RECHARGE
- WALLET_PAY

已完成的客户端行为包括：

- 正式登录
- userId session 保存
- logout 后清理旧 session
- 再登录创建新 UserContext
- 找桩查询
- 正式预约
- 取消预约
- 开始充电
- 停止充电
- 结算流程
- 充值请求
- 支付请求
- 支付期间防止重复点击
- Server 业务错误展示
- 断线后保留最后成功数据
- stale 状态提示
- 断线时禁止写操作
- 重连后重新查询可恢复页面

## 4. Admin Qt Client

当前已接入正式服务端 action：

- ADMIN_LOGIN
- ADMIN_OVERVIEW
- ADMIN_REVENUE
- ADMIN_STATIONS
- ADMIN_PILES
- ADMIN_ORDERS
- ADMIN_USERS
- ADMIN_LOGS
- ADMIN_STATION_CREATE
- ADMIN_USER_FREEZE
- User unfreeze flow

已完成：

- adminToken session 管理
- Admin 正式登录
- Overview 查询
- Revenue 查询
- Stations 查询
- Piles 查询
- Orders 查询
- Users 查询
- Logs 查询
- 新建站点
- 冻结 / 解冻用户
- 写操作成功后重新查询服务端
- 不依赖本地表格直接修改模拟成功
- offline / stale UI
- disconnect 后禁用写操作
- reconnect 自动刷新正式查询页

## 5. Current Member1 Blockers

以下功能目前因正式 action 尚未完整提供而阻塞 Member2：

### Owner

- USER_PROFILE
- WALLET_BALANCE
- STATION_DETAIL
- ORDER_ACTIVE
- ORDER_DETAIL
- ORDER_HISTORY

因此当前无法完整实现：

- 首页真实用户资料
- 实时余额重新查询
- 真实站点详情
- 服务端电桩状态列表
- 客户端重启后恢复活动订单
- 充电过程中周期查询订单详情
- 服务端历史订单页面
- timeout 后基于订单状态安全恢复部分流程

### Admin

- ADMIN_ALARMS
- ADMIN_TICKETS
- ADMIN_STATION_UPDATE
- ADMIN_PILE_SET_STATUS
- ADMIN_PILE_RESTART

因此当前无法形成完整的：

- Alarm 查询 → ACK
- Ticket 查询 → Start → Resolve → Close
- 正式站点修改
- 电桩启用 / 禁用
- 电桩重启

这些项在正式 action 冻结前不应由 Qt 客户端自行定义协议或模拟服务端业务规则。

## 6. Member1 Integration Requirements

下一阶段需要 Member1 确认或提供：

### Protocol

冻结以下 action：

- USER_PROFILE
- WALLET_BALANCE
- STATION_DETAIL
- ORDER_ACTIVE
- ORDER_DETAIL
- ORDER_HISTORY
- ADMIN_ALARMS
- ADMIN_TICKETS
- ADMIN_STATION_UPDATE
- ADMIN_PILE_SET_STATUS
- ADMIN_PILE_RESTART

每个 action 应提供：

- request payload
- success response
- failure response
- error code
- qint64 / decimal 等字段类型约束

### Runtime

需要统一确认：

- Server 地址
- Server 端口
- protocol version
- SQLite 唯一路径
- User 演示账号
- Admin 演示账号

### Database

需要提供可重复执行的：

- reset
- seed

流程。

要求：

- 幂等
- 可重复
- 团队统一
- 不允许三位成员分别硬编码不同数据库路径

### Server Business Rules

需要继续保证：

- 业务规则只存在于服务端
- 支付幂等
- 写库失败事务回滚
- 预约竞争处理
- 禁用电桩后预约返回明确错误
- 金额使用统一 decimal 规则

## 7. Member3 Confirmation Required

当前 feature 无法直接确认 Member3 实现状态。

下一次联调需要 Member3 提供以下信息。

### Simulator

确认：

- simulator 所在分支
- PILE_TELEMETRY 已真实发送
- 使用正式 4-byte framing
- pileId 为正式数据库正整数 ID
- 可以稳定产生正常遥测
- 可以稳定产生过热场景
- 可以产生 timeout / command failure 场景

### FastAPI

确认：

- FastAPI 读取与 C++ Server 相同 SQLite
- 使用统一配置
- 只读数据库
- 不复制独立业务状态
- 能读取 telemetry / alarm / order 等正式记录

### Dashboard

确认：

- Dashboard 不再只依赖独立 CSV
- 通过 FastAPI 读取同一数据库数据
- 约 2~3 秒自动刷新
- 能展示真实 telemetry
- 能展示 alarm 状态变化

### Required Evidence

建议提供：

- simulator 正常场景截图
- overheating 场景截图
- telemetry 数据库记录
- FastAPI 查询结果
- Dashboard 页面
- alarm 状态变化
- 测试结果
- 演示录屏或关键流程截图

## 8. Cross-Member Dependency Matrix

| Item | Member1 | Member2 | Member3 | Current Status |
| --- | --- | --- | --- | --- |
| USER_LOGIN | PASS | PASS | N/A | PASS |
| STATION_NEARBY | PASS | PASS | N/A | PASS |
| STATION_DETAIL | BLOCKING | BLOCKED | N/A | BLOCKED |
| ORDER_RESERVE | PASS | PASS | N/A | PASS |
| ORDER_CANCEL | PASS | PASS | N/A | PASS |
| ORDER_START | PASS | PASS | N/A | PASS |
| ORDER_STOP | PASS | PASS | N/A | PASS |
| WALLET_RECHARGE | PASS | PASS | N/A | PASS |
| WALLET_PAY | PASS | PASS | N/A | PASS |
| ORDER_ACTIVE | BLOCKING | BLOCKED | N/A | BLOCKED |
| ORDER_DETAIL | BLOCKING | BLOCKED | N/A | BLOCKED |
| ORDER_HISTORY | BLOCKING | BLOCKED | N/A | BLOCKED |
| ADMIN_OVERVIEW | PASS | PASS | N/A | PASS |
| ADMIN_REVENUE | PASS | PASS | N/A | PASS |
| ADMIN_STATIONS | PASS | PASS | N/A | PASS |
| ADMIN_PILES | PASS | PASS | N/A | PASS |
| ADMIN_ORDERS | PASS | PASS | N/A | PASS |
| ADMIN_USERS | PASS | PASS | N/A | PASS |
| ADMIN_LOGS | PASS | PASS | N/A | PASS |
| ADMIN_STATION_CREATE | PASS | PASS | N/A | PASS |
| ADMIN_USER_FREEZE | PASS | PASS | N/A | PASS |
| ADMIN_ALARMS | BLOCKING | BLOCKED | UNKNOWN | BLOCKED |
| ADMIN_TICKETS | BLOCKING | BLOCKED | UNKNOWN | BLOCKED |
| PILE_TELEMETRY | PASS server-side | N/A | UNKNOWN | PARTIAL |
| SQLite Unified Config | PARTIAL | PASS client-side | UNKNOWN | PARTIAL |
| FastAPI | prerequisite available | N/A | UNKNOWN | UNKNOWN |
| Dashboard | N/A | N/A | UNKNOWN | UNKNOWN |
| Overheat → Alarm | PARTIAL | BLOCKED UI query | UNKNOWN | BLOCKED |
| Full Stage1 E2E | PARTIAL | PARTIAL | UNKNOWN | PARTIAL |

## 9. UI Acceptance Items Requiring Team Confirmation

### 9.1 Owner Window Size

Stage1 baseline mentions validation at:

1024 × 680

Current owner client design uses a portrait-oriented layout with a larger minimum height.

需要团队确认：

- 1024×680 是否仍为强制验收尺寸；
- 或是否以当前车主端 UI baseline 为准。

在确认前不建议为了单一尺寸要求破坏现有响应式布局。

### 9.2 SIMULATED Label

Stage1 baseline 要求模拟环境中的界面明确标识数据来源。

当前建议统一语义：

OFFLINE_DEMO
→ SIMULATED / Mock Data

Remote
→ Formal Service · Simulated Database

Remote 模式虽然使用模拟数据库，但请求链路是真实的：

Qt
→ TCP
→ C++ Server
→ SQLite

因此不建议将 Remote 数据简单标记为 Mock。

该显示口径需要团队统一确认。

## 10. Recommended Next Integration Order

建议下一轮联调按照以下顺序进行。

### Phase 1 — Member1

首先确认：

1. gap action 冻结计划；
2. SQLite 唯一路径；
3. reset / seed 命令；
4. Server host / port；
5. User/Admin 演示账号；
6. UI 尺寸验收口径；
7. simulated data source label 口径。

### Phase 2 — Member3

随后确认：

1. simulator 分支；
2. PILE_TELEMETRY 真实发送；
3. telemetry SQLite 写入；
4. FastAPI 同库只读；
5. Dashboard 同源数据刷新；
6. overheating → alarm 链路。

### Phase 3 — Member2

Member1 action 冻结后继续接入：

- USER_PROFILE
- WALLET_BALANCE
- STATION_DETAIL
- ORDER_ACTIVE
- ORDER_DETAIL
- ORDER_HISTORY
- ADMIN_ALARMS
- ADMIN_TICKETS
- ADMIN_STATION_UPDATE
- ADMIN_PILE_SET_STATUS
- ADMIN_PILE_RESTART

### Phase 4 — Full E2E

最终进行：

Owner Login
→ Station Query
→ Station Detail
→ Reserve
→ Start
→ Telemetry
→ Stop
→ Pay
→ History

以及：

Overheat Telemetry
→ Alarm Created
→ Admin Query
→ Alarm ACK
→ Ticket
→ Admin Logs
→ FastAPI
→ Dashboard

所有客户端、服务端、模拟器、FastAPI、Dashboard 必须使用同一 SQLite 配置。

## 11. Current Conclusion

截至 2026-09-09：

Member2 已达到当前协议条件下的 READY FOR INTEGRATION 状态。

Member2 当前剩余功能主要受 Member1 尚未补齐的正式 action 阻塞。

完整 Stage1 尚不能判定完成，仍需要：

- Member1 补齐 protocol gaps 和统一运行环境；
- Member3 提供 simulator / FastAPI / Dashboard 实际联调结果；
- 三位成员完成基于同一 SQLite 的最终 E2E 验证。
