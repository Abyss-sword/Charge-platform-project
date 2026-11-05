# analytics\manifests\source.example.json 逐行注释

文件职责：登记固定种子模拟数据集的来源、允许/禁止用途、覆盖时间、隐私边界和批次号，供准入模块加载。

原始文件：`analytics\manifests\source.example.json`

| 行号 | 原代码 | 详细注释 |
| ---: | --- | --- |
| 1 | <code>{</code> | 来源清单根对象开始。`source_registry.load_manifest` 会按必填键全集校验这份对象。 |
| 2 | <code>  "dataset_name": "member3-simulated-charge-events",</code> | 数据集名称。写入每条清洗记录和大屏来源信息，表明这是成员3教学模拟事件，不是运营生产库。 |
| 3 | <code>  "provider": "第29组成员3固定种子生成器",</code> | 提供者说明。答辩时可指向 `demo_pipeline.generate_demo_csv`：固定时间窗、固定三站、可复现，而不是随机网上爬数。 |
| 4 | <code>  "source_url": "local://analytics/src/demo_pipeline.py",</code> | 逻辑来源地址。`local://` 表示数据在仓库内生成，没有外网抓取；路径指向真正写 CSV 的脚本。 |
| 5 | <code>  "allowed_uses": ["teaching-analysis", "testing", "demonstration"],</code> | 允许用途。校验器要求列表中必须含 `teaching-analysis`，否则 `AdmissionError`。测试和演示是课程验收需要的额外许可。 |
| 6 | <code>  "prohibited_uses": ["claiming-real-operations", "personal-profiling"],</code> | 禁止用途：不得把模拟数据说成真实运营，不得做个人画像。和下面的 `SIMULATED` 互相印证。 |
| 7 | <code>  "acquired_at": "2026-09-05T09:00:00+08:00",</code> | 清单登记/获取时间（东八区）。演示管道常把该值当作统一的 `ingested_at`，保证每次重跑时间戳一致。 |
| 8 | <code>  "data_mode": "SIMULATED",</code> | 数据模式。必须属于 `SIMULATED` / `REAL_ANONYMIZED` / `PUBLIC_AGGREGATED`。若同时声称真实，校验会失败。 |
| 9 | <code>  "dataset_version": "sim-v2",</code> | 与 charge_event_v2 对齐的数据集版本。curated 输出目录为 `runtime/analytics/curated/sim-v2/`。 |
| 10 | <code>  "coverage_start": "2026-08-30T00:00:00+08:00",</code> | 数据覆盖起点。与 `generate_demo_csv` 的起始 `datetime(2026, 8, 30, tzinfo=+08:00)` 一致。 |
| 11 | <code>  "coverage_end": "2026-09-02T23:59:59+08:00",</code> | 覆盖终点。96 小时演示窗落在 8 月 30 日 0 点到 9 月 2 日结束；`coverage_start` 不得晚于该值。 |
| 12 | <code>  "redistribution_allowed": true,</code> | 允许在课程范围内再分发（提交仓库、给组员复现）。模拟且无 PII，才可以设为 true。 |
| 13 | <code>  "retention_policy": "课程验收结束后可删除；不含个人信息",</code> | 保留策略。告诉评委：这批数不是长期生产资产，验收后可清 `runtime/analytics`。 |
| 14 | <code>  "privacy_boundary": "禁止手机号、姓名、支付账号、车辆标识和连续轨迹标识",</code> | 隐私边界的人读声明。机器侧另有 `PRIVACY_FIELDS` 对照 CSV 表头，命中则整批拒收。 |
| 15 | <code>  "ingest_batch": "sim-20260905-001",</code> | 批次号，同时是 `runtime/analytics/raw/sim-20260905-001/` 目录名。重复导入时用 SHA-256 防止静默覆盖。 |
| 16 | <code>  "fields": ["source_record_id", "source_station_id", "event_time", "timezone", "energy", "energy_unit", "duration", "duration_unit", "amount", "amount_unit", "event_status"]</code> | 来源 CSV 的列清单，顺序与 `generate_demo_csv` 的 `fields` 以及 `normalize.REQUIRED_SOURCE_FIELDS` 一致。注意这些是「入站原始列」，还不是 v2 出口列（没有 `energy_kwh`，而是 `energy`+`energy_unit`）。 |
| 17 | <code>}</code> | 清单结束。除 `fields` 外，其余键全部属于 `REQUIRED_MANIFEST_FIELDS`；缺任一键准入失败。 |

---

【文件讲解·答辩用】

**功能作用。** 这是数据进入分析仓库前的「入境登记表」。没有合法清单，CSV 不能拷进 `raw/`。它回答四个问题：谁提供、允不允许教学分析、覆盖哪段时间、有没有隐私越界。成员3用这一份示例跑通固定种子演示，真实/公开数据阶段二换清单即可，不必改清洗代码。

**技术原理。** `load_manifest` → `validate_manifest`：检查必填键、`data_mode` 枚举、`teaching-analysis` 必须出现、时间窗起止顺序、模拟数据不得 `claims_real_data`。`ingest_source` 再读 CSV 表头与 `PRIVACY_FIELDS` 求交，固化文件到 `raw/<ingest_batch>/`，并写下 `source_manifest.json` 和带 SHA-256 的 `batch_manifest.json`。原始层只增不改。

**对应 JSON 的设计思路。** 键名与 `REQUIRED_MANIFEST_FIELDS` 对齐，避免「文档一套、代码一套」。`allowed_uses`/`prohibited_uses` 把伦理约束写成机器可判的列表。`fields` 描述的是**来源宽表**（带单位列），清洗后再变成契约窄表；这样同一管道能吃 Wh 或 kWh，只要单位列合法。批次号、版本号、模式会原样拷到每条 v2 事件上，形成血缘。

**与代码关系。** `demo_pipeline.run_pipeline` 调用 `ingest_source(ANALYTICS_DIR / "manifests" / "source.example.json", generated, runtime)`。`test_source_registry.py`、`test_ingest_raw.py` 用同结构的最小清单做正反例。大屏来源栏读的是管道写入元数据里的 `dataset_name`/`data_mode`/`ingest_batch`，源头就是这份文件。

**答辩讲法。** 「先登记再入库。」指着 `data_mode=SIMULATED` 和 `prohibited_uses` 说明不会把教学数据冒充真实运营；指着 `fields` 说明入站列带单位，出站才是 `energy_kwh`。若问能否改数字：可以改覆盖时间，但必须同步改生成脚本，否则清单和 CSV 对不上，质量报告会暴露。
