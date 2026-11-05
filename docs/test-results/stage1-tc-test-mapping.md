# 第一阶段 TC → 测试映射表（2026-09-14 补）

> 目的：关闭审查报告 §5 "仓库无 TC→测试映射表" 的缺口，使第一条阶段的每条遗留 TC 都能追到可执行的测试用例与证据文件。
>
> **来源限制（如实声明）**：TC-01…TC-24 的**原始编号表未落盘于当前 HEAD**（`acceptance.md` / `test-results.md` 在 HEAD 与全部 ref 中缺失，见 `deliverables/stage2/acc4569/defense/stage1-risk-recheck-20260914.md` §5）。本表按 `docs/stage2/baseline.md:30` 评审记录 A5 点名的遗留编号（TC-05/18/19 未修复；TC-20/21/22/24 未执行）以及审查报告可确认的主题归类；**未确认主题的编号不臆造映射，明确标注"原表未落盘"**。
>
> 判定口径：仅"已关闭/部分关闭/仍存在"，未实测标 NOT_RUN。命令均在干净副本执行。

## 1. 评审记录 A5 点名的遗留 TC

| TC | 主题（可确认来源） | 测试用例 / 证据 | 状态 | 命令 / 文件 |
| --- | --- | --- | --- | --- |
| TC-05 | 原表未落盘；A5 记"未修复" | — | **原表缺失，无法逐条复判** | 见审查报告 §5 |
| TC-18 | 告警→工单闭环（审查报告 §3 明确） | `admin_smoke_test::alertsPageLoadsOnEntry`（P1-A 新增，进入页面即加载告警/工单）；协议闭环 `tests/integration/alarm_ticket_test.cpp`；驱动 C 流程 | 已关闭 | `ctest -R admin_smoke_test`；`deliverables/stage1-linkage/report.txt` C3（close=756） |
| TC-19 | 原表未落盘；A5 记"未修复" | 相关覆盖：`shared_ui_test::enumCandidateBarMapsLabelToProtocolValue`（候选栏）、`navigation_provider_test::unavailableIsExplicit`（地图兜底卡） | **原表缺失，按主题归类** | 见审查报告 §5 |
| TC-20 | "Web 3 秒内反映"（审查报告 §5） | 前端 `dashboard` vitest 断言 `refreshIntervalSeconds===3`；三端联动驱动 A3/B7/C3 同周期变化 | **部分关闭**（无秒表级端到端断言） | `cd dashboard && npm run test:run`；`report.txt` A3/B7/C3 |
| TC-21 | 原表未落盘 | — | **原表缺失，无法逐条复判** | 见审查报告 §5 |
| TC-22 | FastAPI 停止降级（审查报告 §5，本轮已补实测） | `BUSINESS_OK_WITHOUT_ANALYSIS`：分析服务停时业务 TCP 完成登录→预约→开始→查余额→充值；隔离性 `analytics/tests/test_service_isolation.py`、`test_api.py` | 已关闭（本轮补运行时实测） | `python -m pytest analytics/tests/test_service_isolation.py -q`；审查报告 §5 |
| TC-24 | 车主端完整 GUI 操作链（审查报告 §5） | `tests/ui/navigation_smoke_test.cpp`、`user_polling_test.cpp`、`user_query_provider_test.cpp` 覆盖页面链与轮询；**GUI 手工登录截图仍仅登录页** | **仍存在**（BLOCKED，见 `final-report.md` §6） | `ctest -R "navigation_smoke|user_polling"` |

## 2. 本轮修复项对应的测试（防回归门禁）

| 修复 | 缺陷 | 测试用例 | 提交 |
| --- | --- | --- | --- |
| P1-A | 运营端告警工单页进入不加载 | `admin_smoke_test::alertsPageLoadsOnEntry`（断言 `queryAlarms/queryTickets` 各 ≥1） | `1c3362e` |
| P1-B | 头像阈值两端不一致 | `payment_idempotency_test`：5MiB+1 拒绝、5MiB 内 JPEG 通过、WebP 通过 | `41f75d4` |
| P1-C | 车主端可见页 3 秒刷新不全 + 离页不停 | `user_polling_test`：`visiblePagePollsAndStopsOnHide` / `refreshIssuesQueries` / `chargingPageStopsPollingOnHide` | `37621b8` |
| P2-a | 枚举候选栏无测试 | `shared_ui_test::enumCandidateBarMapsLabelToProtocolValue` | `bb59a03` |
| P2-b | 地图静态兜底卡无内容断言 | `navigation_provider_test::unavailableIsExplicit`（含站点/坐标/距离/模拟起点/外部地图入口） | `bb59a03` |
| P2-c | 总览旧快照丢弃无断言 | `remote_admin_provider_test::overviewDropsStaleSnapshot` | `bb59a03` |

## 3. 12 条必过场景 → 用例映射（`inputs/requirement-matrix.md`）

| 场景 | 映射 | 测试 / 证据 |
| --- | --- | --- |
| 自动注册不重复（并发首登只建一条） | A02 | `reservation_race_test`、`user_authorization_protocol_test` |
| 地图降级（无密钥/断网不空白） | A03 | `navigation_provider_test::unavailableIsExplicit` |
| 并发预约互斥 | A09 | `tests/integration/reservation_race_test.cpp` |
| 订单/余额/流水/桩一致 | A05/A06/A22 | `payment_idempotency_test`、`stage1_supplement_check.py`、驱动 B7 |
| 未结订单拦截 | A08 | `order_metering_protocol_test` |
| 防重复扣费 | A07/A20 | `payment_idempotency_test`（同 requestId 仅 1 条 PAYMENT）；驱动 B6 |
| 断网重连恢复 | A26/A27 | `remote_admin_provider_test::ordersAndLogsKeepStaleRowsAndRefreshOnReconnect` |
| 异常生成告警工单 | A15/A16 | `tests/integration/alarm_ticket_test.cpp`；驱动 C 流程 |
| 冻结用户限制 | A14 | `reservation_race_test::frozenUserCannotReserveOrOccupyPile`；`stage1_supplement_check.py` |
| 营收对账 | A22 | 驱动 B7（运营汇总 0.60 = Web trend 0.60） |
| 预测优于朴素基线否则降级 | A25 | `analytics/tests/test_model.py`；`forecast_v1` 契约 INSUFFICIENT_DATA |
| 20 桩模拟连接 10 分钟不崩溃 | A32 | `docs/test-results/` 压测记录（T21 loadtest） |

## 4. 回归基线（本轮 HEAD，干净副本实测）

| 套件 | 结果 | 命令 |
| --- | --- | --- |
| ctest | 38/38 通过 | `QT_QPA_PLATFORM=offscreen ctest --test-dir build -j1` |
| pytest | 67 通过 | `PYTHONPATH=analytics python -m pytest analytics/tests -q` |
| vitest | 13/13 通过 | `cd dashboard && npm run test:run` |
| mvn（MapReduce JUnit） | 6/6 BUILD SUCCESS | `mvn -f bigdata/mapreduce/pom.xml test` |
| e2e 充电闭环 | 通过 | `BUILD_DIR=build bash tests/e2e/basic_charge_flow.sh` |
| 三端联动验收驱动 | ACCEPTANCE=OK | `python scripts/acceptance-driver.py` |
