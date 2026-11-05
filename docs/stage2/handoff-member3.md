# 成员1 → 成员3 交接包：hourly 契约与运行证据（T03/T04）

> 交接日期：2026-09-14；交接人：成员1；接收人：成员3（T05 Hive/PySpark、T06 模型/Flask）。
> 依据：`docs/superpowers/plans/stage2-complete-20260912/07-新规格成员分工与交接补充-20260914.md` §2"交给成员3"。
> 原则：成员1 只提供版本化 HDFS 输入、字段契约与质量报告；Hive 建表/业务 SQL、PySpark 特征、模型与 Flask 均归成员3，成员1 不代写。

## 1. 数据版本与路径

| 项 | 值 |
|---|---|
| datasetVersion | `course-v1` |
| runId（当前 active） | `course-001` |
| 来源命名空间 | `course`（课程真实充电会话数据，2014-11 ~ 2015-10） |
| dataMode | `REAL_ANONYMIZED` |
| staging 输入路径 | `/charge_platform/staging/course-v1/course-001/hourly_fragments.tsv` |
| hourly 结果路径 | `/charge_platform/hourly/course-v1/course-001/part-r-00000`（含 `_SUCCESS`） |
| 本地 active manifest | `runtime/analytics/active_manifest.json`（激活指针，原子替换） |

## 2. 字段契约（staging TSV 6 列，无表头；对应 `station_hour_v1`）

| 列 | 字段 | 说明 |
|---|---|---|
| col1 | station_id | 带命名空间站点，如 `course:369001`（禁止凭编号映射到本地业务站点） |
| col2 | hour_utc | UTC 整点 `YYYY-MM-DDTHH:00:00Z` |
| col3 | energy_kwh | 十进制文本，**未提前舍入**；两位 HALF_UP 在发布层 |
| col4 | session_count | 会话开始计数；跨小时会话只在起始小时计一次 |
| col5 | coverage_seconds | 每站区间并集秒数（0–3600，非多桩求和） |
| col6 | observation_kind | **`SESSION_ALLOCATED`（会话分摊估计）**；图表与评估必须注明，不得称逐小时实测 |

hourly 输出为 6 列（同 staging 列序），由 Java MR（BigDecimal）复算，与 Python Decimal 参考完全一致（`MR_REFERENCE=OK ROWS=6700`）。

## 3. ⚠️ 关键限制（成员3 必须据此判定 S2-08）

- **观测类型是分摊估计**：本数据集为会话级（起止 + 会话总电量），小时电量是按重叠秒数分摊的估计值。`00:103` 要求图表注明 `SESSION_ALLOCATED`，不得称"逐小时实测"。
- **无容量标签**：数据无容量/桩数/停用列，`capacity_piles`、`occupied_piles`、`available_piles` 在发布产物中一律为 `null`，原因 `NOT_COLLECTED`。
  - 因此 **S2-08 空闲桩预测必须标 BLOCKED**，`freePilesExpected` 只能返回 `null + NO_CAPACITY_DATA`。
  - 解阻塞途径：接入含站级 EVSE 数的来源（如 Caltech ACN，适配器骨架已在 `analytics/src/adapters/acn.py` 预留，未启用）。
- **无金额**：来源 `totalFees` 为二进制浮点长尾且无单位列，`amount_yuan` 一律 `null + NOT_COLLECTED`，不发布金额。
- **站点范围**：来源 105 站，仅保留满足 `distinct_days>=28 且 records>=50` 的 **19 站**（规则可复现，写于 `analytics/sources/course/manifest.json`）。不得整批声明"≥28 天连续覆盖"。

## 4. 质量与拒绝统计

| 项 | 值 |
|---|---|
| 来源输入 | 3340 行（105 站） |
| 适配通过 | 1812 行（19 站） |
| 适配拒绝 | 1 行（`ANOMALY`：会话时长 55.238h > 24h，主原因唯一、诊断标签 `DURATION_GT_24H`） |
| 站点范围外 | 1527 行（未入选站点，单独记 `excluded`，非数据缺陷） |
| 质量门 | `valid_rate=1.0`（阈值 0.95），`reconciled=true`，已发布 |
| 小时片段 | 6700 行（staging = hourly，逐行一致） |

## 5. 运行命令与作业证据

```bash
# 来源适配（本地受控区，含 userId 的原始文件不外流）
PYTHONPATH=analytics analytics/.venv312/bin/python -m src.adapters.course_sessions \
  --input /home/bit/data/controlled/course/source-raw.csv \
  --output runtime/controlled/course/run-001.csv \
  --rejected runtime/controlled/course/run-001-rejected.jsonl \
  --stats runtime/controlled/course/run-001-stats.json \
  --amount-crosscheck runtime/controlled/course/run-001-amount.json

# 治理→HDFS→发布（上传失败即中止，不伪造成功）
PYTHONPATH=analytics analytics/.venv312/bin/python -m src.run_governance \
  --manifest analytics/sources/course/manifest.json \
  --source-csv runtime/controlled/course/run-001.csv \
  --run-id course-001 --runtime-root runtime

# Java 小时聚合（真实 YARN）
hadoop jar bigdata/mapreduce/target/charge-hourly.jar org.group29.charge.HourlyJob \
  --input /charge_platform/staging/course-v1/course-001 \
  --output /charge_platform/hourly/course-v1/course-001
```

| 作业 | ID | 结果 |
|---|---|---|
| Java HourlyJob（真实数据） | `job_1789301464634_0006` | SUCCEEDED，VALID_ROWS=6700、INVALID_ROWS=0 |
| Spark 只读冒烟（环境锁定用） | `application_1789301464634_0010` | SUCCEEDED，SPARK_ROWS=6700 |

## 6. 成员3 可直接使用的环境（已就绪）

- Hive 3.1.3：HS2 在 `jdbc:hive2://localhost:10000`（`doAs=false`）；成员1 只读冒烟已建 `smoke_db.hourly_course_smoke` 外部表，成员3 应建正式分区外部表。
- Spark 3.4.1：`spark-submit --master yarn --deploy-mode client` 已验证可用。
- 运行前置：`export JAVA_HOME=/home/bit/opt/jdk1.8.0_261 HADOOP_HOME=/home/bit/opt/hadoop-3.2.1 HADOOP_CONF_DIR=$HADOOP_HOME/etc/hadoop`。

## 7. 证据位置（交付）

`deliverables/stage2/acc4569/`：`data-quality/`（质量与拒绝）、`hadoop-jobs/`（applicationId、MR 参考比对、HDFS 树）、`hive-spark-smoke/`（四项冒烟与版本锁定）、`environment.md`。
