# 成员3 第二阶段答辩稿

## 目录

- [我负责哪些方面](#我负责哪些方面)
- [Hive 查询与业务报表](#hive-查询与业务报表)
- [Spark 特征工程](#spark-特征工程)
- [负荷预测](#负荷预测)
- [空闲桩预测](#空闲桩预测)
- [高峰与预警](#高峰与预警)
- [动态推荐](#动态推荐)
- [Flask 分析接口](#flask-分析接口)
- [报告与复算](#报告与复算)

---

## 我负责哪些方面

第二阶段负责离线分析链：在成员1已经发布到 HDFS 的站点×小时表上，完成查询、特征、模型、接口和口径，把可核对的预测、高峰、推荐和报表交给成员2的 Qt / Web 调用。

数据流是 **HDFS 小时表 → Hive 报表 / PySpark 特征 → 树模型产物 → Flask 只读 API**。这条链和在线充电业务分开。订单、电桩状态仍以成员1的 SQLite / TCP 为准；分析数字必须带 `datasetVersion`、`dataMode`、`source`，不能把课程历史电量说成当天营收。分析服务关掉时，预约和结算仍应能走。

不负责：HDFS 与 MapReduce 小时表（成员1）；Qt 页面与 Web 大屏绑定（成员2）；订单、支付、电桩控制（成员1 服务端）。

具体是下面八块。请按块提问，后文按同名板块展开。

| 负责板块 | 这块实际做了什么 | 状态 |
|---|---|---|
| [Hive 查询与业务报表](#hive-查询与业务报表) | 外部分区表；日电量、小时负荷排名、站点覆盖率；两类二维对比；报表导出后由 API 读取 | PASS |
| [Spark 特征工程](#spark-特征工程) | YARN 上补齐小时轴、缺失不补 0、滞后/滚动特征、训练样本与 13 维特征清单 | PASS |
| [负荷预测](#负荷预测) | 1 / 6 / 24 小时负荷模型，对比昨日同小时基线 | PASS |
| [空闲桩预测](#空闲桩预测) | 接口保留空闲桩字段；无容量数据，返回 null 并标明原因 | BLOCKED |
| [高峰与预警](#高峰与预警) | 未来 24 小时 Top3 高峰、训练集 P90 高负荷窗口、相对历史高负荷预警 | PARTIAL |
| [动态推荐](#动态推荐) | 按距离、价格、当前/预测可用比例给当前运营站打分，不编造等待分钟 | PARTIAL |
| [Flask 分析接口](#flask-分析接口) | `/health`、`/forecast`、对比、Hive 报表、高峰、推荐六条只读接口 | PASS |
| [报告与复算](#报告与复算) | 最终报告与逐步复算说明 | PASS |

---

## Hive 查询与业务报表

**代码：** `bigdata/hive/01_tables.sql`、`bigdata/hive/02_reports.sql`、`bigdata/hive/export_reports.py`

### 用了什么技术

- **Hive 3.1.3 外部分区表**：表只挂元数据，文件仍在 HDFS `/charge_platform/hourly/<dataset>/<run>`，不把 MapReduce 结果再拷进 warehouse。
- **动态分区** `dataset_version` + `run_id`：不同批次物理隔离，查询必须带版本，避免把两批小时表加在一起。
- **视图 + 窗口函数**：日聚合用 `GROUP BY` 日期截断；小时排名用 `DENSE_RANK() OVER (PARTITION BY 版本, 小时 ORDER BY 电量 DESC, 站号 ASC)`。
- **Beeline TSV2**：`export_reports.py` 用 `--outputformat=tsv2` 拉视图，解析时丢掉 JDBC 握手日志，只认列头那一行。
- **原子发布**：先写临时目录，再用 `os.replace` 换成正式 JSON，避免 Flask 读到半文件。

### 实现了什么功能

- 站点日电量 1335 行；小时负荷排名 6715 行；站点覆盖率 19 行
- 二维对比：站点×小时热力、星期×小时（102 行）
- Flask `GET /analysis/hive` 读导出 JSON，`source=HIVE`；`/health` 要求报表与特征同一 `datasetVersion` / `runId`

表规模：6715 行 / 19 站，`2014-12-02T09:00Z`–`2015-10-04T07:00Z`，总电量 `10349.589999999990`。数据集 `course-local-v1`。

### 具体怎么实现

1. `CREATE EXTERNAL TABLE station_hour`，六列对应 `station_hour_v1`：站号、UTC 整点、`DECIMAL(38,12)` 电量、会话数、覆盖秒、观测类型。分隔符 Tab，无表头。
2. `ALTER TABLE ... ADD PARTITION` 把变量 `${hivevar:dataset_version}` / `run_id` 指到对应 HDFS 子目录。Beeline 必须 `--hivevar`；写成 `-hivevar` 变量不注入，`${hivevar:...}` 当字面量，查询静默 0 行但退出码仍是 0。
3. 建库不写自定义 `LOCATION`，落在 HS2 默认 warehouse（成员1冒烟库已验证）。自定义到 `/charge_platform` 时 HS2 用户可能无写权限。
4. 日电量：`SUBSTR(hour_utc,1,10)` 取 UTC 日再 `SUM(energy_kwh)`。
5. 覆盖率：先按站取 `MIN/MAX(hour_utc)`。`hour_utc` 是定长 ISO，字典序等于时间序。跨度用 `DATEDIFF(日) * 24 + 整点差 + 1`，不用 `UNIX_TIMESTAMP`（依赖 HS2 本地时区和夏令时）。
6. 电量求和走 Decimal HALF_UP 到 12 位。成员1冒烟若用 DOUBLE，总电量会带 `10349.589999999990` 这种浮点长尾，对账看行数和数量级。
7. 导出时 `dataset_version`/`run_id` 只允许 `[A-Za-z0-9._-]`，拼进 SQL 前先校验，防止注入。`SET hive.local.time.zone=UTC` 后再 `SELECT`。
8. 契约名 `member3_hive_reports_v1`。Flask 不在请求里连 Hive，只读这份发布物。

---

## Spark 特征工程

**代码：** `bigdata/spark/build_features.py`

### 用了什么技术

- **Spark 3.4.1 PySpark DataFrame**，`spark-submit --master yarn`，不是 `local`。上机作业 `application_1789486682725_0013` SUCCEEDED。
- **显式 schema 读 TSV**：`DecimalType(38,12)` 对齐 Hive，避免把电量读成 double。
- **按站 `sequence(min,max,1 hour)` 补齐轴**，再 `left join` 观测；缺测行 `observation_kind=MISSING`，电量保持 null。
- **窗口函数** `Window.partitionBy(station_id).orderBy(hour_start)`：`lag` / `rowsBetween` 只在本站内滑，禁止跨站把 A 站历史接到 B 站。
- **persist + 小 shuffle**：中间帧缓存；`spark.sql.shuffle.partitions` 默认 8（2 vCPU 上默认 200 分区全是调度开销）。

### 实现了什么功能

- 观测 6715 行 → 完整小时轴 90213 行；训练样本 29898 行
- 13 维可用特征：`hour_of_day`、`weekday_monday_0`、`is_weekend`、`month`、`day_of_month`、`week_of_year`、`session_count`、`coverage_ratio`、`lag1_kwh`、`lag24_kwh`、`rolling_mean_3_kwh`、`rolling_mean_24_kwh`、`station_code`
- 登记 16 维，容量相关维无来源，写明不可用
- `feature-manifest.json` 钉死原点、标签、基线规则，训练和 Flask 共用

### 具体怎么实现

1. 读入后校验：站号/时间/电量非空且电量 ≥ 0；覆盖秒在 0–3600；`(station_id, hour_start)` 无重复。失败直接抛 `INVALID_HOURLY_ROWS` / `DUPLICATE_STATION_HOURS`。
2. 缺测小时：电量 null；`session_count`/`coverage_seconds` 填 0 只表示「这一小时没有观测」，**电量绝不填 0**，否则模型会把停运学成低负荷。
3. 日历特征从 UTC `hour_start` 取。星期用 `pmod(dayofweek+5, 7)` 把 Spark 周日=1 转成周一=0，与契约一致。
4. **原点 T** = `hour_start + 1h`，即特征小时 `[t0, t0+1)` 的结束时刻。该小时自身的电量、会话、覆盖在 T 之前已经发生，可以进特征。
5. **标签**：时域 h 对应 `[t0+h, t0+h+1)`，也就是 `[T+h-1, T+h)`。实现上 `lead` 行数是 h。h=1 紧贴原点，h=24 的 `targetEnd` 正好是 T+24h。
6. **昨日同小时基线** `lag(24-h)`：h=24 时 lag=0，基线就是原点行，不看未来。不变式 `lead(h)+lag(24-h)=24`。
7. 滚动均值窗口 `rowsBetween(-3,-1)` / `(-24,-1)`，**不含当前行**，避免把「此刻电量」泄漏进「预测此刻之后」。
8. 训练行过滤：原点小时和目标小时都必须有观测。补齐轴约 95% 是空小时，不过滤则 lag/标签几乎全空。
9. 会话电量按重叠秒数分摊到小时，观测类型 `SESSION_ALLOCATED`，不是逐小时电表。站号 `course:369001` 这种命名空间，禁止映射到 SQLite 教学站。

---

## 负荷预测

**代码：** `analytics/src/model.py`、`analytics/src/train.py`

### 用了什么技术

- **HistGradientBoostingRegressor**（sklearn）：直方图梯度提升，把连续特征先分箱再分裂，适合带缺失的表格时序；原生把 NaN 送到单独分支，不必先填 0。
- 第二阶段超参：`max_depth=6`、`learning_rate=0.06`、`max_iter=180`、`l2_regularization=0.1`、`random_state=42`。浅树防过拟合，固定种子可复现。
- **时间顺序切分 + 标签清洗**：按 `feature_available_at` 排序后 70/15/15；训练集/验证集还要求 `target_end` 不超过该段右边界，避免 h=24 的标签跨进下一段。
- **两套基线**：昨日同小时、近 3 小时均值。验证集上选更好的那个再和模型比。
- 产物 **joblib**：`course_load_h01.joblib` … `h24`，内含估计器和 `station_codes`。目录 `analytics/models/stage2/`，`.gitignore` 不进仓库。

### 实现了什么功能

对 1 / 6 / 24 小时分别给出负荷预测、基线和测试误差（实际训了 1–24 全部时域，接口按规格暴露三个）。上机测试 MAE：

| 时域 | MAE | 对比 |
|---|---|---|
| 1h | 0.637805 | 低于基线 |
| 6h | 0.854812 | 低于基线 |
| 24h | 0.683029 | 低于基线 |

选模只看验证集；测试集数字只用于报告，不回头改超参。

### 具体怎么实现

1. 特征矩阵列顺序冻结为 `STAGE2_FEATURE_NAMES` 13 列，写入模型清单。推理不得另排，否则和训练错位。
2. `station_code` 按站号排序后的稳定整数编码，和估计器一起序列化。
3. 切分：去重原点时刻，70% 和 85% 处切两刀。原点落在训练段但标签 `target_end` 越过边界的样本丢掉（h=24 会吃掉验证段开头约一天，短序列会 `DATA_INSUFFICIENT`）。
4. 每个 horizon 单独 `fit`，标签是该时域的 `target_energy_kwh`，不是把 1 小时模型重复 24 次。
5. 验证集上算模型 MAE 与两个基线 MAE。可比基线样本 < 30 或基线非有限值时，记 `selectionCaveat=NO_COMPARABLE_BASELINE`，**不把「模型 < inf」说成打赢基线**。
6. 验证集模型更好才 `selected_mode=TRAINED` 并落盘；否则该时域推理走基线。
7. HTTP 路径只 `joblib.load`，不在请求里训练、不连 YARN。

---

## 空闲桩预测

**代码：** `analytics/src/forecast_service.py`、`analytics/src/flask_api.py`、`analytics/src/adapters/acn.py`（预留未启用）

### 用了什么技术

`forecast_v1` 里预留整数或 null 字段 `freePilesExpected`。容量本应从站级 EVSE 数来；课程小时表没有 `capacity_piles` / `occupied_piles`。ACN 适配器按 Caltech 站点信息映射容量，契约命名空间已留，数据未下载、未跑证据。

### 实现了什么功能

规格要求 1 / 6 / 24 小时空闲桩。当前每个预测点该字段为 `null`，`nullReason=NO_CAPACITY_DATA`。状态 BLOCKED，接口仍 200，曲线里负荷照常、空闲桩明确不可用。

### 具体怎么实现

1. 发布层不把电量、会话数、覆盖率换算成占用：连接 ≠ 充电，无容量时无法定义空闲。
2. **0 和 null 语义不同**：0 = 确定没有空闲桩；null = 未采集。填 0 会被前端画成「全满」。
3. 推荐里的「预测可用比例」同源阻塞，该项不参与打分，权重摊到其余指标。
4. 解阻塞：接入含 EVSE 数的来源 → 重跑 hourly / 特征 / 训练 / 发布，不能只改 Flask 一行公式。

---

## 高峰与预警

**代码：** `analytics/src/model.py`、`analytics/src/train.py`、`analytics/src/forecast_service.py`、`analytics/src/flask_api.py`；变更说明 `docs/ICR-20260917-01-peak-topn-and-p90-warning.md`

### 用了什么技术

- **稳定排序 Top3**：负荷降序、`targetStart` 升序；比较用 `Decimal(str(energyKwh))`，不用 float，避免 `0.1+0.2` 并列排乱。
- **训练集分位数 P90**：`numpy` 分位，按站、按不重复目标小时，与当前预测曲线隔离。
- **Precision@3**：同一原点 24 点窗口内，预测高峰集合与观测高峰集合的命中比例。
- **窗口合并**：扫描 24 个有序点，严格高于阈值的连续小时合成一段。接口 `GET /analysis/peak`。

### 实现了什么功能

- 未来 24 小时 Top3 高峰时段
- 该站历史高负荷线（训练集 P90）及连续越线窗口
- 评测 Precision@3 写入训练产物
- 预警只标「相对历史高负荷」，建议现场值守或错开非紧急充电；不控制设备

代码已按规格改完。VM 上旧产物若无 `peakThresholds`，预警为 `UNKNOWN`（降级）。未上机重训，不记 PASS。

### 具体怎么实现

1. `rank_peak_hours`：丢掉电量为 null 的点，再 `(-电量, 时间)` 排序，截前 3，带 `rank=1..3`。
2. `training_peak_thresholds`：只用训练切分里的真实电量；同一 `target_end` 只计一次；样本 < 20 不发布该站阈值。
3. `_high_load_windows`：`energy > P90` 才计入（等于阈值不算高峰）。中间缺测小时打断窗口，不把两段连成一段。
4. `precision_at_peak`：每个原点窗口预测 Top3 与观测 Top3 做集合交集；窗口短于 3 个有值点则跳过，避免短窗虚高。
5. `warning.level`：有阈值且出现越线窗 → `RELATIVE_HIGH_LOAD`；无阈值 → `UNKNOWN` + `PEAK_THRESHOLD_NOT_PUBLISHED`。`automaticControl=false`。
6. 不把曲线整体 ×1.20 当高峰：均值抬高时筛不出尖峰，也和「相对历史」无关。

---

## 动态推荐

**代码：** `analytics/src/recommend.py`、`analytics/src/business_snapshot.py`、`analytics/src/flask_api.py`；变更说明 `docs/ICR-20260917-02-recommendation-spec-weights.md`

### 用了什么技术

- **只读业务快照**（成员1 `business-snapshot`）：当前站名、坐标、单价、桩状态。与 `course:*` 历史分析站分开。
- **Min-max 归一 + 加权和**：成本项（距离、价格）反向，使 1 分永远表示更好。全程 `Decimal`，排序不抖。
- **缺项重归一**：每个候选只用自己有的指标，权重按 `w_i / Σw_available` 缩放。
- 契约 `member3_recommendation_v1`，接口 `GET /recommend`。

### 实现了什么功能

最多 3 个当前运营站，带分数和中文理由。权重：距离 0.30、价格 0.20、当前可用 0.25、预测可用 0.25。无虚构等待分钟。预测可用因无容量为空，权重摊到其余项。未在 VM 用新契约复验，不记 PASS。

### 具体怎么实现

1. `build_dynamic_candidates`：按站聚合桩；全故障站剔除；当前可用 = 空闲桩 / 容量（两位字符串）。不计算「充电中 ×15 分钟」。
2. 距离用快照坐标对查询点（默认演示点可覆盖）；缺距离的站在同分时排最后。
3. 四项全空的站 `excludedForNoCriteria`，不进榜。某站缺价只丢掉价格维，不把缺价当 0 元。
4. 全员某指标相同则该维人人得 1（拉不开差距就不惩罚）。
5. 理由只报得分突出的维（超过内部阈值），便于车主端展示「更近 / 更便宜 / 当前更空」。
6. 同分键：`score` → `distanceKm` → `idlePiles` → `stationId`。
7. `arrivalTime` 必须是 ISO8601，非法 400；能解析也写入 `arrivalTimeReason`：未参与打分（到达小时可用比例需要容量标签）。
8. 第一阶段教学 FastAPI 仍用旧 `rank_stations`（含等待分钟），两套并存，互不覆盖。

---

## Flask 分析接口

**代码：** `analytics/src/flask_api.py`、`analytics/src/forecast_service.py`

### 用了什么技术

- **Flask** 同步只读服务，默认 `127.0.0.1:8001`（`CHARGE_FLASK_HOST/PORT` 可改），`threaded=True`。
- 契约 **`forecast_v1`**（camelCase JSON）。`ForecastPublication` 启动时定位特征目录与 `stage2` 模型目录，请求路径只反序列化。
- 响应头：`Cache-Control: no-store`、`X-Content-Type-Options: nosniff`、CORS `*`。未知路由 / 500 也返回 JSON `DEPENDENCY_UNAVAILABLE`，不把 Flask HTML 错误页给 Qt。
- `ValueError` 统一 400 `PARAM_INVALID`。

### 实现了什么功能

| 接口 | 功能 |
|---|---|
| `GET /health` | 特征、模型、Hive 报表是否就绪且版本一致；`modelMode=TRAINED` |
| `GET /forecast` | 指定站 1/6/24 小时负荷点列；空闲桩无数据则为 null |
| `GET /analysis/compare` | 模型 vs 昨日同小时（及近 3 小时）基线 |
| `GET /analysis/hive` | 日电量 / 排名 / 覆盖率 |
| `GET /analysis/peak` | Top3、P90 窗口、预警 |
| `GET /recommend` | 动态推荐 |

上机上述接口 200。高峰阈值、推荐新字段依赖重训后的产物。

### 具体怎么实现

1. `ForecastPublication` 读 Spark 导出的 JSONL + manifest；优先用行内 `origin_time`，与特征作业同一套 T。
2. `/forecast?horizon=&station=`：horizon 只允许 1/6/24。对该站从原点起拼 `horizon` 个点，每点 `targetStart/targetEnd/energyKwh/meanPowerKw/freePilesExpected`。能量两位字符串。
3. 时域 h 覆盖 `[T+h-1, T+h)`。前端必须用返回的时间字段画轴，不能用 `horizonHours` 自己加钟点（Qt `rejectUnknownFields` 也禁止私自加顶层字段）。
4. 某站在 h=10–14 没有模型站点编码时，该段降级到同版本基线，`reasons` 含 `MODEL_STATION_H10_UNAVAILABLE`，HTTP 仍 200，不再误报 400。
5. `/health` 同时检查 Hive 报表目录与特征 `datasetVersion`/`runId` 一致，防止预测和排行来自两批数据。
6. `sourceMode=LIVE_ARTIFACT` 才是正式发布；测试夹具是 `FIXTURE`，答辩不能当真实预测讲。
7. 不写 SQLite、不启模拟器、不调 YARN。业务库权限仅快照文件只读。

---

## 报告与复算

**代码 / 文档：** `deliverables/stage2/member3-cf543ae/2026-09-17-member3-final-report.md`、`deliverables/stage2/member3-cf543ae/2026-09-17-recomputation.md`；上机记录 `deliverables/stage2/member3-c78aec5/2026-09-16-vm-acceptance.md`

### 用了什么技术

Markdown 固定结构：结论表、任务矩阵、缺陷列表、逐步 shell 命令。仓库外证据包按 SHA-256 登记，模型与 `runtime/` 不进 Git。Beeline / `spark-submit` / `pytest` 命令写死参数，便于非作者复跑。

### 实现了什么功能

四类结论分开：本地测试、YARN 作业、真实数据、业务口径。复算说明从 hourly 到六个接口逐步给出预期行数和 MAE。非作者复跑仍为 NOT_RUN。

### 具体怎么实现

1. 最终报告把 S2-09/S2-10 标 PARTIAL，不把接口 200 写成口径验收通过。
2. 复算步骤 1–7 对应：环境变量、Hive 建表、报表导出、Spark 特征、训练、Flask、对照数字出处。
3. 上机输入 MD5 `2BF780A4…`，发布为 `course-local-v1` / `course-local-20260915`，与成员1 `course-v1`（`d0d5d4f4…`）区分。对齐只需换 `--dataset-version` / `--run-id` / `--input` 重跑，不改切分和模型代码。
