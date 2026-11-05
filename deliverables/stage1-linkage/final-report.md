# 第一阶段三端联动改造最终交付报告（S1-A ~ S1-F）

分支：`feature/stage1-linkage-20260913`（自审查基线 `c255e09` 切出）
基线：`docs/stage1-audit/` 四文件（00 审查报告 / 01 修改规格 / 02 Agent 提示与交付限制）
交付日期：2026-09-14（VM 内实际执行日期）

## 1. 结论总览

| 任务 | 内容 | 状态 |
|---|---|---|
| S1-A | 服务端联动契约（operationId/changeVersion/serverTimeUtc + snapshotVersion/snapshotAtUtc、linkage_meta、扩展 ADMIN_OVERVIEW、ADMIN_REVENUE_SUMMARY） | 完成，测试通过 |
| S1-B | 运营端（总览重建、营收四范围、枚举候选栏、陈旧版本守卫、3s 可见刷新、在途禁用） | 完成，测试通过 |
| S1-C | 车主端（地图静态兜底卡、头像 5MB 校验+服务端回读、中文桩标签） | 完成，测试通过 |
| S1-D | Web（snapshotVersion 透传、旧快照丢弃、来源栏快照版本/时间展示） | 完成，测试通过 |
| S1-E | 集成验收（协议驱动三条全流程 ACCEPTANCE=OK；GUI 截图证据归档） | 完成（车主端 GUI 登录遗留 UI 缺陷，见 §6） |
| S1-F | 全面核验（全量回归 + 非作者复跑 + 需求映射 + 本报告） | 完成 |

## 2. 提交清单（feature/stage1-linkage-20260913）

- `cf78333` fix: unblock fresh-DB migration replay and telemetry order contract（前置修复：迁移重放、遥测幂等 duplicate 标志）
- `225843b` feat(server): linkage contract + enriched admin overview（S1-A）
- `1818151` feat(admin): overview rebuild, revenue ranges, enum combos, stale guard, 3s refresh（S1-B）
- `6489a17` feat(user): map static card, avatar validation + server re-read, Chinese pile labels（S1-C）
- `00935d7` feat(dashboard): snapshotVersion + stale-response drop（S1-D）
- `6960f07` feat(acceptance): UI screenshot hook, WAL mode, acceptance driver（S1-E）
- `9097e4e` fix(acceptance): allow ACC_DB_PATH/ACC_SERVER_PORT/ACC_DASHBOARD_API overrides（非作者复跑时发现的驱动可用性修复）

## 3. 需求映射（00 报告 P0/P1/P2 × 状态）

### P0（不关闭不得验收）
| 要求 | 状态 | 证据 |
|---|---|---|
| 写操作统一经 C++ 事务；响应带 operationId/changeVersion/serverTimeUtc | 已关闭 | `tests/integration/linkage_contract_test.cpp` 5 用例；验收驱动每步记录 OP-*/changeVersion 严格递增（`report.txt` B1=744→C3 close=756；非作者复跑 `rerun-report.txt` 对应 B1=892→C3 close=904） |
| 查询带 snapshotVersion/snapshotAtUtc | 已关闭 | 同上；FastAPI /dashboard payload 增 snapshotVersion（pytest 52 通过） |
| 三端围绕同一 SQLite 库联动（非单端变化） | 已关闭 | 驱动 A3：admin 改桩后用户侧 `userPile101Status=FAULT` 且 Web `FAULT count=1` 同周期变化；B7：支付后运营汇总 0.60 与 Web trend 0.60 同步；C3：工单 CLOSED 三端一致 |
| 重复支付只扣一次 | 已关闭 | 驱动 B6：同一 requestId 两次支付，`wallet_transaction` 仅 1 条 PAYMENT，余额 200.00→199.40（差 0.60 一笔）；非作者复跑独立 SQL 复核一致 |
| 状态由服务端校验（禁止越权） | 已关闭（既有+保持） | 既有 order/pile 状态机校验未回退；驱动 B3 开始前复核状态 |

### P1
| 要求 | 状态 | 证据 |
|---|---|---|
| 地图失败静态兜底卡 | 已关闭 | `flow_pages.cpp` showStaticFallback；站点/经纬度/距离/外部地图入口/“模拟起点”标注；离线加载失败→fallback（QtTest 覆盖状态机） |
| 告警→工单闭环 | 已关闭 | 驱动 C 流程：过温→告警→ACKNOWLEDGED→PROCESSING→RESOLVED→CLOSED（`report.txt` changeVersion ack/start/resolve/close=752/753/754/756；复跑 `rerun-report.txt`=900/901/902/904） |
| 总览/营收口径 | 已关闭 | OverviewPage 指标卡标注“时间·口径·快照版本”；RevenuePage 仅已支付口径、两位小数、四范围 |
| 固定枚举改候选栏 | 已关闭（范围内） | 站点/桩类型、站点状态、桩状态 QComboBox（中文显示、协议值提交）；枚举映射 `shared/ui/enum_labels` 有 QtTest |

### P2
| 要求 | 状态 |
|---|---|
| 空/陈旧/错误/离线状态 | 已关闭：OverviewPage Stale 态（快照版本回退判旧）、PageState 五态 |
| 图表提示/布局 | 已关闭：环图/折线/柱图 + 4 张 5 行清单表 |
| 头像压缩 | 已关闭：PNG/JPEG/WebP 过滤、5MB 上限、方形裁剪、服务端回读渲染 |
| Web 旧响应丢弃 | 已关闭：adapter `snapshotVersion < previous` → 丢弃并显示陈旧提示（vitest 用例覆盖） |

