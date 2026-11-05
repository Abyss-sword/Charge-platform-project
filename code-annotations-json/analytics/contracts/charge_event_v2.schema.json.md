# analytics\contracts\charge_event_v2.schema.json 逐行注释

文件职责：定义当前活动的分析事件契约：只读、已脱敏；电量和金额为 kWh/元的两位小数字符串。

原始文件：`analytics\contracts\charge_event_v2.schema.json`

| 行号 | 原代码 | 详细注释 |
| ---: | --- | --- |
| 1 | <code>{</code> | JSON Schema 根对象开始。描述清洗后每一条充电事件必须满足的形状，是成员3分析管道的出站合同。 |
| 2 | <code>  "$schema": "https://json-schema.org/draft/2020-12/schema",</code> | 使用 JSON Schema 2020-12。与 v1 同一方言，方便两份契约对照，也让标准校验器能直接加载。 |
| 3 | <code>  "$id": "charge_event_v2.schema.json",</code> | 当前活动契约的稳定标识。`demo_pipeline` 打印 `CONTRACT=charge_event_v2`，`ingest_raw` 写入的 `batch_manifest.contract` 也是这个名字。 |
| 4 | <code>  "title": "charge_event_v2",</code> | 标题与 `$id` 对齐，文档、大屏来源信息和验收日志都引用这一名称。 |
| 5 | <code>  "description": "Read-only, de-identified charge event export contract. Energy and amount are canonical two-decimal strings in kWh and yuan.",</code> | 相对 v1 的核心变更写在这里：电量、金额改为规范两位小数字符串，单位 kWh 和元，不再用整数 Wh/分。 |
| 6 | <code>  "type": "object",</code> | 单条事件是对象。`normalize_row` 返回的 dict 必须能一对一落到这些属性上。 |
| 7 | <code>  "additionalProperties": false,</code> | 未声明键一律非法。分析出口不能夹带 `phone`、`vin` 等列；这也迫使清洗层只输出契约字段。 |
| 8 | <code>  "required": [</code> | 14 个必填字段开始。与 v1 相比，把 `energy_wh`/`amount_cents` 换成 `energy_kwh`/`amount_yuan`，其余血缘字段保持不动。 |
| 9 | <code>    "source_record_id",</code> | 来源记录号。清洗时按它做批次内去重；阶段二换源也必须保留，才能证明同一条业务事实。 |
| 10 | <code>    "source_station_id",</code> | 原始站点字符串，例如 `SRC-STATION-A`。内部站号由映射表给出，不在本字段里写死。 |
| 11 | <code>    "station_id",</code> | 映射后的内部站点整数。小时聚合 `build_curated` 按它分组统计负荷。 |
| 12 | <code>    "occurred_at",</code> | 事件发生时间（带时区）。来源 CSV 的 `event_time` 经 `normalize._time` 规范化后写入此字段。 |
| 13 | <code>    "ingested_at",</code> | 导入时间，来自批次元数据，不是事件自己的时钟，避免把「何时发生」和「何时入库」混为一谈。 |
| 14 | <code>    "timezone",</code> | 声明时区。与 `occurred_at` 一起决定小时桶和星期特征。 |
| 15 | <code>    "energy_kwh",</code> | v2 电量字段：规范两位小数字符串，单位 kWh。这是相对 v1 `energy_wh` 的单位与类型双重升级。 |
| 16 | <code>    "duration_seconds",</code> | 时长仍用整数秒。来源若给分钟，`_duration_seconds` 会换算；契约出口不再暴露原始单位。 |
| 17 | <code>    "amount_yuan",</code> | v2 金额字段：规范两位小数字符串，单位元。禁止二进制浮点累加，与业务库 `amount_yuan` 口径一致。 |
| 18 | <code>    "status",</code> | 出口状态。来源 `COMPLETED`/`STOPPED` 等会先映射，最终只能是下面六值之一。 |
| 19 | <code>    "data_mode",</code> | 数据模式。第一阶段模拟管道固定写 `SIMULATED`，大屏用同一字段做来源徽章，换真实数据不必改 UI。 |
| 20 | <code>    "dataset_name",</code> | 数据集名，从来源清单拷贝到每条事件和 curated 元数据。 |
| 21 | <code>    "dataset_version",</code> | 数据集版本。`write_curated` 用它作为 `runtime/analytics/curated/<version>/` 目录名。 |
| 22 | <code>    "ingest_batch"</code> | 批次号。staging 与 rejected 目录、质量报告都挂在这个键上，失败行可按批回放。 |
| 23 | <code>  ],</code> | 必填列表结束。没有把桩号、用户号放进来：分析出口按站点聚合，刻意不含可识别个人或车辆的键。 |
| 24 | <code>  "properties": {</code> | 类型与格式约束开始。必填只解决「有没有」，这里解决「合不合法」。 |
| 25 | <code>    "source_record_id": { "type": "string", "minLength": 1 },</code> | 非空字符串。空记录号无法去重，`normalize_csv` 会因 `MISSING_REQUIRED` 拒绝。 |
| 26 | <code>    "source_station_id": { "type": "string", "minLength": 1 },</code> | 非空来源站号。未知值在 `map_station` 中变成 `UNMAPPED_STATION`，进入 rejected 而不是猜一个站。 |
| 27 | <code>    "station_id": { "type": "integer", "minimum": 1 },</code> | 正整数。与 `station_mapping.json` 的值类型一致，聚合时可作为 dict 键。 |
| 28 | <code>    "occurred_at": { "type": "string", "format": "date-time" },</code> | ISO 8601 `date-time`。清洗结果带偏移，例如 `2026-08-30T00:00:00+08:00`。 |
| 29 | <code>    "ingested_at": { "type": "string", "format": "date-time" },</code> | 导入时刻同样带时区。演示管道里常用清单上的 `acquired_at` 作为统一 ingested_at，保证可复现。 |
| 30 | <code>    "timezone": { "type": "string", "minLength": 1 },</code> | 非空 IANA 名。非法时区被拒绝，不会静默当成 UTC。 |
| 31 | <code>    "energy_kwh": { "type": "string", "pattern": "^[0-9]+\\.[0-9]{2}$" },</code> | 必须是 `数字.两位小数` 文本，如 `7.20`。正则禁止科学计数法和二进制浮点的 `7.199999`。清洗用 `Decimal` 量化后再 `format(..., ".2f")`。 |
| 32 | <code>    "duration_seconds": { "type": "integer", "minimum": 0 },</code> | 非负整数秒。与 v1 相同，便于按时长做过滤和平均充电时长统计。 |
| 33 | <code>    "amount_yuan": { "type": "string", "pattern": "^[0-9]+\\.[0-9]{2}$" },</code> | 金额同样锁定两位小数字符串，如 `12.50`。与电量同一模式，避免一边用字符串一边用 float 求和。 |
| 34 | <code>    "status": {</code> | 状态约束开始。出口枚举比来源枚举更窄，是「对内统一语言」。 |
| 35 | <code>      "type": "string",</code> | 状态为字符串，CSV、JSON、SQLite 文本列都可以原样存放。 |
| 36 | <code>      "enum": ["RESERVED", "CHARGING", "PENDING_PAYMENT", "FINISHED", "CANCELED", "EXPIRED"]</code> | 六种业务状态。`STATUS_MAP` 把 `COMPLETED`/`STOPPED`→`FINISHED`、`FAILED`→`CANCELED`，映射失败则整行拒绝。 |
| 37 | <code>    },</code> | 状态约束结束。 |
| 38 | <code>    "data_mode": {</code> | 数据模式约束开始。来源清单的 `data_mode` 必须能通过同一枚举，否则准入失败。 |
| 39 | <code>      "type": "string",</code> | 字符串枚举，前端无需再做数字码表。 |
| 40 | <code>      "enum": ["SIMULATED", "REAL_ANONYMIZED", "PUBLIC_AGGREGATED"]</code> | 与 `source_registry.ALLOWED_MODES` 一致。模拟数据若声称真实，清单校验会单独拒绝。 |
| 41 | <code>    },</code> | 数据模式约束结束。 |
| 42 | <code>    "dataset_name": { "type": "string", "minLength": 1 },</code> | 非空数据集名。大屏 API 的 `datasetName` 即来自此字段在元数据中的拷贝。 |
| 43 | <code>    "dataset_version": { "type": "string", "minLength": 1 },</code> | 非空版本。教学示例为 `sim-v2`，与来源清单、curated 元数据必须相同，否则实验不可复现。 |
| 44 | <code>    "ingest_batch": { "type": "string", "minLength": 1 }</code> | 非空批次，如 `sim-20260905-001`。既是目录名，也是每条事件上的血缘标签。 |
| 45 | <code>  }</code> | `properties` 结束。契约故意不含 `energy_wh`、`amount_cents`，防止旧整数单位回流。 |
| 46 | <code>}</code> | Schema 结束。46 行与 v1 行数相同，便于逐行 diff：真正改动集中在描述、必填名和电量/金额类型。 |

