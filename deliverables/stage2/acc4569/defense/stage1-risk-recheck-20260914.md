# 第一阶段风险复检与全面审查报告（2026-09-14）

> 审查对象：`integration/stage2-acceptance`（HEAD `557c6e9`，含第一阶段联动线 `feature/stage1-linkage-20260913`）。
> 审查方式：全量回归 + 历史风险逐条代码核验 + 独立运行时时测（干净副本 `/home/bit/projects/stage1_audit`）。
> 状态口径：只用 已关闭 / 部分关闭 / 仍存在；未实测项标 NOT_RUN。
> **更新（2026-09-14）**：§3 三项部分关闭缺陷与 §4 四项测试门禁缺口已按确认后实施并复测通过，详见 §10；本表为**审查时**（修复前）的判定。

## 0. 结论速览

历史清单共 **158 条**，其中第一阶段 P0/P1/P2 级约 **95 条**（其余 33 条属二阶段范围，仅作参考）。

| 判定 | 数量（一阶段口径） | 说明 |
|---|---|---|
| 已关闭 | 约 88 条 | P0 全部关闭；P1 绝大多数关闭 |
| **部分关闭** | 3 条 | 见 §3（告警页刷新、头像阈值两端不一致、车主端刷新不全） |
| 仍存在的实现级缺陷 | 0 条 P0 | 未发现 P0 回归 |
| 测试门禁缺口 | 4 条 | 见 §4（代码已实现但无对应测试，缺陷因此漏检） |

**核心业务结论未变**：跨端联动在本 HEAD 上复跑仍 **ACCEPTANCE=OK**（§2），P0 六类（写入未进统一库/其他端不更新/重复扣款/状态越权/Web 假数据）均未复现。

## 1. 全量回归结果（HEAD 557c6e9，干净副本实测）

| 套件 | 结果 | 命令 |
|---|---|---|
| cmake + build（236 targets） | 成功 | `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build` |
| ctest | **37/37 通过** | `ctest --test-dir build -j1 --output-on-failure` |
| pytest | **67 通过** | `PYTHONPATH=analytics python -m pytest analytics/tests -q` |
| vitest | **13/13 通过** | `cd dashboard && npm run test:run` |
| mvn（MapReduce JUnit） | **6/6 BUILD SUCCESS** | `mvn -f bigdata/mapreduce/pom.xml test` |
| e2e 充电闭环 | 通过（FINISHED/IDLE/PAYMENT_COUNT=1/REVENUE 0.00→10.27） | `BUILD_DIR=build bash tests/e2e/basic_charge_flow.sh` |
| **三端联动验收驱动** | **ACCEPTANCE=OK** | `ACC_SERVER_PORT=48086 ACC_DB_PATH=... python scripts/acceptance-driver.py` |

## 2. P0 级历史风险逐条复检（全部仍关闭）

| 编号 | 历史问题 | 结论 | 证据 |
|---|---|---|---|
| P0/T13/4.2 | 用户认证与订单归属（未登录可操作、可操作他人订单、伪造 userId） | **已关闭** | `request_router.cpp:169-192` userReady（userToken 库存 SHA-256）；`:388-402` 订单动作一律传 tokenUserId；`order_service.cpp:442-491` detail/active/history 均 `WHERE user_id=?`；测试 `user_authorization_protocol_test` U01-U09、`reservation_race_test::frozenUserCannotReserveOrOccupyPile` |
| P0/T12/4.1 | 设备计量与订单计量衔接（读数→订单电量、重复不增、停后不加、新单新起点） | **已关闭** | `telemetry_service.cpp:116-124`（重复幂等）、`:126-135`（时序倒退拒绝）、`:186-203`（累计−基准）；测试 `telemetry_protocol_test:93`、`order_metering_protocol_test:105/218` |
| P0/T15/4.4 | 电价口径（充电中改站价影响本单） | **已关闭** | `order_service.cpp:340-355` 开始时锁价、`:422-430` 停止优先 priceLocked；测试 `order_metering_protocol_test::e04_lockedPriceSurvivesPriceChange`、`reservation_race_test:79` |
| P0/G21 | 重复付款/重复扣款 | **已关闭（服务端）** | `wallet_service.cpp:99-176`（pay 防重）+ `request_dedup` 事务回放；验收驱动 B6 同 requestId 两次支付仅 1 条 PAYMENT，余额 200.00→199.40 |
| P0/R2 | 场景按钮冒充业务联动（写业务库） | **已关闭** | `api.py:249-257` 返回 `writesBusinessDatabase:False`；`simulator_control.py:41-48`；测试 `test_api.py:369-391`、`test_service_isolation.py` |
| P0/R15 | 状态枚举口径（报告写 AVAILABLE/MAINTENANCE） | **已关闭（报告措辞有误）** | 迁移 `001_initial.sql:47-48` 电桩 `CHECK(status IN ('IDLE','RESERVED','CHARGING','FAULT','OFFLINE'))`；MAINTENANCE 实为**站点**状态（`:36-37`）。协议口径统一为 IDLE 五态 |

