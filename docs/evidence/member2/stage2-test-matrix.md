# Member2 Stage2 测试与验收映射

## 2026-09-16 接口接入更新

Member3 远端及跟踪引用均已确认 `442e9e0`。HTTP 和 forecast 正常路径已解除阻塞；
新增 `analysis_http_client_test`（shared/network/analysis）及 `forecast_pages_http_test`
（user/admin/analysis/network/lifecycle/boundary/responsive）。
本轮实现、分层门禁、运行配置见 [forecast 接入证据](stage2-forecast-integration.md)。
下方 2026-09-15 的 17 项执行记录为历史证据，不是本轮最终数量。

本轮完整 Build PASS；Member2 标签筛选 **19/19 PASS**，包含 User 12 项、Admin 5 项、
Analysis 5 项、Responsive 6 项（标签交叠，不可相加）。两个新增测试 target 均通过。
Qt 6.2.4 / GCC 11.3.0；configure 的 Vulkan headers 缺失为非阻断 warning，已有测试还有 avatarPath 初始化 warning。

根 CTest：**36/43 PASS，7 个非 Member2 target 失败**，与本轮 HTTP 基础提交验证时的失败名单一致。
不能沿用旧记录的“只有3个失败”。未修改任何后端文件来规避失败：

| 失败 target | 本次可观察原因 |
|---|---|
| schema_test | 数据库重置退出 127；本机 sqlite3 命令缺失 |
| reset_demo_database_test | sqlite3: 未找到命令 |
| server_launch_config_test | 脚本查找 `build/charge_platform_server`，当前构建在 build-stage2 |
| telemetry_protocol_test | 管理员登录/令牌断言失败 |
| user_authorization_protocol_test | 跨角色场景 adminToken 为空 |
| admin_operations_protocol_test | 管理员登录断言失败 |
| member1_flow_test | ADMIN_LOGIN_FAILED |

`git diff --check` PASS。本轮未改 dashboard，未重跑 Web 测试，不能据此新增 Web 验收结论。

| 验收项 | 本轮新增自动证据 | 当前整体状态 |
|---|---|---|
| S2-T11 | 两端 1/6/24 正常响应消费、数值和时间映射 | PARTIAL：负荷已接，容量指标及真实页面/实际值比较待联合验收 |
| S2-T15 | HTTP 六状态、EXPIRED 保留最后正文、错误隐藏正文 | PARTIAL：真实模型停用/过期触发仍需人工 |
| S2-T16 | 两 Qt 端复用相同 parser/panel，不推算时间偏移 | PARTIAL：Web 绑定与窗口 ICR、三端同版本签收未完成 |
| S2-T17 | local HTTP 乱序、1→6→24、切站清理、离页与重入隔离 | PARTIAL：Qt 自动测试通过，Web 和真实联合复核待完成 |
| S2-T22 | 两端五档尺寸的有数据页面 sanity | PARTIAL：物理断网/DPI/视觉验收不由此替代 |

推荐、正式容量、合规 Top3 与预警仍按具体缺口阻塞；不能再将下面历史表中的
“AnalysisHttpClient 受接口门禁限制”或“正式预测 API 未达到门禁”作为当前结论。

## 历史基础矩阵（2026-09-15）

生成日期：2026-09-15

执行前基线提交：`7ecf403`

适用范围：`clients/user/`、`clients/admin/`、`clients/shared/` 与 `tests/ui/`。

状态口径：只有完整满足文档验收输出才记 `PASS`；部分自动覆盖记 `PARTIAL`；必须现场执行记 `MANUAL`；缺正式上游契约或服务记 `BLOCKED`。

## 1. 固定复现命令