---

【文件讲解·答辩用】

**功能作用。** v2 是成员3分析链路和大屏共同遵守的「出站数据合同」。生成、准入、清洗、聚合、评测都向它看齐；业务 SQLite 和大屏 HTTP 接口展示的电量、金额也是同一套两位小数字符串，阶段二换匿名真实数据时不用改字段名和界面。

**技术原理。** 金额和电量不用 `number`，是因为 JSON 数字是 IEEE 浮点，`0.1+0.2` 在 Python/JS/C++ 里都会漂。改成 `string` + `^[0-9]+\.[0-9]{2}$`，跨语言只能当十进制文本读，再用 `Decimal` 计算、只在最终量化。`additionalProperties: false` 从模式上禁止 PII 列混进分析出口。

**对应 JSON 的设计思路。** 相对 v1 只替换度量表示，血缘字段（来源号、站号、时间、时区、模式、版本、批次）原样保留，这是「版本升级而不是另起炉灶」。pattern 写死两位小数，是为了和业务库、大屏 `energyKwh`/`amountYuan` 展示一致。状态和 `data_mode` 枚举与准入模块共享，避免清单允许的模式契约却不认。

**与代码关系。** `normalize_row` 产出的 14 个键与本 Schema 的 `required` 一一对应；`ingest_raw` 把 `contract: charge_event_v2` 写入 `batch_manifest.json`；`demo_pipeline.main` 打印 `CONTRACT=charge_event_v2`。大屏不直接读这份文件，但 FastAPI/`sqlite_dashboard.py` 读库时的 `energy_kwh`、`amount_yuan` 必须符合同一字符串规则。测试 `test_normalize.py` 断言映射后是 `"7.20"`、`"12.50"` 这种文本。

**答辩讲法。** 打开本文件和第 31、33 行：「活动契约是 v2；电量和金额是两位小数字符串，不是 float。」再对比 v1 的 `energy_wh`/`amount_cents`，说明为什么换。补一句：清洗用 `Decimal`，Schema 用正则，两边一起卡住精度。若问大屏关系：界面只展示契约字段，不展示手机号，因为 Schema 根本不允许那些键出现。
