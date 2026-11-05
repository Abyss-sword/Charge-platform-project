# 第二阶段基线登记（T01，成员1）

> 登记日期：2026-09-13；**2026-09-14 更新基线统一与真实数据接入**。本文件记录实际仓库状态、旧报告对应关系与 S2 需求追踪起点。
> 原则：只有实际执行并观察通过的项目才可填 PASS；未执行的为 NOT_RUN，缺依赖的为 BLOCKED。

## 1. 实际仓库状态

| 项 | 值 |
|---|---|
| 候选仓库 | `E:/Software_and_Data/charge_platform_stage1_candidate_local` |
| 统一基线提交 | `acc4569`（`integration/stage2-acceptance` 合并提交：汇合 `feature/stage1-linkage-20260913` 的 T01–T04 + 第一阶段联动 S1-A~F 与 `feature/member1-stage2-data-infra` 的 T08/T09；两条线共同祖先 `c255e09`） |
| 工作分支 | `integration/stage2-acceptance`（仅本地提交，不 push）；后续修复提交 `652cff3`（隐私黑名单）、`8551802`（真实数据适配器）、`877b501`（WAL 备份）、`2a98231`（HDFS 失败中止） |
| 归集后 mode 位 | 原 6 个文件可执行位在归集分支上均为 `100755`（合并时已正确保留），**无需再单独恢复** |
| 旧登记（已由上文取代） | 原基线 `742ac34` / `feature/member1-stage2-data-infra`；`stage1-linkage` 线含第一阶段三端联动改造 |

> 注：本节原记录"基线提交 742ac34、工作分支 feature/member1-stage2-data-infra、6 个 mode 变化文件未提交"为 2026-09-13 的历史状态；2026-09-14 按 02 文档分支策略新建集成分支 `integration/stage2-acceptance` 统一两条线，mode 位已在合并中恢复。

## 2. 旧验收报告对应关系（不盲合地基）

| 报告 | 对应提交 | 与本基线的关系 |
|---|---|---|
| `deliverables/stage1-final/acceptance.md`、`test-results.md`（2026-09-10 双平台验收 45/45、pytest 23/23） | `integration/stage1-final-20260909` 合并后的候选工作树（含 `9ba7592`） | 742ac34 = 该线 + stage2 运维脚本提交；本次以其为继续基线 |
| 评审记录 A2：`charge_platform_project_vm` main（b1d3ab7）"已含二阶段地基" | main 另一条线（telemetry/alarm foundation） | **本次不合并不重验**：candidate 线已有等价遥测/告警实现（迁移 005/007/008/009 与服务层）；避免双线盲合 |
| `docs/test-results/2026-09-12-member1-stage2-delivery.md` | 742ac34 | 即本基线父提交的交付记录（5 个脚本 + bash -n + ctest），作为脚本审查起点而非"已完成"证明 |

不一致清单：① 旧报告对应提交与 742ac34 相差一个 stage2 脚本提交（已知、已审）；② main 线与 candidate 线的双"地基"问题按评审记录 A2 冻结处理，不自动合并；③ 6 个 mode 变化文件是检出差异，非功能改动。

## 3. 第一阶段未结项核对

第一阶段验收报告标注全部通过（45/45）。评审记录 A5 曾指出 TC-05/18/19 未修复、TC-20/21/22/24 未执行——属第一阶段归档范围，本次按"T08 复跑第一阶段未结用例"以当前提交重跑相关回归（ctest 全量 + e2e），不在本文件重复判定。

## 3a. T08 实际核查发现的缺陷与修复（2026-09-13）

以下均为在 HEAD 742ac34 上实测复现、随后最小修复并复测通过的项（对应"文件存在不是完成"与 A5 类陈旧报告问题）：

