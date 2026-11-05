# 任务状态表（task-status.md）

状态取值：未开始 / 进行中 / 模块通过 / 整组通过 / 阻塞。证据路径待实际生成后回填。禁止只写百分比。

| 任务 | 依赖 | 写作者 | 状态 | 证据 |
| --- | --- | --- | --- | --- |
| R0 候选建立与三支合并 | - | 总控 | 模块通过（c9738d8） | docs/stage1-release/baseline.md |
| R0 环境准备（Node/venv/npm ci） | 候选建立 | 总控 | 未开始 | - |
| R0 初始构建基线（ctest -N 清单） | 环境准备 | 总控 | 未开始 | - |
| M1-01 独立启动与测试隔离 | 初始基线 | 成员1 | 未开始 | - |
| M1-02 用户会话/归属/请求状态（U01-U09） | M1-01 | 成员1 | 未开始 | - |
| M1-03 统一计量/异常结算/管理禁用（E01-E13） | M1-02 | 成员1 | 未开始 | - |
| M1-04 头像上传/查询 | M1-02 | 成员1 | 未开始 | - |
| M1-05 接口审查与 contract-delta 冻结 | M1-01~04 | 成员1 | 未开始 | - |
| M2-01 会话接入/错误码/pending防重 | contract-delta | 成员2 | 未开始 | - |
| M2-02 车主端14行页面链 | M2-01 | 成员2 | 未开始 | - |
| M2-03 订单恢复状态映射 | M2-01 | 成员2 | 未开始 | - |
| M2-04 运营端9模块 | M2-01 | 成员2 | 未开始 | - |
| M2-05 视觉与交付文档 | M2-02~04 | 成员2 | 未开始 | - |
| M3-01 模拟器可靠/持续/可控 | contract-delta | 成员3 | 未开始 | - |
| M3-02 同库只读API与健康检查 | M1-03 | 成员3 | 未开始 | - |
| M3-03 指标算法（8项） | M3-02 | 成员3 | 未开始 | - |
| M3-04 预测/推荐诚实化 | M3-03 | 成员3 | 未开始 | - |
| M3-05 大屏联动与页面状态 | M3-04 | 成员3 | 未开始 | - |
| M1-07/R3 统一启动器 + E2E | 全模块 | 成员1/总控 | 未开始 | - |
| R4 A01-A32 验收 + 两链各两轮 | 全部 | 总控 | 未开始 | - |
| R4 交付8文件 | 验收 | 总控 | 未开始 | - |

## 需求矩阵 21 条映射状态

见 inputs/requirement-matrix.md；逐条结果随验收在 acceptance.md 回填。

## 未完成/阻塞记录

- git fetch 失败（GitHub publickey）：已记录，不影响本地候选开发；推送需用户另行授权与凭据。
- node/npm 原缺失：待用户级安装（已获授权）。
- fastapi 等 Python 依赖系统缺失：待候选 venv 安装。

## 2026-09-14 现状更新（不改写上方历史行）

上方表格为 R0 阶段编制时（2026-09-10 前）的初始快照，多数行停留在"未开始"；实际进展已远超该表。以下按当前 HEAD 的事实更新，证据为仓库内可复核产物。

- **当前 HEAD**：`integration/stage2-acceptance`，本轮修复后为 `stage1-risk-recheck` 系列提交（P1-A `1c3362e`、P1-B `41f75d4`、P1-C `37621b8`、P2 `bb59a03`，另含 `b60144f` 审查报告）。
- **权威独立证据**：`deliverables/stage1-linkage/report.txt` + `rerun-report.txt`（三端联动三流程，`report.txt` B1=744→C3 close=756；非作者复跑 B1=892→C3 close=904）+ 本轮 `ACCEPTANCE=OK` + `ctest 38/38`。
- **技术栈锁定**：Python 3.12.13（`analytics/.venv312`）、Hive 3.1.3 / Spark 3.4.1 / Hadoop 3.2.1 / JDK 1.8.0_261（见 `docs/stage2/baseline.md`）。

| 任务 | 实际状态 | 证据指引 |
| --- | --- | --- |
| R0 环境准备（Node/venv/npm ci） | 模块通过 | `docs/stage2/baseline.md`；venv312 就位 |
| R0 初始构建基线（ctest 清单） | 模块通过 | ctest 现有 38 个用例全绿 |
| M1-01 独立启动与测试隔离 | 模块通过 | `scripts/start-all.sh`；S2-T19 退出码 3/4 实测 |
| M1-02 用户会话/归属/请求状态（U01-U09） | 整组通过 | `tests/integration/user_authorization_protocol_test.cpp` |
| M1-03 统一计量/异常结算/管理禁用（E01-E13） | 整组通过 | `telemetry_protocol_test`、`order_metering_protocol_test` |
| M1-04 头像上传/查询 | 模块通过（教学边界） | 服务端可读路径 + PNG/JPEG/WebP ≤5MiB（P1-B 已对齐） |
| M1-05 接口审查与 contract-delta 冻结 | 模块通过 | `docs/stage2/contracts.md` |
| M2-01 会话接入/错误码/pending防重 | 模块通过 | `user_query_provider_test`、`remote_admin_provider_test` |
| M2-02 车主端14行页面链 | 模块通过 | 14 页可操作；`tests/ui/navigation_smoke_test.cpp` 等 |
| M2-03 订单恢复状态映射 | 模块通过 | 六态 fixtures（T01） |
| M2-04 运营端9模块 | 模块通过（P1-A 本轮修复） | 告警工单页进入即加载，`admin_smoke_test::alertsPageLoadsOnEntry` |
| M2-05 视觉与交付文档 | 模块通过 | `deliverables/stage1-linkage/`、本报告 |
| M3-01 模拟器可靠/持续/可控 | 模块通过 | 遥测协议测试 + `stage1_supplement_check.py` |
| M3-02 同库只读API与健康检查 | 模块通过 | `tests/service_isolation_test`、pytest |
| M3-03 指标算法（8项） | 模块通过 | pytest 67 通过；FastAPI `/dashboard` |
| M3-04 预测/推荐诚实化 | 模块通过（一阶段基线） | `forecast_v1` 契约 + INSUFFICIENT_DATA 诚实降级 |
| M3-05 大屏联动与页面状态 | 模块通过 | vitest 13/13；三端联动 A3/B7/C3 |
| M1-07/R3 统一启动器 + E2E | 模块通过 | `tests/e2e/basic_charge_flow.sh` |
| R4 A01-A32 验收 + 两链各两轮 | 部分通过 | S1 联动三流程 ACCEPTANCE=OK；TC 原表未落盘，见下方未结项 |
| R4 交付8文件 | 部分通过 | `deliverables/stage1-linkage/final-report.md` 等；TC→测试映射见 `docs/test-results/stage1-tc-test-mapping.md` |

## 仍未结项（第一阶段遗留）

- TC-24 车主端完整 GUI 操作链：仍无收口（截图仅登录页；`final-report.md` §6 自标登录后不切主窗口的 UI 缺陷 BLOCKED）。
- TC-20 "Web 3 秒内反映"：仅 3000ms 常量与前端单测断言，无秒表级端到端断言。
- A01-A32 全 PASS 的原始产物：仓库无 `deliverables/stage1-final/`，该历史声明不可复核；当前以 S1 联动报告与 `ACCEPTANCE=OK` 为权威。

