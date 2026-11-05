# 成员3 复算说明（T09）

> 编写日期：2026-09-17；对应报告 [`2026-09-17-member3-final-report.md`](2026-09-17-member3-final-report.md)。
> 目的：让**非实现者**能独立跑出报告里的每一个数字，并在数字不一致时定位到具体环节。
> 原则：只写实际执行过的命令。未执行过的步骤在 §6 单独列出并标 NOT_RUN，不混进可复算清单。

## 1. 前置条件

| 项 | 值 | 检查命令 |
|---|---|---|
| 虚拟机 | Ubuntu `bitdev`（上轮地址 `192.168.192.132`，成员1文档记为 `192.168.41.128`，**登录前先确认当前网段**） | `hostname -I` |
| Hadoop | 3.2.1，五进程在线 | `jps`（应见 NameNode、DataNode、SecondaryNameNode、ResourceManager、NodeManager） |
| Hive | 3.1.3，HiveServer2 监听 `127.0.0.1:10000` | `ss -ltn \| grep 10000` |
| Spark | 3.4.1 | `spark-submit --version` |
| Python | 3.12，虚拟环境 `analytics/.venv312` | `analytics/.venv312/bin/python -V` |
| 输入小时表 | `/charge_platform/hourly/course-local-v1/course-local-20260915` | `hdfs dfs -ls <path>` |

```bash
export JAVA_HOME=/home/bit/opt/jdk1.8.0_261 HADOOP_HOME=/home/bit/opt/hadoop-3.2.1 \
       HIVE_HOME=/home/bit/opt/apache-hive-3.1.3-bin SPARK_HOME=/home/bit/opt/spark-3.4.1-bin-hadoop3 \
       HADOOP_CONF_DIR=$HADOOP_HOME/etc/hadoop
export PATH=$JAVA_HOME/bin:$HADOOP_HOME/bin:$HIVE_HOME/bin:$SPARK_HOME/bin:$PATH
export DV=course-local-v1 RID=course-local-20260915
cd /home/bit/projects/<工作副本>
```

上游的数据治理与 MapReduce 小时聚合属成员1链路，其复算见 `deliverables/stage2/acc4569/defense/non-author-rerun.md`。本文件从 HDFS 小时表开始。

## 2. 逐步复算

### 步骤 1：Hive 外部分区表

```bash
beeline -u jdbc:hive2://127.0.0.1:10000/default \
  --hivevar dataset_version=$DV --hivevar run_id=$RID -f bigdata/hive/01_tables.sql
```

预期：分区挂载成功，自检查询返回 **6715 行 / 19 站点**，覆盖 `2014-12-02T09:00:00Z`—`2015-10-04T07:00:00Z`，总电量 `10349.589999999990`。

> 变量必须写 `--hivevar`（双横线）。写成 `-hivevar` 不会注入，`${hivevar:dataset_version}` 被当字面量，查询会**静默返回 0 行**——这是最容易误判成"数据丢了"的坑。
> 总电量末位与成员1的 `10349.59` 不同是预期的：Hive 侧工作列为 `DECIMAL(38,12)`，成员1侧为 `DOUBLE`，差异只在展示末位，不是对账错误。

### 步骤 2：三条业务查询与两类二维对比

```bash
beeline -u jdbc:hive2://127.0.0.1:10000/default \
  --hivevar dataset_version=$DV --hivevar run_id=$RID -f bigdata/hive/02_reports.sql
```

预期：站点日电量、绝对小时负荷排名、站点覆盖率三条业务查询均返回非空；星期×小时二维对比 **102 行**。

### 步骤 3：Hive 报表导出为版本化 JSON

```bash
analytics/.venv312/bin/python bigdata/hive/export_reports.py \
  --beeline $HIVE_HOME/bin/beeline \
  --jdbc-url jdbc:hive2://127.0.0.1:10000/default \
  --dataset-version $DV --run-id $RID \
  --output-dir runtime/analytics/stage2/$DV/$RID
```

预期：生成三份报表 JSON 与 `hive-report-manifest.json`，行数为 **站点日电量 1335 / 绝对小时负荷排名 6715 / 站点覆盖率 19**。写入为临时目录 + `os.replace` 原子替换，中断不会留下半份报表。

