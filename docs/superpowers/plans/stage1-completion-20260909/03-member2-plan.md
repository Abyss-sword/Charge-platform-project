# 成员2：Qt双端剩余工作与实施计划

> **For agentic workers:** 采用 `executing-plans`，先读01契约。保留已完成界面设计和正常业务适配，不另造后台，不回退Mock。你不是仓库中唯一工作者，不撤销他人修改。

**Goal:** 使车主端与运营端从部分接口联通提升为完整可操作、可恢复的第一阶段双端。

**Architecture:** 页面→Remote Provider→共用ServiceClient→成员1服务端；页面状态根据返回数据生成。网络状态、业务状态、加载/错误状态分开，禁止仅凭内存布尔值决定充电是否开始。

**Tech Stack:** Qt6 Widgets/Network、信号槽、QSS、现有Provider接口、QtTest。

## 1. 修改边界与实际文件

可写 `clients/user/`、`clients/admin/`、`clients/shared/network/`、`clients/shared/config/`、`clients/shared/ui/`、`tests/ui/` 和对应模块文档。根CMake/shared/domain/数据库/服务端由成员1修改。

入口：`clients/user/shell/main_window.cpp`、`user_application_flow.cpp`，`clients/user/pages/flow_pages.cpp`、`home_page.cpp`、`profile_page.cpp`、`active_order_page.cpp`、`charging_history_page.cpp`、`station_list_page.cpp`、`user_login_page.cpp`。

数据适配：`clients/user/adapters/remote_user_login_provider.*`、`remote_station_provider.*`、`remote_user_order_provider.*`、对应provider头；模型 `user_context.h`、`user_order.h`、`station.h`。

运营入口：`clients/admin/admin_main_window.cpp`、`admin_pages.cpp/.h`、`admin_provider.h`、`remote_admin_provider.*`。公共连接 `clients/shared/network/service_client.*`。

根build遗漏双端已确认；已有11项UI测试与一个临时真实协议测试不代表完整点击验收。临时 `/tmp/member2-review-driver-9384bf0` 不作为最终依赖，将有价值用例归入 `tests/ui/`。

## 2. M2-01：接入新会话、错误码和确定性写请求

**依赖：** M1-01/02合同及样例。**新增建议：** `clients/shared/network/pending_operation_store.*`、`tests/ui/pending_operation_test.cpp`；原有文件优先扩展。

- [ ] 首先补失败测试：服务端充值已提交但响应被测试代理丢弃；客户端重试不能生成新requestId。
- [ ] ServiceClient增加调用方可显式提供requestId的接口，默认新意图才生成UUID。响应只匹配请求ID；超时不能丢失pending业务记录，迟到响应不能串到新请求。
- [ ] Provider结果增加规范code、requestId、数据和message；不得把所有失败压成一个Error文本。
- [ ] UserContext保存userToken/过期时间（内存），用户操作都带token；退出清会话和敏感UI，pending按用户隔离，不能串号。
- [ ] 充值/支付发前保存最小pending到QStandardPaths应用数据目录，权限限制为当前用户；使用QSaveFile原子写。不得把令牌存进该文件。
- [ ] 超时后调用REQUEST_STATUS；COMMITTED后刷新余额/订单，NOT_FOUND时确认重试仍同ID同参数。旧响应不确定时不允许“再点一次新充值”制造另一笔意图。
- [ ] AUTH_INVALID要求登录后恢复核对；ORDER_STATE_CONFLICT刷新详情；余额不足可充值后回到原订单。默认不自动重放写请求。

**测试：** 同ID同充值仅一笔；同ID改金额拒绝；提交后丢响应→重启→同用户登录→核对仅一次；换用户不能看到pending内容；晚到响应不更新错误页面；错误码到UI分支可断言。

## 3. M2-02：完整车主页面数据链

按照下表逐行完成，不能只勾选“页面存在”。每行都需成功、空/不可用、输入错误、断线表现与重新查询证明。

| 页面 | 实际动作 | 成功结果与按钮 | 必验异常 |
| --- | --- | --- | --- |
| 登录 | USER_LOGIN | token/用户存入Context，进入首页 | 非法手机号不发送；失效响应不给假登录 |
| 首页 | USER_PROFILE、ORDER_ACTIVE | 昵称余额及当前订单入口 | 无活动订单显示正常空状态，不报错 |
| 附近站点 | STATION_NEARBY | 距离排序、价格和空闲量 | 无站点、坐标非法、网络失败 |
| 详情 | STATION_DETAIL | 本站pile列表，选中真实pileId | 停运/故障/未知状态不可选 |
| 导航 | 本站经纬度 | 打开可用地图或复制本站坐标 | 明确地图降级，不复制固定示例 |
| 预约确认 | ORDER_RESERVE | 服务端成功后进入RESERVED | 抢桩失败刷新；未完成订单跳恢复 |
| 充电页 | ORDER_START、ORDER_DETAIL | CHARGING电量/单价/费用/时间 | 无新鲜读数提示设备未就绪 |
| 停止结算 | ORDER_STOP、ORDER_DETAIL | PENDING_PAYMENT账单 | 重复停止查询事实，不重算金额 |
| 支付 | WALLET_PAY、余额/详情查询 | FINISHED、余额减少、历史可见 | 余额不足、响应未知、不重复扣款 |
| 活动订单恢复 | ORDER_ACTIVE、ORDER_DETAIL | 按服务端状态恢复正确页 | 无单清空旧UI；终态不能再停止 |
| 历史 | ORDER_HISTORY | 分页、同订单详情 | 空页正常，越界分页不无限请求 |
| 账户资料 | USER_PROFILE/UPDATE、WALLET_BALANCE | 昵称保存重读，余额实时来源 | 名称非法、冻结、失效会话 |
| 头像 | USER_AVATAR_UPLOAD/GET | 上传成功重读，重新登录仍显示 | 非图片/超限/损坏拒绝，旧图保留 |
| 充值 | WALLET_RECHARGE、REQUEST_STATUS | 金额校验、防重、刷新余额 | 不确定结果不再新建一笔 |

