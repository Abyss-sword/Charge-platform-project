# 成员3 第二阶段最终交付报告（T05/T06/T09）

> 交付日期：2026-09-17；分支 `feature/member3-simulator-analytics-dashboard`；报告提交 `cf543ae`。
> 上机验证基线：`714cbaf`（首轮）→ `c78aec5`（接口覆盖修复）→ `39722b4`（Hive 报表复验）；本报告新增 `cf543ae`（规格口径修正，**尚未上机**）。
> 依据：`plans/00-总规格与决策.md` §5/§6/§7、`plans/01-技术实施与任务包.md`、`plans/07-新规格成员分工与交接补充-20260914.md` §4。
> 状态口径：只用 NOT_RUN / PASS / FAIL / BLOCKED / PARTIAL；降级另记 degraded。
> 复算步骤与逐个数字的出处见同目录 [`2026-09-17-recomputation.md`](2026-09-17-recomputation.md)。

## 1. 结论：严格区分四类

| 类别 | 结论 | 依据 |
|---|---|---|
| **本地单元/集成** | **通过** | `analytics/tests` + `bigdata` 共 **132 passed**（`cf543ae`，Windows 本机 Python 3.12）；此前上机基线为 106（`c78aec5`）、109（`39722b4`） |
| **真实 Hive / Spark on YARN 作业** | **通过** | MapReduce `job_1789486682725_0001` SUCCEEDED；Spark `application_1789486682725_0013` FINISHED/SUCCEEDED（YARN 执行，非 local 模式）；Hive 外部分区表 6715 行 / 19 站 |
| **真实数据** | **通过（含一项保留）** | 课程会话 CSV（MD5 `2BF780A4…`）单独发布为 `course-local-v1` / `course-local-20260915`；**保留项：与成员1的 `course-v1`（MD5 `d0d5d4f4…`）不是同一份输入** |
| **业务口径符合总规格** | **部分通过** | S2-05/06/07 与两类二维对比符合；**S2-09/S2-10 于 2026-09-17 才按规格改正，代码通过但未上机**；S2-08 BLOCKED |

**总体：成员3侧的算法与接口实现通过，但不宣称 S2-09/S2-10 已完成验收**——这两项的规格修正只有本地测试证据，缺 VM 实测数字（见 §5.1）。

## 2. 逐任务结论

| 任务 | 内容 | 状态 | 关键证据 |
|---|---|---|---|
| T05 | Hive 分区外部表 + 三条业务查询 | PASS | `01_tables.sql`、`02_reports.sql`；6715 行 / 19 站，覆盖 `2014-12-02T09:00Z`—`2015-10-04T07:00Z`，总电量 `10349.589999999990`；站点×小时、星期×小时（102 行）均已执行 |
| T05 | PySpark 特征与二维分析 | PASS | `build_features.py` 由 YARN 执行；观测 6715 行补齐为完整小时轴 90213 行，训练样本 29898 行，可用特征 13 维；缺失小时保持空值不补 0 |
| T06 | 1—24 小时负荷模型 | PASS | 时间顺序 70/15/15 切分，选模只看验证集；测试集 MAE 1h `0.637805` / 6h `0.854812` / 24h `0.683029`，均低于对应基线；实际产出 24 个时域 |
| T06 | Flask 正式分析 API | PASS | `/health`、`/forecast`、`/analysis/compare`、`/analysis/peak`、`/recommend` 五接口 200，`modelMode=TRAINED` |
| T06 | Hive 报表发布与 `/analysis/hive` | PASS | 真实 Hive 导出三视图 1335 / 6715 / 19 行；Flask 从版本化 JSON 消费，三类均 `source=HIVE` 且版本匹配；`/health` 同时校验其可用性与版本一致 |
| T06 | S2-09 高峰与预警（规格口径） | **NOT_RUN（代码通过）** | 2026-09-17 按 `00:§6` 重写为 Top3 + 训练集 P90 + Precision@3；132 项本地测试通过；**VM 未重训，`peakThresholds` 尚不存在** |
| T06 | S2-10 推荐（规格口径） | **NOT_RUN（代码通过）** | 2026-09-17 按 `00:§7` 改为规格权重并删除虚构等待分钟；契约冻结 `member3_recommendation_v1`；**VM 未复验** |
| T06 | S2-08 空闲桩预测 | **BLOCKED** | 来源无容量/占用列，`freePilesExpected` 恒 `null + NO_CAPACITY_DATA`；未用电量伪造桩占用 |
| T09 | 报告与复算证据 | PASS | 本报告 + 复算说明 + `member3-c78aec5/2026-09-16-vm-acceptance.md` + 仓库外证据包（SHA-256 已登记） |

## 3. S2 需求矩阵（成员3负责部分）