### 步骤 4：PySpark 特征工程（YARN）

```bash
spark-submit --master yarn --deploy-mode client \
  bigdata/spark/build_features.py \
  --dataset-version $DV --run-id $RID \
  --input /charge_platform/hourly/$DV/$RID \
  --output /charge_platform/features/$DV/$RID \
  --local-report-dir runtime/analytics/stage2/$DV/$RID
```

预期：YARN 状态 `FINISHED / SUCCEEDED`；输入观测 **6715** 行；补齐后完整小时轴 **90213** 行，其中仍为真实观测 **6715** 行；训练样本 **29898** 行；可用特征 **13** 维。

核对要点：缺失小时必须是空值 + 缺失标记，不能是 0；`rolling_mean_*` 只能用预测原点之前的数据。

```bash
yarn application -list -appStates ALL | grep member3-features   # 取 applicationId
```

### 步骤 5：训练 1—24 小时模型

```bash
PYTHONPATH=analytics analytics/.venv312/bin/python -m src.train --stage2 \
  --feature-dir runtime/analytics/stage2/$DV/$RID \
  --model-dir analytics/models/stage2
```

预期标准输出含 `STAGE2_TRAIN=PASS`，以及：

| 时域 | 发布模式 | 测试集模型 MAE | 测试集基线 MAE |
|---|---|---:|---:|
| 1 小时 | TRAINED | 0.637805 | 0.866064 |
| 6 小时 | TRAINED | 0.854812 | 1.054351 |
| 24 小时 | TRAINED | 0.683029 | 0.750154 |

生成 `stage2-evaluation.json` 与 24 个 `course_load_h{01..24}.joblib`。

> **`cf543ae` 起新增三个顶层键**：`peakRule`、`peakThresholds`（每站训练集 P90）、`peakEvaluation`（Precision@3）。若产物中没有这三个键，说明用的是旧代码产物，`/analysis/peak` 会以 `warning.level=UNKNOWN` 降级——此时必须重跑本步骤，不能改接口去"补"阈值。
> MAE 可复现的前提：`random_state=42`、固定超参、按时间切分。若数字漂移，先查输入 `datasetVersion`/`runId` 是否同一份，再查是否误用随机划分。

### 步骤 6：Flask 五接口 + Hive 接口

```bash
export CHARGE_STAGE2_FEATURE_DIR=$PWD/runtime/analytics/stage2/$DV/$RID
export CHARGE_STAGE2_MODEL_DIR=$PWD/analytics/models/stage2
export CHARGE_BUSINESS_SNAPSHOT=$PWD/runtime/analytics/business_snapshot.json
PYTHONPATH=analytics analytics/.venv312/bin/python -m src.flask_api &

curl -s "127.0.0.1:8001/health"
curl -s "127.0.0.1:8001/forecast?horizon=24"
curl -s "127.0.0.1:8001/analysis/compare?kind=weekday_hour"
curl -s "127.0.0.1:8001/analysis/hive?kind=station_daily_energy"
curl -s "127.0.0.1:8001/analysis/peak?horizon=24"
curl -s "127.0.0.1:8001/recommend?limit=3"
```

预期：全部 HTTP 200。`/health` 为 `status=OK` 且特征、模型、Hive 报表三者版本一致；`/forecast` 返回连续 24 点且 `modelMode=TRAINED`；`/analysis/compare` 102 行；`/analysis/hive` 三类均 `source=HIVE`。

`/forecast` 的窗口口径核对（ICR-20260915-01）：`points[0].targetStart == originTime`，`targetEnd == originTime + horizonHours`，即 h=1 紧贴原点。

`/analysis/peak` 的口径核对（ICR-20260917-01，**需先完成步骤 5 的重训**）：`peaks` 恰 3 条且 `rank` 连续 1..3；每条 `targetStart` 能在同一次 `/forecast?horizon=24` 中找到且数值一致；`highLoadWindows` 内每小时电量严格大于 `threshold.thresholdEnergyKwh`；`warning.basis=TRAINING_SET_P90` 且 `warning.automaticControl=false`。

