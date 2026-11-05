# 第二阶段数据与API契约（T01）

> 维护人：成员1（`analytics/contracts` 目录由成员1统一变更）。
> 状态：契约与夹具已实现并按 schema 校验通过（`analytics/tests/test_stage2_fixtures.py`）。
> 效力：与《00-总规格与决策》§5/§7 一致；冲突时以总规格为准并在此登记修订。

## 1. 契约总览

| 契约 | Schema | 用途 | 字段风格 |
|---|---|---|---|
| `charge_event_v2` | `analytics/contracts/charge_event_v2.schema.json`（保留不变） | 第一阶段业务导出；金额/电量两位字符串 | snake_case |
| `historical_interval_v1` | `analytics/contracts/historical_interval_v1.schema.json`（新增） | 历史分析区间记录；允许金额/容量未知 | snake_case |
| `station_hour_v1` | `analytics/contracts/station_hour_v1.schema.json`（新增） | 按站点×UTC小时聚合的小时表发布行 | camelCase |
| `forecast_v1` | `analytics/contracts/forecast_v1.schema.json`（新增） | 分析API预测响应（Qt/Web/API三端共用） | camelCase |

来源适配层（`analytics/src/adapters/`）：

| 适配器 | 状态 | 说明 |
|---|---|---|
| `course_sessions.py` | **已启用** | 课程真实充电会话数据（2014-2015）→ 规范来源 CSV；年份 +2000 还原、Asia/Shanghai 转 UTC、`userId` 等隐私列在本地受控区裁剪、站点按 `distinct_days>=28 且 records>=50` 筛选、会话分摊标 `SESSION_ALLOCATED` |
| `acn.py` | **预留未启用** | Caltech ACN（含站级 EVSE 数，可补容量维度）；契约已预留 `acn` 命名空间，未下载数据、不产生证据 |

隐私黑名单（`source_registry.PRIVACY_FIELDS`）已含个人标识列：`user_id`/`userid`/`account`/`account_id`/`plate` 等，命中表头或清单 `fields` 即整批阻断（T03 接入课程数据时实测补充）。

保护性约束：

1. `charge_event_v2` 消息封装 `version="1.0"` 不因第二阶段改变；金额、电量仍为两位十进制字符串，禁止数值类型与科学计数法。
2. 历史数据不强行套用订单契约：`historical_interval_v1` 允许 `amount_yuan`、容量、状态为 `null`，但**每个 null 必须在 `null_reasons` 中说明原因**（`NOT_COLLECTED` 未采集 / `NOT_APPLICABLE` 不适用 / `ANOMALY` 异常），由 schema `allOf/if-then` 强制。
3. `energy_kwh`（区间记录）保留来源精度（最多6位小数），**不逐片段提前舍入**；两位 HALF_UP 只发生在 `station_hour_v1` 发布层。

## 2. historical_interval_v1（历史分析区间）

必填字段：`source_id, dataset_version, record_id, analysis_station_id, interval_start_utc, interval_end_utc, energy_kwh, observation_kind, data_mode, source_timezone`。

| 字段 | 类型 | 说明 |
|---|---|---|
| `source_id` | string | 来源命名空间，如 `acn`（Caltech ACN）、`sim`（教学模拟） |
| `dataset_version` | string | 数据版本；不同版本是替代快照，不得相加 |
| `record_id` | string | 来源内稳定主键；**去重键 = (source_id, record_id)**，跨批次不重复累加 |
| `analysis_station_id` | string | 带命名空间站点，如 `acn:caltech-1`；**不得凭编号映射到本地业务站点** |
| `interval_start_utc` / `interval_end_utc` | string | UTC，`YYYY-MM-DDTHH:MM:SSZ`，结束晚于开始 |
| `energy_kwh` | string | 十进制文本，非负，禁科学计数法 |
| `observation_kind` | enum | `METER_INTERVAL` 实测 / `SESSION_ALLOCATED` 会话分摊估计 / `PUBLIC_HOURLY` 公开小时聚合 |
| `data_mode` | enum | `SIMULATED` / `REAL_ANONYMIZED` / `PUBLIC_AGGREGATED`（沿用第一阶段） |
| `source_timezone` | string | IANA 时区，仅用于展示换算，存储一律 UTC |