- [ ] 移除正常模式中已有 `Unavailable` 接线，改用Remote Provider；Mock只在显式OFFLINE_DEMO中使用。
- [ ] 不为每页建立新的TCP客户端或重复登录；复用共享连接和用户Context。
- [ ] 页面离开后停止定时刷新/断开对应回调，防重复信号、悬空访问和请求堆积。
- [ ] 查询返回后核对仍是当前用户/订单/页面，旧响应不能覆盖新选择。

## 4. M2-03：订单恢复是状态映射，不是按钮补丁

| 服务端事实 | 页面必须显示 | 允许的主要动作 |
| --- | --- | --- |
| 无活动订单 | 首页/找站 | 选择新桩 |
| RESERVED | 预约详情和期限 | 设备就绪时开始、取消 |
| CHARGING | 实时充电与费用 | 停止，不再次开始 |
| PENDING_PAYMENT | 冻结账单及原因 | 充值/支付，不继续充电 |
| FINISHED | 成功和历史 | 返回找站，不再次扣款 |
| CANCELED/EXPIRED | 明确终止原因 | 新建预约，不复用旧单 |
| 请求结果未知 | 正在核对/断线 | 禁止新写操作，允许重新查询 |

- [ ] 删除或收敛仅靠 `charging_` 决策的路径，以模型状态统一渲染按钮。
- [ ] 登录后、重连后、操作冲突后主动查询ORDER_ACTIVE/DETAIL；返回之前不放开开始/支付等写按钮。
- [ ] CHARGING期间2秒左右查询详情，离开页停轮询；过热/离线自动结算后能进入待支付，不卡在“充电中”。
- [ ] 断开只表示数据过期，不擅自把服务端订单取消。重新打开Qt无需人工记住orderId。

**新增建议测试：** `tests/ui/order_recovery_test.cpp`。通过FakeTcpServer验证状态渲染，再连接真实候选服务完成“开始已提交、响应丢失”的故障注入，不能只测连接信号变绿。

## 5. M2-04：完整运营端

| 模块 | 请求与交互 | 必验 |
| --- | --- | --- |
| 登录/退出 | ADMIN_LOGIN，token内存，会话失效回登录 | 普通用户不能访问 |
| 总览/营收 | OVERVIEW、REVENUE、REVENUE_TREND实际DTO | 与数据库同日/同站统计 |
| 站点 | STATIONS、STATION_CREATE/UPDATE | 名称/坐标/价格校验，停运不得新预约 |
| 电桩 | PILES、PILE_SET_STATUS/RESTART | 禁用不会被心跳覆盖，活动单冲突有提示 |
| 订单 | ORDERS筛选、详情展示 | 不能在UI直接篡改为已支付 |
| 用户 | USERS搜索/冻结/解冻，reason | 冻结状态重查可见，不能越权 |
| 告警 | ALARMS、ALARM_ACK | 同桩同类关联清楚，不删告警 |
| 工单 | TICKETS、START/RESOLVE/CLOSE | assignee、notes必填；未恢复不能关 |
| 日志 | LOGS，筛选动作 | 能追溯管理人、对象、动作与时间 |

上表省略的统一前缀均为 `ADMIN_`，实际action以合同为准。管理员查询响应按类型解析，不把总览强套rows。

- [ ] 用确认弹窗保护冻结、禁用、重启等动作，明确“教学模拟”；不把弹窗确认本身当操作成功。
- [ ] 写操作成功后重查目标和相关总览；失败保持原事实，不能先改表格再不回滚。
- [ ] 告警/工单页面替换Unavailable连接，提供完整列表、选择、详情和处理按钮，不仅一个只读列表。
- [ ] 新增/扩展 `tests/ui/remote_admin_provider_test.cpp`、`admin_smoke_test.cpp` 验证每类操作、权限、参数和状态冲突。

## 6. M2-05：视觉与交付

- 保持既有车主移动风、运营侧栏工作台和共用QSS；只修影响可用性的问题，不全盘改设计。
- 车主既有360×780、480×1040预览可保留；另按实际答辩窗口尺寸测试滚动、按钮可达。运营端1180×720或现有最小约束下不可遮挡；触发输入错误时不挤掉确认按钮。
- 每个页面覆盖loading/success/empty/error/disconnected；颜色配文字，金额完整显示两位，禁用理由可见。
- 全部私有操作经服务端，不能直接打开SQLite。单一入口启动，不要求用户编辑源码或手输pileId。
- 交付 `docs/stage1-release/member2.md`：页面—动作—测试映射、真实截图、已知限制、子构建目标；由成员1纳入统一根入口。

**验收结论：** 只有上表每行能执行、能查询确认、失败状态明确且真实服务恢复测试通过，才能标记成员2模块就绪；整组通过仍须第05文档。
