# 接口变更单

- 编号：ICR-20260917-01
- 提出人 / 日期：成员3 / 2026-09-17
- 需求编号：S2-09（未来高峰时段及负荷预警）
- 动作名：分析服务 `GET /analysis/peak`，以及模型产物 `stage2-evaluation.json` 新增高峰口径字段

## 旧字段与示例

```json
{
  "status": "OK",
  "datasetVersion": "course-local-v1",
  "runId": "course-local-20260915",
  "analysisStationId": "course:369001",
  "thresholdRule": "forecast mean * 1.20; if none, report daily maximum",
  "averageEnergyKwh": "6.24",
  "thresholdEnergyKwh": "7.49",
  "peaks": [{ "targetStart": "2015-10-01T20:00:00Z", "energyKwh": "7.60", "level": "HIGH" }]
}
```

旧口径按"预测均值 ×1.20"筛点，筛不到就退化成 `level=DAILY_MAX` 的单点。

## 新字段与示例

```json
{
  "status": "OK",
  "datasetVersion": "course-local-v1",
  "runId": "course-local-20260915",
  "analysisStationId": "course:369001",
  "originTime": "2015-10-01T00:00:00Z",
  "horizonHours": 24,
  "modelMode": "TRAINED",
  "topN": 3,
  "peakRule": "top 3 forecast hours ordered by load descending then target time ascending; ...",
  "threshold": {
    "source": "TRAINING_SPLIT_DISTINCT_TARGET_HOURS",
    "quantile": 0.9,
    "thresholdEnergyKwh": "7.00",
    "sampleCount": 240,
    "reason": null
  },
  "peaks": [
    { "rank": 1, "targetStart": "2015-10-01T23:00:00Z", "targetEnd": "2015-10-02T00:00:00Z",
      "horizonHours": 24, "energyKwh": "7.40", "meanPowerKw": "7.40" }
  ],
  "highLoadWindows": [
    { "start": "2015-10-01T20:00:00Z", "end": "2015-10-02T00:00:00Z", "hours": 4, "peakEnergyKwh": "7.40" }
  ],
  "warning": {
    "level": "RELATIVE_HIGH_LOAD",
    "basis": "TRAINING_SET_P90",
    "capacityThreshold": { "available": false, "reason": "NO_CAPACITY_DATA" },
    "advice": ["STAFF_ON_SITE", "RESCHEDULE_NON_URGENT_CHARGING"],
    "automaticControl": false,
    "reasons": ["ABOVE_TRAINING_P90", "NO_CAPACITY_DATA"]
  },
  "evaluation": { "status": "OK", "topN": 3, "originsEvaluated": 40, "peakHits": 96, "precisionAtTopN": 0.8 },
  "reasons": ["NO_CAPACITY_DATA", "SESSION_ALLOCATED_NOT_METERED"]
}
```

模型产物 `stage2-evaluation.json` 同步新增 `peakRule`、`peakThresholds`、`peakEvaluation` 三个顶层键。

`warning.level` 取值：`RELATIVE_HIGH_LOAD`（有超线时段）、`NONE`（有阈值但无超线时段）、`UNKNOWN`（阈值未发布）。

## 修改原因

`00-总规格与决策.md` §6 对高峰与预警的要求是硬性的，旧实现三处不符：

1. 规格要求"在未来24小时预测曲线中取 Top3 小时，按负荷降序、时间升序稳定排序"。旧口径是阈值筛选，返回数量不定（可能 0 个、也可能 8 个），且没有并列时的确定顺序，界面无法稳定呈现"未来高峰 Top3"。
2. 规格要求"另给超过训练集历史 P90 阈值的时间段"。旧口径的 ×1.20 是拿同一条预测曲线的均值当参照，属于自比：整条曲线一起抬高时反而筛不出任何高峰，与"相对历史高负荷"的判据无关。
3. 规格要求"高峰输出 Top3 命中数或 Precision@3"。旧产物没有任何高峰评估数字。

