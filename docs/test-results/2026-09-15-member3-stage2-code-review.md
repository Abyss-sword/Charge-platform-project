# 成员3第二阶段代码静态审查与修复（T05/T06 上机前）

> 审查日期：2026-09-15；审查人：成员3；被审提交：`b7cfcd1`（Hive SQL、PySpark 特征、stage2 模型、Flask 分析服务，14 个文件 1462 行新增）。
> **本文件不是 T05/T06 的验收记录。** 审查在 Windows 本机完成，未连接 Hive、Spark 和 YARN，因此不产生 applicationId、作业日志和模型产物，任何结论都不得记作 `PASS`。

## 1. 审查条件与手段

| 项 | 值 |
|---|---|
| 本机环境 | Windows，Java 22，`analytics/.venv` 未安装 pyspark |
| 为何不能真实执行 | Spark 3.4.1 只支持 Java 8/11，Windows 还缺 Hadoop native 依赖；Hive/YARN 在 `bit@192.168.41.128` |
| 可用输入 | 成员1交付的 20 行 hourly 样本（`deliverables/stage2/acc4569/hadoop-jobs/hourly-course-001-sample.tsv`），不足以驱动完整作业 |
| 实际手段 | 逐文件静态审查 + 契约一致性核对（Spark 写入端与 Flask 读取端）+ 本地纯函数单测 + pyflakes/py_compile |
| 数据事实依据 | `course-001-stats.json`：1812 条会话、19 站、覆盖 2014-12-02 至 2015-10-04；hourly 6700 行（约 4.8% 的小时有观测） |

## 2. 发现与处置

| # | 发现 | 严重度 | 处置 |
|---|---|---|---|
| 1 | `02_reports.sql` 注释写 `-hivevar`，beeline 只认 `--hivevar`。变量不注入时 `${hivevar:dataset_version}` 成为字面量，三条业务查询静默返回 0 行且退出码为 0 | 高（假通过） | 已修：改为 `--hivevar` 并写明后果；`01_tables.sql` 补完整运行示例 |
| 2 | `CREATE DATABASE` 指定 `LOCATION '/charge_platform/hive/warehouse/stage2.db'`，该路径从未被验证；成员1冒烟建的 `smoke_db` 走的是默认 warehouse。若 HiveServer2 运行用户对 `/charge_platform` 无写权限，建库即失败 | 高（首次执行失败） | 已修：去掉自定义 LOCATION，走默认 warehouse；注释说明改回自定义位置时需先确认权限 |
| 3 | 24 个时域窗口 union 后，`training` 被重复触发：counts 一次、写 parquet 一次、导出时 count 与 toJSON 各一次，整条 DAG 至少重算三四遍；`spark.sql.shuffle.partitions` 默认 200 对 6700 行是纯开销。VM 仅 2 vCPU、可用内存 5.1GiB | 高（表现为长时间无进展） | 已修：`parsed`/`completed`/`training` 三处 persist；shuffle 分区默认 8 并可用 `--shuffle-partitions` 覆盖；导出复用已算行数 |
| 4 | 本地导出预算 25 万行，实际约 16 万行（24 × 约 6700），余量仅三成；换更大 runId 或放宽站点筛选即触发 `LOCAL_EXPORT_BUDGET_EXCEEDED` 中止 | 中 | 已修：默认提到 40 万，超限提示改为"确认驱动机有空间再调大"，manifest 记录 `localExportBudget` |
| 5 | 标签泄漏与训练推理错位（同一根因）：h=1 的标签取当前小时电量（`lead(h-1)`），而同一行特征含该小时的 `session_count`、`coverage_ratio`；小时电量本就按 `coverage_seconds` 分摊得出，等于把答案喂进特征。同时 Flask 把目标定在 `hour_start + h`，比训练标签整体晚一小时 | 高（指标不可辩护） | 已修：见第 3 节口径统一 |
| 6 | 基线不可比时自动判定模型胜出：稀疏轴上 lag24 大量缺失，验证集若无可用基线样本则 `baseline_mae = inf`，`val_model_mae < inf` 恒成立，`selectedMode` 无条件为 `TRAINED`。与"预测不足不造假"冲突 | 高（结论失真） | 已修：新增 `MINIMUM_BASELINE_SAMPLES = 30`、`baselineComparable`、`selectionCaveat=NO_COMPARABLE_BASELINE`，Flask `reasons` 同步暴露 |
| 7 | Hive 自检的 `DECIMAL(38,12)` 求和不会等于成员1冒烟表 DOUBLE 求得的 `10349.589999999990` | 中（对账误判） | 已修：`01_tables.sql` 自检查询上方写明口径差异；README 同步 |
| 8 | `station_coverage_v` 用 `UNIX_TIMESTAMP(hour_utc, "...'Z'")`，`'Z'` 是被转义的字面量，实际按服务器本地时区解析。取 max 减 min 时固定偏移抵消，Asia/Shanghai 无夏令时故结果正确，但换有夏令时的时区会偏一小时 | 低 | 已修：改为 `DATEDIFF` 加整点差的纯整数算法，不再依赖时区 |