```bash
cmake -S . -B build-stage2 -DBUILD_TESTING=ON
cmake --build build-stage2 --parallel 2

# Member2 全部测试
ctest --test-dir build-stage2 -L '^member2$' --output-on-failure

# A. User client
ctest --test-dir build-stage2 -L '^user$' --output-on-failure

# B. Admin client
ctest --test-dir build-stage2 -L '^admin$' --output-on-failure

# C. Shared/network
ctest --test-dir build-stage2 -L '^(shared|network)$' --output-on-failure

# D. Analysis contract/state
ctest --test-dir build-stage2 -L '^analysis$' --output-on-failure

# E. Navigation/lifecycle
ctest --test-dir build-stage2 -L '^(navigation|lifecycle)$' --output-on-failure

# F. Remote/Mock boundary
ctest --test-dir build-stage2 -L '^boundary$' --output-on-failure

# G. Responsive layout
ctest --test-dir build-stage2 -L '^responsive$' --output-on-failure
```

测试使用 Qt offscreen 和本机 fake TCP server，不访问外网、真实业务服务或 Member3 API。运行环境必须允许绑定 localhost 临时端口。

## 2. Stage2 验收编号定义与当前映射

| 编号 | 文档目标与输入 | 期望输出 | Member2 范围 | 自动测试 | 人工测试 | 状态与备注 |
|---|---|---|---|---|---|---|
| S2-T01 | 新用户登录，修改昵称头像，充值，查站导航，预约/充电/停止/支付 | 持久化正确，余额、订单、营收对账 | 共同负责客户端操作链 | `user_login_test`、`station_query_test`、`user_query_provider_test`、`user_order_provider_test`、`navigation_smoke_test` | 真实服务、数据库和运营营收对账 | PARTIAL：客户端与 TCP 契约通过，持久化需联合验收 |
| S2-T02 | 新增站、冻结/解冻用户、重启桩、告警工单流转 | 权限和状态保护有效，预测不改变业务状态 | 共同负责运营 UI 与 Provider | `admin_smoke_test`、`remote_admin_provider_test` | 真实管理员权限和业务状态流转 | PARTIAL：客户端请求与页面通过，服务端保护需联合验收 |
| S2-T11 | 1/6/24 小时预测与实际比较 | 各期限有负荷/空闲桩指标、样本数和基线，无同值占位 | 负责期限选择和结果展示，不负责模型正确性 | `analysis_pages_test` 仅覆盖三档选择和无假数据 | 真实结果与实际值比较 | BLOCKED：Member3 正式预测 API 未达到门禁 |
| S2-T12 | 容量 10，输入 -1、12、未知容量 | 裁剪到 0/10 并保留原始越界；未知容量返回 null 原因 | 负责契约消费和展示 | `forecast_contract_test` 覆盖 nullable 字段 | 正式 API 越界结果展示 | BLOCKED：正式结果与容量契约依赖 Member3 |
| S2-T13 | 已知未来曲线且含相同峰值 | 正确稳定排序未来 Top3，不使用历史峰值替代 | 负责运营 Top3 容器和展示 | `analysis_pages_test` 覆盖容器及无固定数值 | 校验真实 Top3 数值和排序 | BLOCKED：Member3 尚未提供合规正式数据 |
| S2-T14 | 禁用/无坐标/预测不可用/并列候选 | 排除禁用站，缺项重分权，排序稳定且说明真实 | 与 Member3 共同负责推荐 UI | 页面仅保留不可用状态和手动查站退路 | 正式推荐候选、理由、详情和导航 | BLOCKED：recommendation schema 未冻结 |
| S2-T15 | 模型文件不可读或结果过期 | 明确展示 baseline/数据不足，不把离线报告冒充已训练 | 负责六状态客户端展示 | `forecast_contract_test`、`analysis_state_presenter_test`、`analysis_pages_test` | 停真实模型或投放过期产物 | PARTIAL：六状态、EXPIRED 正文保留和错误隐藏正文通过；真实服务场景未验收 |
| S2-T16 | 三端选择同站、同原点、同版本 | 单位/数值一致，历史与业务来源分离 | Qt/Web 联动负责人 | `forecast_contract_test` 验证 Qt 契约字段 | 三端同版本逐字段对比 | BLOCKED：Member3 API 与 Web 正式数据未就绪 |
| S2-T17 | 快速切期限和站点，旧请求晚到 | 页面只保留最后选择，不闪回旧结果 | Qt 页面与 HTTP 客户端负责人 | `analysis_pages_test` 覆盖参数切换清理；`service_client_test` 覆盖 TCP 单请求/不重放 | 正式 HTTP 乱序响应 | BLOCKED：AnalysisHttpClient 受 Member3 接口门禁限制 |
| S2-T18 | 分别停止分析服务与 Hadoop | 新训练失败如实返回，旧产物标旧，充电支付仍可用 | 负责客户端降级及业务入口保持 | `user_order_provider_test`、状态 presenter 测试 | 实际停止服务并完成支付闭环 | PARTIAL：客户端隔离基础通过，实际停服属于联合人工验收 |
| S2-T22 | 两档分辨率、关闭动效、断网恢复 | 关键按钮无遮挡，状态清晰，数值不因动效改变 | 直接负责 | `navigation_smoke_test`、`admin_smoke_test`、`analysis_pages_test`、`station_query_test`、`remote_admin_provider_test` | 实机 DPI/字体及物理断网复核 | PARTIAL：自动布局、动效关闭和 fake TCP 恢复通过，仍需实机确认 |
| S2-T24 | 非作者按说明操作并完成单人答辩演练 | 无口头补丁可复现，时长 10–11 分钟，材料与版本一致 | 参与操作与材料交接 | 本文提供固定命令 | 非作者复跑、计时、录屏和签字 | MANUAL |

