# 成员3数据质量对账（D8 正式批次 + 六类夹具）

契约仍是 `charge_event_v2`。单位以 kWh / 秒 / 元 为准（第一阶段已冻结，不再退回 Wh/分）。本文件不训练模型。

## 一、上游公开数据转换审计

来源：Leeds City Council / Data Mill North 的 `2021 EVCP use Q1.csv`，Open Government Licence v3.0。

| 项 | 值 |
| --- | --- |
| 上游文件 SHA-256 | `7046f4084e4f9383a7f575b166b28c5d87fd1e9373bc5631ed53e43dc9ae966a` |
| 上游记录 / 有效 / 异常 | 1011 / 999 / 12 |
| 异常明细 | 6 条缺少结束时间和电量；6 条电量为负 |
| 隐私处理 | 丢弃 Unique User ID、Charging event、CP ID、Connector、Model |
| 衍生规则 | 按 Europe/London 会话开始小时聚合；无会话小时补零；费用固定 0 且不用于营收 |
| 衍生文件 | 6450 行（3 站 × 2150 连续小时） |

## 二、正式获准批次 `public-leeds-2021q1-001`

命令：`python -m src.curate_batch`（连续两遍，质量 JSON 相同）。

| 项 | 值 |
| --- | --- |
| data_mode | `PUBLIC_AGGREGATED` |
| 清单阈值 `min_valid_rate` | 1.0 |
| SHA-256 | `912e083158ef5dc7cd035a79b3f233d2572d3df818eaef0856ab924bd63e6e83` |
| 输入 / 通过 / 拒绝 | 6450 / 6450 / 0 |
| 守恒 | 6450 = 6450 + 0 |
| 重复 / 缺失 / 未映射 / 单位 / 隐私 | 0 / 0 / 0 / 0 / 0 |
| curated 行 | 6450（3 站 × 2150 小时） |
| 昨日基线可用 / 不可用 | 6378 / 72（第一天 24 小时 × 3 站无昨天，不填 0） |
| raw 是否改写 | 否（清洗前后哈希与字节数相同） |

一小时回算：站 `1`、`2021-03-11T08:00:00+00:00`，原始会话计数 3，`load_kwh=168.26`，与 accepted 的电量和会话计数一致。

D9 时间切分评测：1/6/24 小时模型 MAE 分别为 2.0860 / 2.1005 / 2.1203，昨日基线 MAE 分别为 2.1365 / 2.1415 / 2.1480，三个时域均发布 `trained`。这只表示本次固定数据和固定切分下优于基线，不表示已达到生产预测精度。

20 桩 60 秒协议压测依赖成员1服务端监听 `48080`，仓库内尚无 `tests/load/run_20_piles.py`。成员3协议回环仍由 `simulator/tests/tcp_loopback_test.py` 覆盖；分析管线本步不写业务 SQLite。

## 三、六类规则夹具（不得顶替正式 raw）

夹具 `analytics/tests/fixtures/six_class_events.csv` 共 6 条：

| 行 | source_record_id | 类别 | 预期 |
| --- | --- | --- | --- |
| 2 | PUB-OK-1 | 完全合法 | 通过。站 1，5.00 kWh，1800 秒，6.00 元，FINISHED |
| 3 | PUB-OK-2 | 完全合法 | 通过。站 2，4.50 kWh，2400 秒，5.40 元，FINISHED |
| 4 | PUB-OK-1 | 重复来源记录号 | 拒绝 `DUPLICATE` |
| 5 | PUB-MISS-1 | 必填 `event_time` 为空 | 拒绝 `MISSING_REQUIRED` |
| 6 | PUB-UNMAP-1 | 来源站 `PUB-UNKNOWN` 不在人工映射表 | 拒绝 `UNMAPPED_STATION` |
| 7 | PUB-UNIT-1 | 电量单位 `joule`，不猜测换算 | 拒绝 `INVALID_ENERGY_UNIT` |

守恒：输入 6 = 通过 2 + 拒绝 4。隐私样例 `privacy_events.csv` 在准入层拒绝，不进 raw。

复跑：`python -m src.fixture_quality`。
