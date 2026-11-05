# 成员3分析血缘（D7 raw → D8 curated）

```text
manifests/source.public-aggregated.json
        │  来源：Leeds City Council / Data Mill North，OGL v3.0
        ▼
上游 2021 EVCP use Q1.csv（不提交仓库）
        │  public_aggregated.py：1011 条审计；丢弃用户/会话/设备标识
        │  999 条有效，12 条缺失或负电量；按开始小时聚合并补零
        ▼
analytics/datasets/public_aggregated/hourly_charge_events.csv
        │  3 站 × 2150 小时 = 6450 行，SHA-256 912e0831...6e83
        │  ingest_raw：复制到 raw，写 SHA-256；已存在则只核对、不改字节
        ▼
runtime/analytics/raw/public-leeds-2021q1-001/
  hourly_charge_events.csv
  source_manifest.json
  batch_manifest.json
        │  curate_batch：只读 raw CSV
        │  时区 / kWh / 秒 / 元 / 人工站点映射
        ▼
runtime/analytics/staging/public-leeds-2021q1-001/
  accepted.json
  quality.json
  raw_snapshot.json
runtime/analytics/rejected/public-leeds-2021q1-001/rejected.json
        │  aggregate_hourly（禁止混 data_mode）
        ▼
runtime/analytics/curated/public-v1/
  hourly_station.csv
  metadata.json
  baseline.json          ← 无昨天则 baseline_available=false
  recalc_sample.json     ← 一小时电量回算
  evaluation.json        ← D9：三时域 MAE/RMSE 与 selected_mode
        ▼
python -m src.train      ← 时间切分 HistGradientBoostingRegressor；未打赢基线的时域不写 joblib
        ▼
GET /health /forecast /recommend
  分析链：PUBLIC_AGGREGATED 站点负荷 + 容量公式空闲桩
  运营大屏：FastAPI /dashboard 仍只读统一 SQLite（教学业务库 SIMULATED）
```

六类夹具走 `fixture_quality.py` 和独立批次 `fixture-six-class-001`，不得覆盖正式 Leeds 批次。

## 正式批次一小时回算

站 `1`、小时 `2021-03-11T08:00:00+00:00`：原始会话计数 3，`load_kwh=168.26`。可用 accepted 中该小时聚合行的 `energy_kwh` 与 `source_event_count` 同时回算。

覆盖：`2021-01-01T07:00:00+00:00` ～ `2021-03-31T21:00:00+01:00`。衍生 raw SHA-256：`912e083158ef5dc7cd035a79b3f233d2572d3df818eaef0856ab924bd63e6e83`。

## 两条链不要混

- 离线分析：本文件，`PUBLIC_AGGREGATED`；`/forecast` `/recommend` 读小时表。
- 运营大屏：FastAPI `/dashboard` 只读成员1 SQLite，教学业务库仍标 `SIMULATED`。回算表见 [`docs/test-results/dashboard-recalc.md`](test-results/dashboard-recalc.md)。
- 模拟教学链 `demo_pipeline` 只写 `runtime/analytics/curated/sim-v2/evaluation.json`。正式徽章只读 `analytics/models/evaluation.json`。管线基准走 Leeds `curate_public_batch`，见 [`docs/test-results/data-pipeline-performance.md`](test-results/data-pipeline-performance.md)。
- 成员3可以把分析链和大屏只读投影独立跑通；全组闭环仍要成员1写库与计量确认、成员2 Qt 跨端。
- 20 桩 600 秒负载需要成员1服务端监听 `48080`，仓库内尚无 `tests/load/run_20_piles.py`。
