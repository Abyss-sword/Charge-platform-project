# 第一阶段修复后全量回归与运行时复核记录（2026-09-14）

> 范围：`stage1-risk-recheck` 系列修复（P1-A/P1-B/P1-C/P2 + 文档）实施后的完整回归与运行时复核。
> 环境：VM 干净副本 `/home/bit/projects/stage1_audit`（Windows 检出经 tar 同步），独立端口/独立库，未触碰演示库与他人改动。
> 工具链：Python 3.12.13（`stage2_acceptance/analytics/.venv312`）、Node v22.23.2（nvm）、Maven 3.9.9 + JDK 1.8.0_261（`/home/bit/opt`）。

## 1. 自动化回归结果

| 套件 | 结果 | 命令 |
| --- | --- | --- |
| ctest | **38/38 通过**（原 37 + 新增 `user_polling_test`） | `QT_QPA_PLATFORM=offscreen ctest --test-dir build -j1` |
| pytest | **67 passed** | `PYTHONPATH=analytics .venv312/bin/python -m pytest analytics/tests -p no:warnings` |
| vitest | **13/13 passed** | `cd dashboard && npm run test:run` |
| mvn（MapReduce JUnit） | **6/6 BUILD SUCCESS** | `mvn -f bigdata/mapreduce/pom.xml test` |
| e2e 充电闭环 | 通过 | `BUILD_DIR=build bash tests/e2e/basic_charge_flow.sh` → `ORDER=FINISHED PILE=IDLE PAYMENT_COUNT=1 REVENUE 0.00→10.27` |
| 三端联动验收驱动 | **ACCEPTANCE=OK** | `ACC_SERVER_PORT=48086 ACC_DB_PATH=... ACC_DASHBOARD_API=http://127.0.0.1:8010/dashboard python scripts/acceptance-driver.py` |

### 1a. 三端联动三条流程（本轮复跑关键值）

- **A 运营改桩 → 三端同步**：A2 改桩 FAULT（`changeVersion=110`，DB `IDLE→FAULT`）→ A3 车主侧 `userPile101Status=FAULT` 且 Web 桩分布 `FAULT count=1`，同快照版本 110；A4 恢复 IDLE（`changeVersion=111`）。
- **B 支付闭环 + 幂等**：B1 充值后余额 200.00；B4 停止结算 0.60（用量 0.50kWh）；B6 同 requestId 两次支付均 `ok=true`，`wallet_transaction` 仅 **1 条** PAYMENT，余额 200.00→199.40（差 0.60 一笔）；B7 运营汇总 0.60 = Web trend 0.60（`snapshotVersion=118`）。
- **C 告警→工单闭环**：过温告警→ACKNOWLEDGED(120)→PROCESSING(121)→RESOLVED(122)→CLOSED(124)；Web 顶栏显示 `ALARM-4 / OVERHEAT / 严重 / CLOSED`。

## 2. 修复项运行时复核

### P1-B 头像阈值（服务端实测，独立库 48086）

| 输入 | 结果 |
| --- | --- |
| 200KB PNG | `ok=true OK` |
| 3MiB JPEG | `ok=true OK`（修复前会被 2MiB 上限拒绝） |
| 64KB WebP（`RIFF`+`WEBP`） | `ok=true OK`（修复前不支持 WebP） |
| 5MiB+1 PNG | `ok=false PARAM_INVALID` |
| `USER_PROFILE` 回读 | `avatarUrl=/tmp/acc_run/av/x.webp`（服务端回读正确） |

结论：客户端 ≤5MB/PNG-JPEG-WebP 与服务端已一致，"通过 UI 校验却在服务端失败"的不一致消除。

### P1-A 告警工单页进入即加载

由 `admin_smoke_test::alertsPageLoadsOnEntry` 以离屏 Qt 实测：未进入该页前 `queryAlarms=queryTickets=0`；`setCurrentPage(AlertsTickets)` 后两者各 ≥1；重复进入会再次发起查询（定时器路径）。ctest 中该用例通过。

### P1-C 车主端刷新

`user_polling_test` 3 用例通过：可见页启动 3 秒定时器、离页停止；`refresh()` 真正发起查询（首页=资料，账户页=资料+余额）；充电页离页停止（修复"充电中离开仍轮询"）。

## 3. 清理

- 验收服务端进程（48086）、FastAPI（8010）已停止；临时库与头像探针目录（`/tmp/acc_run`）已清理。
- 未写演示库；未改动他人文件；未 force、未动 main。

## 4. 对照提交

| 提交 | 内容 |
| --- | --- |
| `b60144f` | 审查报告（158 条历史风险复检） |
| `1c3362e` | P1-A 告警工单页数据加载 |
| `41f75d4` | P1-B 头像阈值 WebP/5MiB |
| `37621b8` | P1-C 车主端 3 秒刷新 + 离页停表 |
| `bb59a03` | P2 四测试门禁 |
| `80d9fab` | 文档修正（串档/状态/TC 映射） |