| 需求 | 状态 | 说明 |
|---|---|---|
| S2-05 Hive 真实查询 | PASS | 三条业务查询 + 两类二维对比，结果被 `/analysis/compare` 与 `/analysis/hive` 实际消费（满足 `00:§4`"Hive 查询导出的排行必须被 API 实际使用"） |
| S2-06 Spark 真实处理 | PASS | PySpark DataFrame 清洗、UTC 补全、缺失标记、滞后特征、多维聚合，由 `spark-submit` 提交至 YARN |
| S2-07 1/6/24 小时负荷预测 | PASS | 三个期限各有独立预测、基线、测试误差与来源信息，无复制同一数值 |
| S2-08 1/6/24 小时空闲桩预测 | **BLOCKED** | 无容量/占用标签；按 `00:§6`"无法获得占用标签时不得标为正式验收通过" |
| S2-09 未来高峰与预警 | **PARTIAL** | 口径已按规格改正（Top3 稳定排序 + 训练集 P90 时段 + Precision@3；预警只标"相对历史高负荷"、不自动控制设备），待上机产出阈值 |
| S2-10 车主推荐 | **PARTIAL** | 权重与同分次序已按规格改正、虚构等待分钟已删除；"预测可用比例"与 S2-08 同因阻塞 |
| 至少 10 个分析维度 | PASS | 登记 16 维，当前数据集 13 维可用，不可用项有明确原因 |
| 至少两类二维对比 | PASS | 站点×小时、星期×小时（102 行），均带 `datasetVersion`/`runId` |

## 4. 实测与审查发现并修复的缺陷

| # | 缺陷 | 影响 | 修复提交 | 测试 |
|---|---|---|---|---|
| M1 | 标签与服务端预测窗口错开一小时；`session_count`、`coverage_ratio` 取自目标小时构成未来信息 | 训练目标与 `/forecast` 返回的时段不是同一小时；特征含未来量 | `b7cfcd1` | `test_features.py` 标签/基线对齐与 24 小时不变量 |
| M2 | 验证集基线样本极少时，`model_mae < inf` 恒成立，被当成"打赢基线" | 稀疏小时轴上会虚报模型优势 | `b7cfcd1` | `test_stage2_model.py::test_missing_baselines_are_not_reported_as_a_win_over_the_baseline` |
| M3 | Beeline 变量写作 `-hivevar`；`CREATE DATABASE ... LOCATION` 依赖 HS2 对 HDFS 目录的写权限 | 文档照抄会失败/建库被拒 | `b7cfcd1` | 上机执行通过 |
| M4 | `station_coverage_v` 用 `UNIX_TIMESTAMP` 计算跨度，结果随会话时区变化 | 覆盖率不可复算 | `b7cfcd1` | 上机查询结果稳定 |
| M5 | 默认站点在第 10—14 小时时域没有模型站点编码，接口返回 400 | 把"局部模型覆盖不足"误报成参数错误 | `c78aec5` | Flask 回归 4 passed；`MODEL_STATION_H10_UNAVAILABLE` |
| M6 | 高峰口径为"预测均值 ×1.20"，非规格要求的 Top3 + 训练集 P90，且无 Precision@3 | 曲线整体抬高时筛不出高峰；与"相对历史高负荷"无关 | `cf543ae` | `test_stage2_model.py`、`test_flask_api.py` 高峰用例 |
| M7 | 推荐含按"充电中桩数 ×15"推算的虚构等待分钟并占 0.35 权重；缺"当前可用比例"项 | 违反 `00:§7`"不显示虚构等待分钟"；权重与规格全不符 | `cf543ae` | `test_recommend.py` 规格权重与无 wait 断言 |
| M8 | `train_stage2_models` 无任何测试覆盖 | 训练产物结构变更只能靠上机才暴露 | `cf543ae` | 新增 `test_stage2_train_bundle.py`（端到端） |

M8 的新增测试当场固化了一条真实约束：h=24 的标签清洗会吃掉验证段开头的 24 小时，原点数不足时该时域直接 `DATA_INSUFFICIENT`。真实数据量足够，但此前无人守这条边界。

## 5. 遗留与阻塞（如实）

### 5.1 本次改动未上机（最重要一项）

`cf543ae` 的 S2-09/S2-10 修正只有本地 132 项测试证据。具体缺口：

- VM 上现有模型产物由旧代码生成，**没有 `peakThresholds` 键**，因此 `/analysis/peak` 当前会返回 `warning.level=UNKNOWN` + `PEAK_THRESHOLD_NOT_PUBLISHED`。这是设计好的降级而非故障，Top3 排名仍可用，但"训练集 P90 高负荷时段"要重跑 `train.py --stage2` 才存在。
- `/recommend` 的新契约（`member3_recommendation_v1`）未在 VM 上取过真实响应。
- 因此 §2 中这两项记 **NOT_RUN**，不记 PASS。按 `07:§6`，没有命令、输入版本和实际结果的"已完成"只能记 NOT_RUN。