可空字段：`pile_id, amount_yuan, available_piles, capacity_piles, occupied_piles, temperature_c, weather_source`。
占用推导约束：只有会话起止且具备完整桩容量、停用信息时才可推导占用；连接≠持续充电；不满足时 `occupied_piles` 必须为 null。

## 3. station_hour_v1（小时聚合发布行）

| JSON 字段 | MR staging TSV 列（T04输入） | 说明 |
|---|---|---|
| `analysisStationId` | col1 站点ID | 带命名空间，TSV 内禁止制表/换行 |
| `hourStartUtc` | col2 UTC小时 | 整点 `YYYY-MM-DDTHH:00:00Z` |
| `energyKwh`（两位） | col3 未舍入电量文本 | TSV 保留未舍入中间值；两位在发布层 HALF_UP |
| `sessionStartCount` | col4 会话开始计数 | 跨小时会话只在起始小时计一次 |
| `coverageSeconds` | col5 覆盖秒数 | 每站区间并集，非多桩覆盖之和；0–3600 |
| `observationKind` | col6 观测类型 | 增加 `MISSING`：无观测小时，`energyKwh=null`、覆盖 0 秒，**不自动填 0** |
| `capacityPiles` 等 | 独立小时表关联 | 容量/状态作为维度关联，缺失为 null |

## 4. forecast_v1（分析API预测响应）

必填：`contractVersion, originTime, targetStart, targetEnd, horizonHours, analysisStationId, businessStationId, dataMode, datasetVersion, modelMode, status, degraded, reasons, points, sourceMode, generatedAt`。

- `originTime`：预测原点 T（UTC）。全部特征只能使用 T 之前已知数据；工程上 T = 最后一个已观测小时的**结束**时刻，故该小时自身的电量、会话数、覆盖率都是 T 之前已知量，可合法入特征。
- **时间窗口对齐**（ICR-20260915-01 澄清，待成员1/成员2复核）：时域 h 的点覆盖 `[T+h-1, T+h)`，即 h=1 紧贴原点。因此 `points[0].targetStart == originTime`、顶层 `targetStart == originTime`、顶层 `targetEnd == originTime + horizonHours 小时`（horizon=24 即 T+24h）。三端一律按 `targetStart`/`targetEnd` 渲染时间轴，**不得**用 `horizonHours` 自行推算钟点。
  - 本条为消解原文矛盾：旧表述"逐小时请求为 T+1h 对齐整点"与"`targetEnd` 如 T+24h"不可能同时成立（24 点从 T+1h 起算必然结束于 T+25h）。按后者定稿，因为前者会让 `[T, T+1)` 无任何时域覆盖，且 h=24 的昨日同小时基线落在原点之后的 `[T, T+1)`，无法构造。详见 `docs/ICR-20260915-01-forecast-window-alignment.md`。
  - 落地状态：分析服务已按新口径实现（成员3）；`tests/fixtures/stage2/forecast/` 六份共享夹具与 `tests/ui/forecast_contract_test.cpp` 断言仍为旧口径，待成员1、成员2按变更单更新。
- `points[]`：`targetStart/targetEnd/horizonHours/energyKwh/meanPowerKw/freePilesExpected/nullReason`。能量与功率为两位字符串；`freePilesExpected` 为整数或 null（容量未知→null + `NO_CAPACITY_DATA`）。字段集合已被 Qt 解析器 `rejectUnknownFields` 冻结，新增任何顶层字段都会让 Qt 端解析失败，必须先走接口变更单。
- `sourceMode`：`LIVE_ARTIFACT`（已发布产物）/ `FIXTURE`（测试夹具；界面联调用，禁止冒充正式预测）。
- 状态语义：