### NOT_RUN / BLOCKED（如实标注）
- 车主端 GUI 手工登录路径：BLOCKED（UI 缺陷，见 §6）。业务断言由协议驱动覆盖，不影响 P0/P1 结论。
- 真实充电桩/真实地图 API 联动：NOT_RUN（教学环境无真机；地图兜底用确定性离线触发验证）。
- Hadoop/Hive/Spark 大数据任务（T05/T06）：本阶段范围外，待成员 3。

## 4. 全量回归终验（VM 全新目录 /home/bit/projects/stage1_linkage_sync，HEAD 9097e4e 归档同步）

| 套件 | 结果 | 日志 |
|---|---|---|
| cmake+build（236 targets） | 成功（62 条 warning，均为既有 missing-field-initializers 类，无新增错误） | final-logs.tgz: build.log |
| ctest -j1 | 37/37 通过 | ctest-final.log |
| pytest（analytics） | 52 通过（2 条既有 httpx 弃用警告） | pytest-final.log |
| vitest（dashboard） | 13/13 通过 | vitest-final.log |
| mvn test（bigdata/mapreduce） | 6/6 通过 BUILD SUCCESS | mvn-final.log |
| e2e basic_charge_flow.sh | ORDER=FINISHED / PILE=IDLE / PAYMENT_COUNT=1 / REVENUE 0.00→10.27 | 同 ctest 运行 |

## 5. 非作者复跑（general-purpose 子代理）

- 复跑环境：VM `/home/bit/projects/stage1_linkage_sync`，`scripts/reset-demo-database.sh` 重建种子库（3 站/12 桩/20 用户/30 单/3 告警/3 工单，changeVersion=1，pile_telemetry=0），服务端 `--port 48090`，FastAPI 8010 只读 `/dashboard`。
- 结果：`ACCEPTANCE=OK`，0 条 CHECK FAILED（rerun-report.txt / rerun-steps.json / rerun-driver.log）。
- 独立 DB 复核（不经驱动）：`wallet_transaction` 该订单仅 1 条 PAYMENT；`maintenance_ticket` 最新一条 CLOSED；终态 changeVersion=906。
- 复跑过程发现并修复驱动可用性问题：DB/端口/URL 原为写死常量，已改为 `ACC_DB_PATH/ACC_SERVER_PORT/ACC_DASHBOARD_API` 环境变量（9097e4e），默认值不变，向后兼容。
- 复跑教训（已写入复跑日志）：验收库必须真正重置——桩 102 存在历史读数 100.50 时驱动发 100.00 会被服务端按“表计倒转”拒绝（TELEMETRY_OUT_OF_ORDER/METER_RESET_SUSPECTED 语义），这是正确的服务端行为，不是缺陷。

## 6. 遗留问题

1. **车主端 GUI 登录 UI 缺陷（P2 级，不阻塞 P0/P1）**：通过 xdotool/XTEST 向登录页输入后，服务端已建立会话（user_session 1 条），但 UI 未切换到主窗口；怀疑 `UserApplicationFlow` 的 loginFinished 信号处理或 QScrollArea 内输入焦点问题。协议层登录正常（驱动 A0 双端登录成功）。未修复——按 02 文档要求如实标注，未用 mock 掩盖。
2. **车主端 GUI 截图仅登录页**：gui-shots/user-login-page.png；运营端总览/营收/链式刷新截图完整（admin-revenue-live/chained/summary.png）。
3. **T05/T06（Hive/Spark 业务落地）**：Hive 3.1.3 与 Spark 3.4.1 已安装并冒烟通过，业务实现待成员 3。
4. **真实数据 ACN 许可**：待用户确认，本阶段全部使用 SIMULATED dataMode。

## 7. 运行与复验指引

```bash
# VM 内（/home/bit/projects/stage1_linkage_sync）
bash scripts/reset-demo-database.sh runtime/acc-final.sqlite          # 干净种子库
CHARGE_PLATFORM_DB=$PWD/runtime/acc-final.sqlite ./build/charge_platform_server --port 48090 &
cd analytics && CHARGE_PLATFORM_DB=$PWD/../runtime/acc-final.sqlite \
  .venv/bin/python -m uvicorn src.api:app --port 8010 &               # 只读 /dashboard
ACC_DB_PATH=$PWD/runtime/acc-final.sqlite ACC_DASHBOARD_API=http://127.0.0.1:8010/dashboard \
  ACC_OUT_DIR=/tmp/acc-evidence analytics/.venv/bin/python scripts/acceptance-driver.py
# 期望末行：ACCEPTANCE=OK

# 回归
ctest --test-dir build -j1
analytics/.venv/bin/python -m pytest analytics/tests -q
cd dashboard && npm run test:run
mvn -f bigdata/mapreduce/pom.xml test
BUILD_DIR=build bash tests/e2e/basic_charge_flow.sh
```

## 8. 回退方式

- 全部改动在 `feature/stage1-linkage-20260913` 分支，main/集成分支未合并；`git revert` 按提交粒度回退即可。
- 迁移 `010_linkage_meta.sql` 向后兼容（新表+种子行），旧库可无损打开；去掉 010 后 linkage 层读不到表会回退版本 0，不影响既有业务。

## 9. 交付限制遵守声明

- 未 reset --hard、未 force push、未覆盖演示库（验收库独立命名 acc-final.sqlite，脚本自动备份 .bak.*）。
- FastAPI/Web 仅只读（mode=ro + query_only）；全部写操作经 C++ 服务事务。
- 无 OFFLINE_DEMO/mock/固定数字作正式证据；未实际执行项均标注 NOT_RUN/BLOCKED。
- 未推送远程；合并/推送待用户授权。
- 证据目录不含密钥/令牌/完整手机号（驱动日志手机号为种子号 13800009999，属教学种子数据）。