S2-T03～T10、S2-T19～T21、S2-T23 的主体分别属于成员1或成员3，不把对应后端/数据测试计入 Member2 自动通过数。

## 3. Member2 CTest 清单

| 测试 target | 代表性 QtTest case | 覆盖对象 | 阶段 | 验收映射 |
|---|---|---|---|---|
| `navigation_smoke_test` | `switchesWithoutRecreatingPages`、`followsBusinessFlowsAndBack`、`fitsTargetSizes` | 车主导航、生命周期、尺寸、模式边界 | Stage1/2 | T01、T22 |
| `navigation_provider_test` | `buildsTencentRouteUrl`、`unavailableIsExplicit` | 导航 Provider | Stage1 | T01 |
| `shared_ui_test` | `supportsEveryPresentationState`、`previewsEmptyErrorAndRestoresContent` | 共享页面状态和控件 | Stage1/2 | T15、T22 |
| `admin_smoke_test` | `createsAndNavigates`、`allPagesFitSupportedSizes`、`alertsPageLoadsOnEntry` | 运营页面、导航、尺寸、工单入口 | Stage1/2 | T02、T22 |
| `mock_data_provider_test` | `failuresAreDeterministic`、`scenariosRecoverNormal` | OFFLINE_DEMO fixture 边界 | Stage1/2 | T22 |
| `user_login_test` | `logoutClearsSessionAndReloginUsesNewContext`、`loginRestoresProfileBalanceAndActiveOrderAfterRestart` | 登录、token、账户切换、恢复、尺寸 | Stage1/2 | T01、T22 |
| `service_client_test` | `timeout`、`singleInFlight`、`reconnectDoesNotReplayCompletedWrite` | TCP ServiceClient、超时和重连 | Stage1/2 | T01、T02、T17、T22 |
| `remote_user_login_provider_test` | `success`、`offlineMapsToOffline`、`duplicateSubmitIsRejected` | 正式登录 Provider | Stage1/2 | T01、T22 |
| `user_runtime_config_test` | `missingFileUsesDefaults`、`invalidPortUsesDefault` | Remote 配置边界 | Stage1 | T01 |
| `station_query_test` | `requestEnvelopeAndSuccessMapping`、`staleDataSurvivesAndReconnectRefreshesVisiblePage` | 找桩、详情、stale/reconnect | Stage1/2 | T01、T22 |
| `user_order_provider_test` | `requestEnvelopesAndSuccessMapping`、`paymentTimeoutQueriesDetailAndRecoversPaidOrder`、`staleDisablesWritesAndReconnectDoesNotReplay` | 预约、充电、支付、充值和恢复 | Stage1/2 | T01、T18、T22 |
| `user_query_provider_test` | `accountQueriesAndPreservesServerCode`、`orderActiveDetailAndHistory` | 资料、余额、活动/历史订单 | Stage1 | T01 |
| `remote_admin_provider_test` | `formalQueriesAndWrites`、`managementActionsSendContractPayloads`、`ordersAndLogsKeepStaleRowsAndRefreshOnReconnect` | 运营查询/写入、stale/reconnect | Stage1/2 | T02、T22 |
| `user_polling_test` | `visiblePagePollsAndStopsOnHide`、`chargingPageStopsPollingOnHide` | 页面可见性和轮询生命周期 | Stage1/2 | T01、T22 |
| `forecast_contract_test` | `officialFixturesParse`、`allContractStatusesMap`、解析失败分支 | forecast_v1 六状态和字段契约 | Stage2 | T11、T12、T15、T16 |
| `analysis_state_presenter_test` | `appliesExpiredStateWithoutHidingExistingContent`、`appliesUnavailableStateAndHidesAnalysisContent` | 六状态 UI 语义 | Stage2 | T15、T22 |
| `analysis_pages_test` | `bothPagesRenderEveryForecastStatus`、`reenteringPagesClearsAnOldErrorState`、`supportedViewportSizesDoNotOverflow` | 两端分析页、生命周期、尺寸、无假数据 | Stage2 | T11～T17、T22 |