预警部分按规格"优先以已登记安全容量对应功率阈值判断；容量不全时只能标注相对历史高负荷，不声称电网超载。只建议值守/调度，不自动控制真实设备"实现：本数据集无容量列，因此 `capacityThreshold.available` 恒为 false，`basis` 只能是 `TRAINING_SET_P90`，并固定携带 `automaticControl: false`。

## 是否影响单位、状态或必填性

单位不变，仍是两位十进制字符串。`peaks[]` 由不定长改为最多 `topN` 条并新增 `rank`；`thresholdRule`、`averageEnergyKwh`、`thresholdEnergyKwh` 三个旧顶层字段被 `peakRule` 与 `threshold` 对象取代。`forecast_v1` 未受影响，其 schema 与字段集合一个字都没动。

## 影响端

分析服务（成员3）、Qt 运营端的未来高峰与预警面板（成员2）。车主端、Web 大屏、模拟器不受影响。

## 兼容办法

成员2目前把 Top3 与预测预警都标为 BLOCKED、尚未消费旧字段，所以本次没有需要保留的旧读法。新结构刻意把"排名"与"是否高负荷"分开：`peaks` 永远给 Top3 供展示，`highLoadWindows` 与 `warning` 才是判据，阈值缺失时 `warning.level=UNKNOWN` 而 `peaks` 仍可用，界面不必因为缺阈值就整块空白。

## 待办分工

| 端 | 待办 | 状态 |
|---|---|---|
| 成员3 | Top3 稳定排序、训练集 P90 阈值、高负荷时段合并、预警字段、Precision@3 | 已完成，本地 122 项测试通过 |
| 成员3 | 在虚拟机重跑 `train.py --stage2` 以生成带 `peakThresholds` 的产物，并复验 `/analysis/peak` | 待上机（旧产物无该键，接口会返回 `warning.level=UNKNOWN`） |
| 成员2 | 运营端消费 `peaks[].rank`、`highLoadWindows`、`warning.level/reasons/advice` | 待处理 |
| 成员1 | 复核高峰口径与规格一致性，纳入 S2 矩阵 | 待处理 |

## 自动测试

- `analytics/tests/test_stage2_model.py`：Top3 按负荷降序、时间升序（含并列）、跳过无值小时、Decimal 比较不退化为浮点；P90 只取训练段且按 `target_end` 去重、样本不足时withhold、历史过短时返回 `UNAVAILABLE` 而不抛异常；Precision@3 命中计算与短窗口跳过。
- `analytics/tests/test_stage2_train_bundle.py`（新增）：跑完整 `train_stage2_models`，断言产物含 `peakThresholds`、`peakEvaluation`，24 个时域全部发布且 JSON 可序列化。
- `analytics/tests/test_flask_api.py`：Top3 排名、P90 高负荷时段合并与边界、无阈值时 `warning.level=UNKNOWN`、缺失小时打断时段、非法 horizon 返回 400。

## 人工验证

上机重训后请求 `/analysis/peak?horizon=24&stationId=<真实站点>`，核对：`peaks` 恰为 3 条且 `rank` 连续；每条 `targetStart` 都能在同一次 `/forecast?horizon=24` 的点位中找到且数值一致；`threshold.thresholdEnergyKwh` 与产物内该站点阈值一致；`highLoadWindows` 的每个小时电量都严格大于阈值。

## 数据迁移

无。但旧模型产物没有 `peakThresholds`，接口会以 `warning.level=UNKNOWN` + `PEAK_THRESHOLD_NOT_PUBLISHED` 明确降级，不会伪造阈值，也不会报错。

## 回退办法

回退需还原 `flask_api.py` 的 `/analysis/peak` 路由、`forecast_service.py` 的 `peak_analysis`、`model.py` 的三个高峰函数与 `train.py` 的产物字段。回退即重新违反总规格 §6 的 Top3 与 P90 要求，不建议。

## 三方复核

- 成员1批准：待签
- 成员2页面复核：待签
- 成员3数据复核：已复核（提出人）
