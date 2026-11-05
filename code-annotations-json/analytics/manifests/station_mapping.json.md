# analytics\manifests\station_mapping.json 逐行注释

文件职责：把模拟来源站 `SRC-STATION-*` 和公开聚合来源站 `PUB-STATION-*` 手工映射成平台内部站点编号 1、2、3。未知来源站号在清洗时拒绝，不按站名猜测。

原始文件：`analytics\manifests\station_mapping.json`

| 行号 | 原代码 | 详细注释 |
| ---: | --- | --- |
| 1 | <code>{</code> | 映射表根对象开始。键是来源站点字符串，值必须是正整数内部站号；由 `load_station_mapping` 整表校验。 |
| 2 | <code>  "SRC-STATION-A": 1,</code> | 来源站 A → 内部站点 1。与 `generate_demo_csv` 的 `stations[0]` 以及 curated 里可用桩数配置 `"1": 4` 对应。 |
| 3 | <code>  "SRC-STATION-B": 2,</code> | 来源站 B → 内部站点 2。演示数据里 B 的电量和单价略高于 A，用于让三站曲线可区分，而不是三个克隆。 |
| 4 | <code>  "SRC-STATION-C": 3,</code> | 来源站 C → 内部站点 3。 |
| 5 | <code>  "PUB-STATION-A": 1,</code> | 公开聚合站 A → 同一内部站点 1。第二阶段 CSV 用另一套来源编号，避免和模拟种子混成同一来源键。 |
| 6 | <code>  "PUB-STATION-B": 2,</code> | 公开聚合站 B → 内部站点 2。 |
| 7 | <code>  "PUB-STATION-C": 3</code> | 公开聚合站 C → 内部站点 3。未出现在本表中的来源站号会在清洗时报 `UNMAPPED_STATION`。 |
| 8 | <code>}</code> | 映射表结束。模拟三站与公开聚合三站共用内部 1/2/3，但来源键分开，D8 不得把两种 `data_mode` 混进一次小时聚合。 |

---

【文件讲解·答辩用】

**功能作用。** 分析库内部只认整数 `station_id`，外部 CSV 只认 `SRC-STATION-*` 这类来源编号。本文件是两者之间唯一、需人工确认的对照表：不自动创建新站，不按中文站名猜测，避免把未知站点静默吞进聚合。

**技术原理。** `json.loads` 读入后，`load_station_mapping` 要求：整体是 dict，每个键是 str，每个值是 `int` 且 `> 0`。`map_station` 做一次字典查找，`KeyError` 转成 `ValueError("UNMAPPED_STATION")`，该行进入 rejected，不进 staging。这是显式失败，而不是默认站 0。

**对应 JSON 的设计思路。** 键用稳定来源 ID 而不是「XX 充电站」，因为名称会变、会重复。值从 1 起连续编号，方便作为聚合键和大屏站点下拉。表很小是刻意的：教学模拟只有三站；第二阶段若接入真实站，应改这份人工表，而不是在代码里写 if-else。

**与代码关系。** `demo_pipeline` 在清洗前执行 `load_station_mapping(.../station_mapping.json)`，把结果传给 `normalize_csv`。`normalize_row` 里 `station_id = map_station(row["source_station_id"], mapping)`。小时聚合和模型特征都使用映射后的整数。测试 `test_normalize.py` 用 `SRC-STATION-A → 1` 断言。

**答辩讲法。** 「来源站号和内部站号分开，中间只有一张人工表。」打开三行映射，说明未知站会进拒绝目录。若问为什么不写在 Python 常量里：JSON 清单可以不改代码就换映射，和来源准入文件放在同一 `manifests/` 下，职责都是「人确认过的配置」。
