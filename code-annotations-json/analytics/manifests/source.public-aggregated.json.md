# analytics\manifests\source.public-aggregated.json 逐行注释

文件职责：登记第二阶段公开聚合来源。模式是 `PUBLIC_AGGREGATED`，不是把第一阶段 `SIMULATED` 改名，也不是学校未授权的 `REAL_ANONYMIZED`。

原始文件：`analytics\manifests\source.public-aggregated.json`

| 行号 | 原代码 | 详细注释 |
| ---: | --- | --- |
| 2 | <code>"dataset_name": "member3-public-aggregated-hourly-load"</code> | 公开聚合小时负荷数据集名，写入 raw 批次清单和后续 curated 元数据。 |
| 8 | <code>"data_mode": "PUBLIC_AGGREGATED"</code> | 第二阶段合法模式。准入器拒绝把它和 `claims_real_data` 一起使用。 |
| 9 | <code>"dataset_version": "public-v1"</code> | curated 目录将使用该版本名，与 `sim-v2` 分开，禁止混训。 |
| 15 | <code>"ingest_batch": "public-20260910-001"</code> | raw 批次目录名。重复导入用 SHA-256 防止换文件。 |
| 16 | <code>"min_valid_rate": 1.0</code> | D8 质量门槛来自清单，不是代码写死的统一比例。本批要求全部通过。 |
| 16 | <code>"fields": [...]</code> | 与清洗必填来源列一致；不含手机号姓名车牌。 |

【文件讲解·答辩用】

功能作用：D7 获准来源清单。没有学校脱敏真源时，用来说清楚“这是公开聚合教学样本”。技术原理：同一套 `validate_manifest` / `ingest_raw`。答辩讲法：指着 PUBLIC_AGGREGATED，明确不是真实个人充电记录。