| status | 错误分类 | 典型原因 |
|---|---|---|
| `OK` | — | 正常返回（可能 `modelMode=BASELINE`） |
| `PARAM_INVALID` | 参数错误 | horizon 不支持、站点格式非法 |
| `NO_DATA` | 数据不足 | 小时表无该站点数据 |
| `EXPIRED` | 数据不足（显式过期） | 产物超过新鲜度上限 |
| `MODEL_UNAVAILABLE` | 依赖不可用 | 模型文件不可读且无基线 |
| `DEPENDENCY_UNAVAILABLE` | 依赖不可用 | 批处理/存储不可用 |

**错误三分类**（T01 约定，界面不得统称"网络失败"）：参数错误→提示用户修正输入；数据不足→显示"暂无数据/已过期"并保留历史；依赖不可用→显示降级原因并保留基础业务入口。

### 4a. `/analysis/peak`（未来高峰与负荷预警，ICR-20260917-01）

口径直接来自《00-总规格与决策》§6，实现与之逐条对齐：

- `peaks[]`：未来曲线的 Top3 小时，**按负荷降序、目标时间升序稳定排序**，带 `rank`（1..3 连续）。无值的小时不参与排名——未知不等于低。少于 3 个有值小时时按实际数量返回。
- `threshold`：`source=TRAINING_SPLIT_DISTINCT_TARGET_HOURS`、`quantile=0.9`。阈值只用**训练段**小时算，且先按 `target_end` 去重（同一钟点是多个原点的标签，不去重会让可达原点多的小时反复计数）。样本不足或该站不在训练段时 `thresholdEnergyKwh=null` 并给 `reason`。
- `highLoadWindows[]`：预测电量**严格大于**阈值的连续小时合并成段（规格用词是"超过"，等于阈值不算）。中间出现无值小时会打断时段，不跨越拼接。
- `warning`：`level` 为 `RELATIVE_HIGH_LOAD` / `NONE` / `UNKNOWN`；`basis` 只能是 `TRAINING_SET_P90`（本数据集无容量列，`capacityThreshold.available` 恒 false）。**不得声称电网超载**，`automaticControl` 恒为 false，`advice` 只给值守/调度类建议码。
- `evaluation`：引用模型产物的 `peakEvaluation`（`precisionAtTopN`、`peakHits`、`originsEvaluated`），即规格要求的"Top3 命中数或 Precision@3"。

模型产物 `stage2-evaluation.json` 相应新增 `peakRule`、`peakThresholds`、`peakEvaluation`。旧产物没有这些键时接口以 `warning.level=UNKNOWN` + `PEAK_THRESHOLD_NOT_PUBLISHED` 明确降级，不伪造阈值。

### 4b. `/recommend`（车主推荐，`member3_recommendation_v1`，ICR-20260917-02）

口径来自《00-总规格与决策》§7。请求参数：`latitude`、`longitude`、`limit`（1–20，默认 3）、`arrivalTime`（可选 ISO8601）。

- **候选排除**：只有 `status=OPEN`、容量大于 0、且并非全部桩故障的站点才是候选。
- **评分项与初始权重**：距离 0.30、价格 0.20、当前可用比例 0.25、预测可用比例 0.25。各项 min-max 归一到 [0,1] 且**统一"1 为最优"**（距离、价格是成本项，归一时反向）。某项在候选集内取值全相同时该项取 1——不区分候选的指标不应惩罚任何一方。
- **缺失项处理**：逐候选剔除其缺失的项，并把剩余权重重新归一（`appliedWeights` 逐条回显，可核对）。本数据集无容量标签，因此"预测可用比例"恒缺失，`criteriaExcluded` 含 `predictedAvailability`，其余三项归一为 0.4000 / 0.2667 / 0.3333。**所有项都缺的候选被剔除**而不是按空指标排名，`status=NO_DATA`。
- **同分次序**：`score` 降序 → 距离升序（未知排末）→ 当前空闲桩降序 → 站点 ID 升序。
- **没有等待分钟**：规格写明"无需假设有排队系统，不显示虚构等待分钟"，因此响应中不存在任何等待字段。此前按"充电中桩数 ×15"推算的 `wait_minutes` 已删除。
- **`arrivalTime`**：接口接受该参数并回显，但预测可用比例需要容量标签，因此 `arrivalTimeUsed` 恒为 `false`、`arrivalTimeReason=NO_CAPACITY_DATA`。**不得**把回显当成"到达时刻已参与计算"。
- `reasons` 为码而非文案，界面自行本地化：`CLOSEST`、`CHEAPEST`、`MOST_AVAILABLE_NOW`、`MOST_AVAILABLE_ON_ARRIVAL`、`BALANCED`。