## 4. 自动与人工边界

- 自动化可以证明：Qt 页面/控件存在、Provider 请求字段、TCP 错误映射、Remote/Mock 隔离、六状态解析和展示、页面生命周期以及目标尺寸下的布局 sanity。
- 自动化不能替代：真实数据库持久化和跨端对账、模型指标正确性、正式 Flask 接口、Qt/Web 三端数值一致性、真实 HTTP 晚到响应、物理断网、实机 DPI/字体视觉检查及答辩演练。
- Member3 门禁解除前，禁止用旧 FastAPI、fixture fallback 或临时 URL 将 T11～T17 标为通过。

## 5. 证据记录模板

每次候选验收记录以下命令的实际输出，并填写候选提交：

```text
candidate commit:
Qt / CMake / compiler:
cmake build result:
member2 label result: passed / total
root ctest result: passed / total
manual checks:
Member3 blockers:
non-Member2 failures:
```

## 6. 本次实际执行结果

执行日期：2026-09-15；候选工作树基于 `7ecf403`，仅增加测试标签与本映射文档。

| 命令/分组 | 实际结果 |
|---|---|
| `cmake --build build-stage2 --parallel 2` | PASS |
| `-L '^member2$'` | 17/17 PASS |
| `-L '^user$'` | 11/11 PASS |
| `-L '^admin$'` | 4/4 PASS |
| `-L '^(shared\|network)$'` | 10/10 PASS |
| `-L '^analysis$'` | 3/3 PASS |
| `-L '^(navigation\|lifecycle)$'` | 10/10 PASS |
| `-L '^boundary$'` | 6/6 PASS |
| `-L '^responsive$'` | 5/5 PASS |
| 根 CTest | 38/41 PASS |

根测试的三个失败均不属于 Member2：`schema_test` 和 `reset_demo_database_test` 因当前环境缺少 `sqlite3`；`server_launch_config_test` 固定查找旧的 `build/charge_platform_server`，而本轮使用 `build-stage2`。本轮未修改对应成员代码。
