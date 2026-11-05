# analytics\contracts\charge_event_v1.schema.json 逐行注释

文件职责：保留旧版分析事件契约（电量 Wh、金额分），用于说明为何升级到 v2；当前管道不再把它当作活动输入。

原始文件：`analytics\contracts\charge_event_v1.schema.json`

| 行号 | 原代码 | 详细注释 |
| ---: | --- | --- |
| 1 | <code>{</code> | JSON Schema 根对象开始。整份文件描述「一条只读、已脱敏的充电事件」必须长什么样。 |
| 2 | <code>  "$schema": "https://json-schema.org/draft/2020-12/schema",</code> | 声明本文件遵循 JSON Schema 2020-12 方言，校验器按该草案解释 `required`、`additionalProperties`、`enum` 等关键字。 |
| 3 | <code>  "$id": "charge_event_v1.schema.json",</code> | 本契约的稳定标识。导出脚本和文档用这个名字指代「第一阶段整数单位版本」，避免和 v2 混淆。 |
| 4 | <code>  "title": "charge_event_v1",</code> | 人类可读标题，与 `$id` 对齐，方便在数据字典和验收输出里写成 `CONTRACT=charge_event_v1`。 |
| 5 | <code>  "description": "Read-only, de-identified charge event export contract. Energy, duration and amount use Wh, seconds and cents.",</code> | 契约语义：只读导出、不含个人信息；电量单位瓦时、时长秒、金额分。这三项整数单位正是后来被 v2 替换的原因。 |
| 6 | <code>  "type": "object",</code> | 一条事件必须是 JSON 对象，不能是数组或标量，以便字段名稳定、便于 CSV/SQLite 列对齐。 |
| 7 | <code>  "additionalProperties": false,</code> | 禁止出现未声明字段。手机号、姓名、支付账号、车牌等敏感列一旦混入会直接校验失败，从结构上落实脱敏。 |
| 8 | <code>  "required": [</code> | 必填字段列表开始。下列 14 项缺一不可，保证分析侧总能做站点映射、时区换算和批次追溯。 |
| 9 | <code>    "source_record_id",</code> | 来源系统内的稳定记录号，用于去重和血缘，而不是平台订单号。 |
| 10 | <code>    "source_station_id",</code> | 原始站点编号字符串。清洗时用映射表翻译成内部整数，禁止靠站名猜测。 |
| 11 | <code>    "station_id",</code> | 平台内部站点编号（正整数），大屏聚合、小时负荷都按它分组。 |
| 12 | <code>    "occurred_at",</code> | 事件发生时间。必须带时区，否则跨日聚合会错位。 |
| 13 | <code>    "ingested_at",</code> | 进入分析管道的导入时间，与发生时间分开，便于审计「何时看见这条数据」。 |
| 14 | <code>    "timezone",</code> | IANA 时区名（如 `Asia/Shanghai`），配合 `occurred_at` 做本地小时切分。 |
| 15 | <code>    "energy_wh",</code> | v1 电量字段：整数瓦时。后续 v2 改为 `energy_kwh` 两位小数字符串，本字段仅作历史对照。 |
| 16 | <code>    "duration_seconds",</code> | 充电时长，非负整数秒。v1/v2 共用这个单位，清洗时会把分钟换算成秒。 |
| 17 | <code>    "amount_cents",</code> | v1 金额字段：整数分。后续 v2 改为 `amount_yuan` 两位小数字符串，避免分/元混用。 |
| 18 | <code>    "status",</code> | 订单/事件状态，取值必须落在下面 `enum` 里，与业务库状态集合对齐。 |
| 19 | <code>    "data_mode",</code> | 数据模式：模拟 / 真实匿名 / 公开聚合。第一阶段固定 `SIMULATED`，界面不因换源改字段。 |
| 20 | <code>    "dataset_name",</code> | 数据集名称，写入质量报告和大屏来源栏，证明「这批数从哪套数据集来」。 |
| 21 | <code>    "dataset_version",</code> | 可复现版本号，curated 目录按它分文件夹，模型评测才能对照同一版数据。 |
| 22 | <code>    "ingest_batch"</code> | 一次导入批次号。raw/staging/rejected 三个目录都用它隔离，失败可整批回放。 |
| 23 | <code>  ],</code> | 必填数组结束。没有可选分析字段；阶段二若要加字段，只能加可选，不能改上述原意。 |
| 24 | <code>  "properties": {</code> | 各字段类型约束开始。`required` 只保证出现，`properties` 保证类型、范围和枚举合法。 |
| 25 | <code>    "source_record_id": { "type": "string", "minLength": 1 },</code> | 非空字符串。空串会被当成缺失记录号，无法做幂等去重。 |
| 26 | <code>    "source_station_id": { "type": "string", "minLength": 1 },</code> | 非空字符串。与 `station_mapping.json` 的键对应，例如 `SRC-STATION-A`。 |
| 27 | <code>    "station_id": { "type": "integer", "minimum": 1 },</code> | 正整数内部站号。0 或负数没有业务含义，映射表也要求 `v > 0`。 |
| 28 | <code>    "occurred_at": { "type": "string", "format": "date-time" },</code> | ISO 8601 日期时间字符串，校验器按 `date-time` 格式检查，必须含时区偏移。 |
| 29 | <code>    "ingested_at": { "type": "string", "format": "date-time" },</code> | 同样是带时区的 ISO 时间。与 `occurred_at` 分开存储，不允许用导入时间冒充发生时间。 |
| 30 | <code>    "timezone": { "type": "string", "minLength": 1 },</code> | 非空时区名。清洗代码用 `zoneinfo.ZoneInfo` 解析，未知时区会报 `UNKNOWN_TIMEZONE`。 |
| 31 | <code>    "energy_wh": { "type": "integer", "minimum": 0 },</code> | 非负整数 Wh。整数可避免当时跨语言浮点误差，但展示要换算 kWh，且与业务库后来的两位小数字符串不一致。 |
| 32 | <code>    "duration_seconds": { "type": "integer", "minimum": 0 },</code> | 非负整数秒。允许 0（预约未开始等），禁止负数时长。 |
| 33 | <code>    "amount_cents": { "type": "integer", "minimum": 0 },</code> | 非负整数分。与电量一样，整数精度够用，但和业务端「元、两位小数」口径冲突，故 v2 废弃。 |
| 34 | <code>    "status": {</code> | 状态字段约束对象开始。先声明类型，再收紧为枚举，防止自由文本进入聚合。 |
| 35 | <code>      "type": "string",</code> | 状态是字符串，不是数字编码，便于日志和 CSV 直接阅读。 |
| 36 | <code>      "enum": ["RESERVED", "CHARGING", "PENDING_PAYMENT", "FINISHED", "CANCELED", "EXPIRED"]</code> | 与业务订单状态对齐的六值枚举。来源若写 `COMPLETED`/`STOPPED`，清洗层会映射成 `FINISHED`，但契约出口只允许这六种。 |
| 37 | <code>    },</code> | 状态约束对象结束。 |
| 38 | <code>    "data_mode": {</code> | 数据模式约束对象开始。三种模式对应三种合法来源，不允许第四种「看起来像真的」的标签。 |
| 39 | <code>      "type": "string",</code> | 模式同样用字符串枚举，大屏来源栏直接展示，无需再映射。 |
| 40 | <code>      "enum": ["SIMULATED", "REAL_ANONYMIZED", "PUBLIC_AGGREGATED"]</code> | 模拟 / 真实已匿名 / 公开聚合。与 `source_registry.ALLOWED_MODES` 完全一致；第一阶段样例必须是 `SIMULATED`。 |
| 41 | <code>    },</code> | 数据模式约束对象结束。 |
| 42 | <code>    "dataset_name": { "type": "string", "minLength": 1 },</code> | 非空数据集名。来源清单里的同名字段会在导入时写入每条事件，保证出站记录可追溯。 |
| 43 | <code>    "dataset_version": { "type": "string", "minLength": 1 },</code> | 非空版本。例如教学模拟用 `sim-v2`，换一批种子或时间窗必须改版本，不能原地覆盖。 |
| 44 | <code>    "ingest_batch": { "type": "string", "minLength": 1 }</code> | 非空批次号。也是 raw 目录名，重复导入同一批次时用 SHA-256 比对，内容不一致则拒绝覆盖。 |
| 45 | <code>  }</code> | `properties` 对象结束。未列出的键已被 `additionalProperties: false` 禁止。 |
| 46 | <code>}</code> | Schema 根对象结束。整份文件 46 行，无注释、无尾随多余字段，可被标准 JSON 解析器加载。 |