## 5. 十进制与舍入规则（全链路）

1. Python `Decimal`、Java `BigDecimal`；Hive/Spark 用 `DECIMAL(38,12)` 工作列，溢出拒绝；最终两位 `HALF_UP`。
2. 会话跨小时分摊：按重叠秒数比例分摊未舍入值，**尾差（分级到 0.01）分配给最大未舍入片段，并列取最早小时**；分摊总量 == 会话总量两位值。
   例：10.27 kWh 跨两个等长小时 → `5.14 + 5.13 = 10.27`（每个未舍入 5.135，HALF_UP 各得 5.14，尾差 −0.01 给最早小时）。
3. 账务规则不受数据管线影响：`10.275 元 → 10.28`（业务侧既有 HALF_UP 规则保留）。

## 6. 夹具清单（同一份夹具供 Qt/Web/API 测试）

```
tests/fixtures/stage2/
├── forecast/                      # forecast_v1 六种响应状态（S2-T15 等界面/接口测试输入）
│   ├── normal.json                # OK + TRAINED，24点完整
│   ├── baseline.json              # OK + BASELINE（验证集选中基线）
│   ├── no_data.json               # NO_DATA（数据不足）
│   ├── expired.json               # EXPIRED（产物过期，degraded=true）
│   ├── model_failure.json         # MODEL_UNAVAILABLE（模型不可读）
│   └── capacity_missing.json      # OK 但 freePilesExpected=null + NO_CAPACITY_DATA
├── station_hour/sample_rows.json  # 小时表发布行样例（含 MISSING 小时）
└── governance/                    # 治理输入夹具（T03/S2-T03~T06）
    ├── manifest_simulated_v1.json       # 合法来源清单（batch-fixture-001）
    ├── manifest_simulated_batch2.json   # 同版本第二批次（幂等测试）
    ├── manifest_privacy_blocked.json    # 声明隐私字段→准入必须拒绝（S2-T03）
    ├── intervals_valid.csv              # 3条合法
    ├── s2_t04_mixed.csv                 # 5条=3合法+1重复+1坏时间（S2-T04）
    ├── intervals_negative_energy.csv    # 负电量拒绝
    ├── intervals_decimal_split.csv      # 10.27跨两小时 + 10.275舍入（S2-T06）
    ├── intervals_cross_batch_dup.csv    # 跨批次重复（S2-T05）
    ├── intervals_privacy.csv            # CSV带隐私列（S2-T03）
    └── expected_intervals/sample_records.json  # 规范化记录样例（过 schema）
```

夹具经 `analytics/tests/test_stage2_fixtures.py` 校验：六个响应夹具必须完全符合 `forecast_v1`；删除 `datasetVersion`、把 `energyKwh` 改为数值都必须被拒绝。

> 待更新（ICR-20260915-01）：六份 `forecast/` 夹具的时间轴仍是旧口径（`originTime` 00:00 对应首点 01:00、顶层 `targetEnd` 为 T+25h），需按 §4 的窗口对齐整体前移一小时。schema 校验不覆盖时间字段之间的关系，因此夹具口径过时不会被现有测试发现，只会在三端联调时表现为曲线偏移一小时。