## 3. 部分关闭项（本次复检发现，需处理）

### 【新发现·P1】运营端"告警工单"页进入后不加载数据 —— 功能不可用
- **现象**：`AlertsTicketsPage` **未覆写** `refresh()`（基类 `admin_pages.cpp:30` 为空实现），且 `AdminMainWindow::setCurrentPage` 的页面分发链（`admin_main_window.cpp:37`）**唯独缺少该类**。结果：进入该页与 3 秒定时器都不发起 `queryAlarms/queryTickets`，表格恒为空 → 无法选中行执行"确认告警/开工单/解决/关闭"。
- **影响**：TC-18（告警工单闭环）的 UI 操作路径实际不可达；仅协议层闭环仍 OK（驱动 C3 PASS）。
- **证据**：`admin_pages.h:75`（无 refresh 声明）、`clients/admin/admin_main_window.cpp:37`（dynamic_cast 链 7 页、无 AlertsTicketsPage）。
- **为何漏检**：`tests/ui/admin_smoke_test.cpp` 只断言页数/导航/表格表头，**不断言页面数据加载**。

### 【新发现·P1】头像校验客户端与服务端阈值不一致
- **现象**：客户端允许 PNG/JPEG/**WebP** ≤ **5MB**（`clients/user/pages/profile_page.cpp:132,136`）；服务端 `wallet_service.cpp:59-83` 只认 PNG/JPEG 且 ≤ **2MiB**。
- **影响**：3MB 合法 PNG 或任意 WebP 会通过 UI 校验、在服务端以 `PARAM_INVALID` 失败，提示与实际不符。
- **运行时实测**（干净库 48086）：500KB PNG → `USER_PROFILE_UPDATE ok=true`，`USER_PROFILE` 回读 `avatarUrl=/tmp/avatar_small.png`；3MB PNG → `ok=false PARAM_INVALID`；失败后回读仍保留旧头像（**服务端回读与"失败保留旧头像"两条已正确**）。
- **证据**：`profile_page.cpp:130-146` vs `wallet_service.cpp:59-83`。

### 【部分关闭·P1】车主端可见页 3 秒刷新不全 + 充电页离开不停止
- **现象**：仅 `ChargingStatusPage` 有 3 秒轮询（`flow_pages.cpp:426-428`），且**无 hideEvent 停止**（`refreshTimer_->start()` 在 `:544`，仅终态 `:479/:506` 停止）——充电中离开页面仍持续轮询；首页/站点页/历史页/订单页只有 `refresh()`（进入时），无 3 秒周期。
- **影响**：违反 01 规格"可见页每 3 秒刷新、离开页面停止定时器"；运营端合规，车主端不完全。
- **证据**：`flow_pages.cpp:426-428,544`；`grep showEvent/hideEvent` 于 ChargingStatusPage 无命中。

## 4. 测试门禁缺口（代码已实现，但无测试 → 缺陷因此漏检）

| 缺口 | 说明 |
|---|---|
| 运营页数据加载 | `admin_smoke_test` 只测页数/导航，未测"进入页面触发查询"；本次 P1 告警页缺陷即由此漏检 |
| 枚举候选栏 | 全仓无 `enum_labels/fillCombo/labelFor` 测试引用；交付报告称"有 QtTest"与实际不符 |
| 地图静态兜底卡内容 | 无 `navigationStaticCard`/"距模拟起点"断言；`navigation_provider_test` 在 WebEngine 可用时 QSKIP |
| 总览 Stale 分支 / 营收汇总防旧覆盖 / 自定义区间非法 | 无断言 |

## 5. 仍无收口证据的历史条目（TC 用例口径）

| 条目 | 现状 |
|---|---|
| TC-22 FastAPI 停止降级 | **本次已补运行时实测**：分析服务停（端口无监听）时，业务 TCP 48086 完成 登录→预约→开始→查余额→充值 全部成功（`BUSINESS_OK_WITHOUT_ANALYSIS`）。此前仅架构隔离测试 |
| TC-24 车主端完整 GUI 操作链 | 仍无收口：截图仅登录页，`final-report.md` §6 自标登录后不切主窗口的 UI 缺陷 BLOCKED |
| TC-20 "Web 3 秒内反映" | 仅 3000ms 常量与单测断言 `refreshIntervalSeconds===3`，无秒表级断言 |
| 7 条 TC 原编号独立复判 | 仓库无 TC→测试映射表；TC 原表与 9-10 acceptance.md 均未落盘于 HEAD |
| A01-A32 全 PASS 的原始产物 | `deliverables/stage1-final/` 在 HEAD 与全部 ref 中缺失，该声明不可复核 |
| `member1_flow.cpp` e2e 缺 ORDER_HISTORY 步骤 | 仍在（协议测试另有覆盖） |
| `docs/stage1-release/task-status.md` | 停留在 9-10 "未开始"，未随进展更新 |

## 6. 口径冲突复核

- **TC 表（9-11，待修复3/待执行4）vs acceptance.md（9-10，A01-A32 全 PASS）**：两者在 HEAD 均无落盘产物。当前 HEAD 的权威独立证据是 `deliverables/stage1-linkage/report.txt` + 本轮 `ACCEPTANCE=OK` + `ctest 37/37`，覆盖 S1 联动三流程，**不按 TC 原编号逐条复判**。
- **引用串档提醒**：`final-report.md:35` 写"report.txt B1=892→C3 close=904"，实际 `report.txt` 为 B1=744、C3 close=756；892/904 是 `rerun-driver.log` 的复跑值——引用串了文件，建议修正。

## 7. 教学限制（非缺陷，如实声明项，建议保持）

明文 TCP 无 TLS、默认演示口令 `admin/123456`（`admin_service.cpp:134-161`，且 `ensureDefaultAdmin` 无生产化开关）、手机号即身份、模拟充值支付、JPEG SOF 尺寸解析未实现、VM 2 核 7.7GiB、UTC 日口径。以上与 `known-limitations` 一致，**不作为缺陷**，但若未来对外部署需先解决。

## 8. 建议处理优先级

1. **P1-A 告警页数据加载**（新增 `void refresh() override` + 加入 `setCurrentPage` 分发链），并补一条"进入页面触发查询"的 UI 测试防回归。
2. **P1-B 头像阈值对齐**：二选一——服务端放宽到 5MB+WebP，或客户端收紧到 2MiB+PNG/JPEG；并在契约/UI 文案统一。
3. **P1-C 车主端刷新**：`ChargingStatusPage` 加 hideEvent 停止；其余可见页按规格决定是否统一 3 秒。
4. **P2 测试门禁**：补枚举候选栏、地图兜底卡、总览 Stale/营收防覆盖 的单测。
5. **文档**：更新 `docs/stage1-release/task-status.md`；修正 `final-report.md` 引用串档；补 TC→测试映射表。

## 9. 报告边界

本报告为**审查结论**（发布时未改动任何代码）。§3 三项缺陷与 §4 测试缺口均给出定位与修复方向，待确认后再实施。运行时实测均在 VM 干净副本上以独立端口/库执行，未触碰演示库与他人改动；测试进程已清理。

## 10. 修复落地记录（2026-09-14 追加，§3/§4 逐项收口）

经确认后按 TDD（先加失败断言→最小修复→复测）实施，全部在 VM 干净副本构建并以 ctest 复测。

### §3 部分关闭项 → 已关闭

| 项 | 修复 | 提交 | 防回归测试 |
|---|---|---|---|
| P1-A 告警工单页进入不加载 | `AlertsTicketsPage` 新增 `refresh() override`（加载告警+工单）并加入 `AdminMainWindow::setCurrentPage` 分发链 | `1c3362e` | `admin_smoke_test::alertsPageLoadsOnEntry` |
| P1-B 头像阈值两端不一致 | 服务端放宽为 PNG/JPEG/**WebP ≤5MiB**（魔数校验，含 `RIFF`+`WEBP`），与客户端一致 | `41f75d4` | `payment_idempotency_test`：5MiB+1 拒绝 / 3MiB JPEG 通过 / WebP 通过 |
| P1-C 车主端刷新不全 + 离页不停 | `PreviewPage` 统一 `pagePollTimer`(3s) + `showEvent/hideEvent`；远程页 `enablePolling()`、`refresh() override`；充电页移除自有定时器，终态/离页停表 | `37621b8` | `user_polling_test`（3 用例） |

### §4 测试门禁缺口 → 已补齐

| 缺口 | 补测 | 提交 |
|---|---|---|
| 运营页数据加载 | `admin_smoke_test::alertsPageLoadsOnEntry`（进入页面即触发 `queryAlarms/queryTickets`） | `1c3362e` |
| 枚举候选栏 | `shared_ui_test::enumCandidateBarMapsLabelToProtocolValue`（中文 label↔协议值、`labelFor` 反查、`fillCombo` userData） | `bb59a03` |
| 地图静态兜底卡内容 | `navigation_provider_test::unavailableIsExplicit`（`navigationStaticCard` + 站点/坐标/距离/模拟起点/外部地图入口） | `bb59a03` |
| 总览 Stale / 营收防旧覆盖 | `remote_admin_provider_test::overviewDropsStaleSnapshot`（版本 10→5 丢弃并入 Stale；11 正常覆盖） | `bb59a03` |

### 复测结论

- ctest **38/38 通过**（原 37 + 新增 `user_polling_test`）；`remote_admin_provider_test` 12 用例、`shared_ui_test` 11 用例、`navigation_provider_test` 4 用例全绿。
- §5 遗留项中的 TC→测试映射缺口已由 `docs/test-results/stage1-tc-test-mapping.md` 关闭；`final-report.md` 引用串档、`task-status.md` 陈旧两处文档缺陷同轮修正（提交见文档提交）。
- 仍存在项不变：TC-24 车主端 GUI 手工登录（BLOCKED，教学边界如实保留）、TC-20 无秒表级断言、A01-A32 原始产物缺失。