---

【文件讲解·答辩用】

**功能作用。** `charge_event_v1` 是第一阶段早期冻结的「只读分析出口」形状：一条脱敏充电事件要带齐来源号、两个站点号、两个时间、时区、Wh、秒、分、状态、模式、数据集和批次。它证明分析侧一开始就和业务库解耦——只消费导出，不反写订单。现在它只作历史参考，活动契约是 v2。

**技术原理。** 用 JSON Schema 而不是口头约定：`required` 卡住缺列，`additionalProperties: false` 卡住隐私列，`enum` 卡住状态和数据模式，整数 `minimum` 卡住负电量/负金额。校验可以在导出脚本、集成测试或文档对照里做，不必把规则写死在某一门语言里。

**对应 JSON 的设计思路。** 单位选 Wh/分/秒，是为了当时全整数、无浮点。14 个必填项覆盖血缘（来源号、批次、版本）和度量（电量、时长、金额）。`data_mode` 预留三种值，是为了阶段二换源时界面字段不变。后来业务端统一「元 / kWh / 两位小数字符串」，整数分和 Wh 无法再对齐，所以另建 v2，而不是改坏已验收的 v1 文件。

**与代码关系。** 成员3当前 Python 管道（`normalize.py`、`ingest_raw.py`、`demo_pipeline.py`）输出并打印 `CONTRACT=charge_event_v2`，不再按 `energy_wh`/`amount_cents` 清洗。本文件仍留在 `analytics/contracts/`，供对照 `docs/analytics-data-contract.md` 和阶段一文档。站点映射、来源清单、状态枚举在 v2 里沿用，只有电量和金额的表示变了。

**答辩讲法。** 「v1 是历史契约，单位是 Wh 和分；现在跑的是 v2，单位是 kWh 和元，而且是两位小数字符串。」指着第 15、17 行和第 31、33 行说明整数单位，再指 `additionalProperties` 说明为什么没有手机号字段。若问为何不删 v1：删了就无法展示版本迁移，评委也无法核对阶段一冻结点。