| # | 缺陷 | 复现 | 修复 | 复测 |
|---|---|---|---|---|
| D1 | 全新库迁移重放失败：`007_meter_baseline_and_telemetry_idempotency.sql` 与 `008` 重复 `ADD COLUMN meter_start_kwh`；该 007 从未进入已验收库的迁移链 | `member1_flow_test` 报 `duplicate column name: meter_start_kwh`；`reset-demo-database.sh` 同样失败 | 将该 007 移入 `database/migrations/superseded/` 停用；其承载的 `pile_telemetry.source_mode`（sqlite_dashboard 依赖）并入 008 | 迁移重放成功；e2e `ORDER=FINISHED/REVENUE 0.00→10.27` |
| D2 | `TelemetryService::record()` 从不填充 `TelemetryResult.orderId/orderEnergyKwh`（结构体与路由响应自 M1-03 起存在，实现缺失），测试与三端联动无法对上订单号 | `telemetry_service_test::lifetimeMeterIsIdempotent...` 断言失败 | record() 挂接 CHARGING 订单时回报订单号与累计用量 | telemetry_service_test 9/9 |
| D3 | 遥测幂等重复响应 `duplicate` 标志恒为 false | 服务测试与协议测试断言失败 | exactDuplicate 返回路径置 `duplicate=true` | 两测试通过 |
| D4 | `telemetry_protocol_test` 未登录即 `ORDER_RESERVE`，被 `AUTH_INVALID` 拒（测试落后于 M1-02 会话契约） | 协议级复现 `AUTH_INVALID` | 测试补 USER_LOGIN 并为订单动作携带 userToken | telemetry_protocol_test 8/8 |
| D5 | `start-all.sh` 重复运行覆盖 PID 记录、无端口占用检查、无就绪等待 | 重复 start 掩埋旧 PID | 拒绝重复启动（exit 3）、端口占用检测（exit 4，报告占用进程）、等待端口就绪、失败回收本次启动进程 | S2-T19 各分支退出码 3/4 实测 |
| D6 | `prepare-analytics-data.sh` 校验 manifest 参数后忽略之，永远跑 SIMULATED demo | 读脚本+实测 | 按 `contract` 分派：`historical_interval_v1` 走治理发布链 | manifest 路径实测通过 |
| D7 | `init-demo.sh` 无路径防护、可重置运行中业务库 | 根路径/源码目录/运行中重置均放行 | 拒绝根级/源码目录/目录目标（exit 2）；存在存活项目进程时拒绝重置（exit 3） | S2-T20 实测 2/2/3 |
| D8 | `scripts/bigdata-preflight.sh` 初版版本比对模式写错（"3.2.1" vs "Hadoop 3.2.1"） | PREFLIGHT=FAIL | 子串匹配 | PREFLIGHT=OK |

回归基线更新：修复后 VM 全量 ctest **36/36 通过**（HEAD 上该套件因 D1 无法建库，从未真正整体通过）。

## 4. S2 需求追踪（登记起点；状态随各任务包更新）

| 需求 | 内容摘要 | 负责人 | 关联测试ID | 状态（2026-09-13起点） |
|---|---|---|---|---|
| S2-01 | 保留第一阶段全业务 | 成员1/2 | S2-T01 | NOT_RUN |
| S2-02 | 保留运营闭环 | 成员1/2 | S2-T02 | NOT_RUN |
| S2-03 | 真实数据治理 | 成员1 | S2-T03/T04/T05/T06 | NOT_RUN（真实来源许可未确认，夹具链路可验收） |
| S2-04 | Hadoop真实参与 | 成员1 | S2-T07/T23 | NOT_RUN |
| S2-05 | Hive真实查询 | 成员3 | S2-T08 | NOT_RUN |
| S2-06 | Spark真实处理 | 成员3 | S2-T09 | NOT_RUN |
| S2-07 | 1/6/24小时负荷预测 | 成员3 | S2-T10/T11/T15 | NOT_RUN |
| S2-08 | 1/6/24小时空闲桩预测 | 成员3 | S2-T11/T12 | NOT_RUN |
| S2-09 | 未来高峰与预警 | 成员3 | S2-T13 | NOT_RUN |
| S2-10 | 车主推荐 | 成员3/2 | S2-T14 | NOT_RUN |
| S2-11 | Qt/Web联动 | 成员2 | S2-T16/T17/T22 | NOT_RUN |
| S2-12 | 运维与交付 | 成员1 | S2-T18/T19/T20/T21/T23/T24 | NOT_RUN |