`/recommend` 的口径核对（ICR-20260917-02）：`scoring.declaredWeights` 为 0.30/0.20/0.25/0.25；每条 `appliedWeights` 之和为 1；响应中不含任何等待字段；`origin.arrivalTimeUsed=false`。

### 步骤 7：全量测试

```bash
PYTHONPATH=analytics analytics/.venv312/bin/python -m pytest analytics/tests bigdata -q
```

预期：**132 passed**（`cf543ae`）。历史基线：106（`c78aec5`）、109（`39722b4`）。

## 3. 换成成员1的 `course-v1` 数据集复算

链路本身与数据集无关，只需替换三个参数并从步骤 1 起重跑：

```bash
export DV=course-v1 RID=course-001
# 步骤 4 的 --input 改为 /charge_platform/hourly/course-v1/course-001
```

MAE 与行数会随输入变化，**不应期望与本报告数字相同**。此时应核对的是口径而非数值：切分方式、基线定义、缺失不补 0、窗口对齐、高峰 Top3 与 P90、推荐权重。

## 4. 每个数字的出处

| 数字 | 出处 | 复算步骤 |
|---|---|---|
| 6715 行 / 19 站 / `10349.589999999990` | Hive 外部分区表自检查询 | 步骤 1 |
| 102 行（星期×小时） | `02_reports.sql` 二维对比 | 步骤 2 |
| 1335 / 6715 / 19 | `export_reports.py` 三视图行数 | 步骤 3 |
| 90213 / 6715 / 29898 / 13 维 | `build_features.py` 标准输出 | 步骤 4 |
| `application_1789486682725_0013` | YARN application 列表 | 步骤 4 |
| `job_1789486682725_0001` | 成员1链路的 MapReduce 作业（上游） | 见成员1复跑记录 |
| MAE 0.637805 / 0.854812 / 0.683029 | `stage2-evaluation.json` 的 `test.modelMae` | 步骤 5 |
| 五接口 200 与 `modelMode=TRAINED` | curl 响应 | 步骤 6 |
| 132 passed | pytest 汇总行 | 步骤 7 |
| MD5 `2BF780A4…` | 原始 CSV（仅存虚拟机受控目录，不入仓库） | `md5sum` |

## 5. 已知的环境性偏差（非产品缺陷）

1. **工作副本无 `.git`**：同步方式为 tar + scp，因此 VM 上无法用 `git revert`；回退需在开发机操作后重新同步。
2. **模型与运行时产物不入仓库**：`analytics/models/`、`runtime/` 被 `.gitignore` 忽略，新副本必须自己跑步骤 4—5，不能指望产物随代码到位。
3. **本地 Windows 无法复算步骤 1—4**：本机未装 Hadoop/Hive/Spark，且本机 Java 版本与 Spark 3.4.1 不兼容。Windows 上只能复算步骤 7 的 pytest。
4. **`/recommend` 依赖成员1的业务快照**：缺 `CHARGE_BUSINESS_SNAPSHOT` 指向的文件时返回 `DEPENDENCY_UNAVAILABLE` + HTTP 503，属既定语义而非故障。
5. **总电量末位差异**：Hive `DECIMAL(38,12)` 与成员1 `DOUBLE` 的展示末位不同，已在步骤 1 说明。

## 6. 尚不可复算的项（NOT_RUN）

| 项 | 原因 | 解除条件 |
|---|---|---|
| `peakThresholds` / `peakEvaluation` 的真实数值 | `cf543ae` 未上机，VM 现有产物由旧代码生成 | 执行步骤 5 重训 |
| `/analysis/peak` 与 `/recommend` 的新契约真实响应 | 同上 | 执行步骤 5 后再跑步骤 6 |
| 非作者复跑结论 | 成员3侧尚未由非实现者独立复跑 | 复跑者按本文件步骤 1—7 执行并记录实际值 |
| 三端同站点/同原点/同版本一致性 | 依赖成员2的 Qt 与 Web 就位、成员1组织签收 | 全组联调 |
| 空闲桩预测数值 | 来源无容量/占用列，`BLOCKED / NO_CAPACITY_DATA` | 接入含站级 EVSE 数的数据源 |