### 5.2 数据集口径不统一

VM 验收输入是本机课程 CSV（MD5 `2BF780A4…`），与成员1交付的 `course-v1`（MD5 `d0d5d4f4…`）不是同一份，已单独发布为 `course-local-v1` 以免冒用。若全组要求统一口径，需用同一套脚本在成员1的数据集上重跑（步骤见复算说明 §3，只需换 `--dataset-version` / `--run-id` / `--input`）。

### 5.3 S2-08 与"预测可用比例"

来源无容量/桩数/停用列，两项同因阻塞。解阻塞需接入含站级 EVSE 数的来源（`analytics/src/adapters/acn.py` 骨架已预留、未启用），代价是整条链路与既有证据全部重跑。

### 5.4 三张接口变更单待签

| 单号 | 内容 | 待办方 |
|---|---|---|
| ICR-20260915-01 | 预测窗口对齐（h 覆盖 `[T+h-1, T+h)`）；已补入总规格 §6 原文佐证 | 成员1批准、成员2页面复核；六份共享夹具时间轴需前移一小时 |
| ICR-20260917-01 | `/analysis/peak` 高峰与预警口径 | 成员1批准、成员2运营端消费 |
| ICR-20260917-02 | `/recommend` 规格权重与契约冻结 | 成员1批准、成员2推荐卡渲染 |

### 5.5 观测类型是分摊估计

数据为会话级（起止 + 会话总电量），小时电量按重叠秒数分摊。按 `00:§103` 必须标注 `SESSION_ALLOCATED`，不得称"逐小时实测"。

## 6. 复算与非作者复跑

- **复算说明**：[`2026-09-17-recomputation.md`](2026-09-17-recomputation.md) 给出从 HDFS 小时表到五个接口响应的逐步命令、每个数字的出处，以及可核对的预期值。
- **非作者复跑**：**NOT_RUN**。成员1的 S2-T24 复跑覆盖其自身链路，成员3侧尚未由非实现者独立复跑。建议在上机重训后一并安排，复跑者按复算说明执行即可。

## 7. 运行与恢复指引

```bash
# 环境（Ubuntu 虚拟机 bitdev）
export JAVA_HOME=/home/bit/opt/jdk1.8.0_261 HADOOP_HOME=/home/bit/opt/hadoop-3.2.1 \
       HIVE_HOME=/home/bit/opt/apache-hive-3.1.3-bin SPARK_HOME=/home/bit/opt/spark-3.4.1-bin-hadoop3 \
       HADOOP_CONF_DIR=$HADOOP_HOME/etc/hadoop
export PATH=$JAVA_HOME/bin:$HADOOP_HOME/bin:$HIVE_HOME/bin:$SPARK_HOME/bin:$PATH

# 全量成员3测试
PYTHONPATH=analytics analytics/.venv312/bin/python -m pytest analytics/tests bigdata -q

# 仅重跑训练（本次 cf543ae 上机所需的唯一一步；特征产物可复用）
PYTHONPATH=analytics analytics/.venv312/bin/python -m src.train --stage2 \
  --feature-dir runtime/analytics/stage2/course-local-v1/course-local-20260915 \
  --model-dir analytics/models/stage2
```

回退：全部改动在 `feature/member3-simulator-analytics-dashboard` 分支，按提交粒度 `git revert`（`cf543ae` / `1f16c72` / `c78aec5` / `b7cfcd1`）。回退 `cf543ae` 会重新违反总规格 §6/§7，不建议。产物目录 `analytics/models/` 与 `runtime/` 均被 `.gitignore` 忽略，回退代码不影响已发布产物。

## 8. 交付限制遵守声明

- 未改他人文件；第一阶段 FastAPI 教学接口的 `rank_stations` 原样保留，两套推荐口径并存且各有测试。
- 未用夹具、静态图或本地模式冒充真实 HDFS/Hive/PySpark/Flask 运行；Spark 作业有 YARN applicationId 可查。
- 未把接口返回 200 当作业务口径达标——正因如此本轮主动把 S2-09/S2-10 从 `PASS` 降为 `PARTIAL`。
- 未改 Decimal 精度规则，未把空值填 0；容量未知处一律 `null + NOT_COLLECTED/NO_CAPACITY_DATA`。
- 含 `userId` 的原始 CSV 只存于虚拟机受控目录，不入仓库、不入共享区；证据目录只放聚合与统计。
- 未 force push、未合并 main、未 reset --hard。
- 模型产物不入仓库；本地 `analytics/models/` 中的三个 joblib 是 9 月 10 日的 Leeds 旧模型，不得当作第二阶段产物。