> 成员3补充（2026-09-15，不改动上表 09-13 的登记值）：S2-05、S2-06、S2-07、S2-09、S2-10 与 T06 的代码已在 `b7cfcd1` 落地，并经静态审查修正八项问题；真实执行仍为 `NOT_RUN`——Hive/Spark/YARN 未执行，无 applicationId、无特征产物、无模型产物。S2-08 维持 `BLOCKED`（数据集无容量列）。审查发现、统一后的预测口径与上机证据清单见 [`../test-results/2026-09-15-member3-stage2-code-review.md`](../test-results/2026-09-15-member3-stage2-code-review.md)。
>
> 成员3更新（2026-09-16）：上一条的 `NOT_RUN` 已作废。S2-05、S2-06、S2-07、S2-09、S2-10、T06 已在 Ubuntu 虚拟机完成真实链路执行，MapReduce `job_1789486682725_0001` 与 Spark on YARN `application_1789486682725_0013` 均 `SUCCEEDED`，1/6/24 小时模型测试 MAE 全部低于基线，五个 Flask 接口全部 200，记录见 [`../../deliverables/stage2/member3-c78aec5/2026-09-16-vm-acceptance.md`](../../deliverables/stage2/member3-c78aec5/2026-09-16-vm-acceptance.md)。S2-08 仍 `BLOCKED`。两点请成员1在集成验收时注意：验收数据集是本机课程 CSV 单独发布的 `course-local-v1`，与你交付的 `course-v1` MD5 不同，若要求统一口径需在你的数据集上重跑；另外 Hive 报表发布链路（`export_reports.py`、`/analysis/hive`）为新增且尚未上机，`/health` 已将其纳入判定。
>
> 成员3更新（2026-09-17，规格口径核对）：按《00-总规格与决策》§6/§7 逐条核对后，S2-09 与 S2-10 由 `PASS` 降为 `PARTIAL`——此前依据只是接口 200。S2-09 的高峰已按规格重写为 Top3（负荷降序、时间升序稳定排序）+ 训练集 P90 高负荷时段 + Precision@3，预警只标"相对历史高负荷"、不声称电网超载、不自动控制设备，接口字段变更见 [`../ICR-20260917-01-peak-topn-and-p90-warning.md`](../ICR-20260917-01-peak-topn-and-p90-warning.md)，需上机重跑 `train.py --stage2` 才会产出 `peakThresholds`。S2-10 的推荐已按规格 §7 重写：权重改为距离 0.30 / 价格 0.20 / 当前可用 0.25 / 预测可用 0.25（缺失项剔除后重新归一），删除了按充电中桩数 ×15 推算的虚构等待分钟，同分依次按距离、空闲桩、站点 ID，契约冻结为 `member3_recommendation_v1`，见 [`../ICR-20260917-02-recommendation-spec-weights.md`](../ICR-20260917-02-recommendation-spec-weights.md)；其中"预测可用比例"与 S2-08 同因阻塞于无容量标签。T09 已按成员1的交付格式补齐最终报告与复算说明：[`../../deliverables/stage2/member3-cf543ae/2026-09-17-member3-final-report.md`](../../deliverables/stage2/member3-cf543ae/2026-09-17-member3-final-report.md) 与 [`2026-09-17-recomputation.md`](../../deliverables/stage2/member3-cf543ae/2026-09-17-recomputation.md)，后者给出步骤 1—7 的逐条命令与每个数字的出处，非实现者可直接照此复跑；成员3侧的非作者复跑（S2-T24 对应项）仍为 `NOT_RUN`。另外 ICR-20260915-01 已补入总规格 §6 原文佐证（"在整点 T 发布未来 24 个小时区间 `[T,T+1h)` 至 `[T+23h,T+24h)`"），据本文件 §5"冲突时以总规格为准"，共享夹具需按总规格回正。
>
> 需成员1处理（2026-09-15）：`contracts.md` §4 的 `targetStart`/`targetEnd` 两句自相矛盾，已提单 [`../ICR-20260915-01-forecast-window-alignment.md`](../ICR-20260915-01-forecast-window-alignment.md) 按 `targetEnd = T+24h` 澄清。连带六份共享夹具 `tests/fixtures/stage2/forecast/*.json` 的时间轴需前移一小时，成员2的 Qt 契约测试断言随之更新。该单待成员1批准、成员2页面复核。

## 5. 执行前置条件状态（2026-09-14 更新）

| 前置条件 | 状态 |
|---|---|
| SSH 免密 `bit@192.168.41.128` | ✅ 已核验（BatchMode 通过） |
| VM 资源 | 2 vCPU / 7.7GiB（可用 5.1GiB）/ 根分区 443GiB 空闲 |
| Hadoop 3.2.1 + JDK 1.8.0_261 | ✅ 伪分布式已装，五进程在线，`/charge_platform` 七目录在 |
| stage2 工作副本 | ✅ `/home/bit/projects/stage2_acceptance`（tar+scp 同步，无 .git） |
| Python | ✅ **3.12.13** 为主（`analytics/.venv312`，`requirements-lock-py312.txt` 锁定 38 项，pytest 67 通过）；3.10 系统版保留兼容 |
| Maven | ✅ `/home/bit/opt/apache-maven-3.9.9`（JDK 8 下 `mvn -v` 正常） |
| Hive 3.1.3 / Spark 3.4.1 | ✅ **已安装并完成四项冒烟锁定版本**（HDFS 上传 + Java 作业 + Hive 外部表 + Spark 读同一路径；见 `deliverables/stage2/acc4569/hive-spark-smoke/`） |
| 真实数据 | ✅ **已接入课程真实会话数据**（3340 行/105 站/2014-2015，`course-v1`）；⚠️ 许可原文链接待补（提供方书面说明），补齐前原始 raw 不进共享区 |
| 空闲桩预测标签 | ❌ 该数据集无容量/桩数/停用列（`capacity_piles`/`occupied_piles` 只能 null + NOT_COLLECTED），S2-08 保持 BLOCKED |

## 6. 证据目录约定

证据根：`deliverables/stage2/acc4569/`（baseline.md、requirements.csv、environment.md、test-results/、hadoop-jobs/、data-quality/、hive-spark-smoke/、script-tests/、loadtest/、defense/）。禁止私钥、令牌、完整手机号、未授权 raw 数据入内；含 userId 的原始文件只存于本地受控区 `/home/bit/data/controlled/course/`，不入仓库、不入共享区。