### 已核实无需处置

- 源文本 27 位小数进 `DECIMAL(38,12)`：Spark 与 Hive 都按 HALF_UP 截到 12 位，不会变 `null`，两侧口径一致。
- `weekday_monday_0` 定义：Spark 的 `pmod(dayofweek+5, 7)` 与 Hive 的 `pmod(datediff(day,'1970-01-05'), 7)` 都以周一为 0。
- `F.pmod` 是 PySpark 3.4 新增，环境为 3.4.1，可用。
- MR 输出目录里的 `_SUCCESS` 会被 Spark 与 Hive 自动忽略。
- 分区用显式 `ADD PARTITION ... LOCATION`，表根设为父目录不会重复读数据。
- 模型产物契约两端对齐：`trustedProjectArtifact`、`datasetVersion`/`runId` 校验、`artifacts` 映射、`baseline_yesterday_hNN` 字段名，写入端（`train.py`）与读取端（`forecast_service.py`）一致。

## 3. 统一后的预测口径

依据 `docs/stage2/contracts.md`："`originTime` 预测原点 T，全部特征只能使用 T 之前已知数据"。

| 项 | 定义 |
|---|---|
| 原点 T | 特征行所代表小时 `[t0, t0+1)` 的结束时刻 `t0+1h`；Spark 以 `origin_time` 发布，Flask 优先读取，缺失时退回 `hour_start + 1h` |
| 标签 | 时域 h 对应 `[t0+h, t0+h+1)`，即 `[T+h-1, T+h)`；`lead(h)` |
| 昨日同小时基线 | `lag(24-h)`，h=24 落在原点行本身 |
| 不变式 | `lead(h) + lag(24-h) = 24`，由 `bigdata/spark/tests/test_features.py` 参数化守住 |
| 训练样本过滤 | 原点小时与目标小时都必须有观测 |
| 契约吻合 | h=1 紧贴原点不留空档；h=24 的 `targetEnd` 正好为 T+24h，与契约对 `targetStart`/`targetEnd` 的描述一致 |

口径同时写入 `feature-manifest.json` 的 `originConvention`、`labelRule`、`baselineRule`、`trainingRowFilter`，答辩时无需翻代码。

## 4. 未处置项（需决策后再动）

`STAGE2_FEATURE_NAMES` 保持原样，未加入原点小时自身的电量。口径修正后它已是合法且通常最强的特征（现有最近特征只到 `lag1_kwh` 即 T-1，`rolling_mean_3_kwh` 为原点前三小时），加入很可能影响模型能否真正打赢基线，但会变更模型契约，故留待决策。

## 5. 本地复验记录

| 项 | 命令 | 结果 |
|---|---|---|
| 分析层与 Spark 纯函数单测 | `analytics/.venv/Scripts/python.exe -m pytest analytics/tests bigdata/spark/tests -q` | 106 通过（审查前 100，新增 6） |
| 静态检查 | `pyflakes` + `py_compile`（改动的 4 个 Python 文件） | 无告警；pyflakes 为临时安装，检查后已卸载 |

新增断言：`lead(h) + lag(24-h) = 24` 不变式；基线全空时必须给出 `NO_COMPARABLE_BASELINE` 且 `baselineComparable` 为假；样本充足时不得有 caveat；`/forecast` 首点 `targetStart` 等于 `originTime`、末点结束等于 T+horizon；训练侧 caveat 必须出现在接口 `reasons`。

**Spark 的 `build_frames` 与 `run_job`、以及全部 Hive SQL 未被执行覆盖**，本地单测只覆盖纯函数与契约。

## 6. 上机执行顺序与证据清单

前置：`export JAVA_HOME=/home/bit/opt/jdk1.8.0_261 HADOOP_HOME=/home/bit/opt/hadoop-3.2.1 HADOOP_CONF_DIR=$HADOOP_HOME/etc/hadoop`。

1. `beeline -u jdbc:hive2://localhost:10000 --hivevar dataset_version=course-v1 --hivevar run_id=course-001 -f bigdata/hive/01_tables.sql`（先确认建库不报权限错，自检行数应为 6700、站点 19）
2. 同样参数执行 `02_reports.sql`，保存三条业务查询与两类二维分析输出
3. `spark-submit --master yarn --deploy-mode client bigdata/spark/build_features.py`，记录 applicationId 与 `SPARK_FEATURES=PASS` 前的全部计数行
4. `python -m src.train --stage2`，保存 `stage2-evaluation.json` 与各时域 `selectedMode`/`selectionCaveat`
5. 起 Flask 并采集 `/health`、`/forecast`、`/recommend`、`/analysis/compare`、`/analysis/peak` 响应样例
6. 归档到 `deliverables/stage2/<commit>/`，并在 T09 报告中写明 S2-08 的 `BLOCKED` 原因、基线不可比的时域、以及第 7 项口径差异

任一步失败都按实际失败记录，不得用本地静态审查结论替代。
