# 接口变更单

- 编号：ICR-20260915-01
- 提出人 / 日期：成员3 / 2026-09-15
- 需求编号：S2-07、S2-09、S2-11（`forecast_v1` 三端共用）
- 动作名：分析服务 `GET /forecast`（`forecast_v1` 响应的时间窗口对齐）

## 旧字段与示例

字段名不变，变的是 `originTime`、`targetStart`、`targetEnd` 与 `horizonHours` 之间的时间关系。旧口径见共享夹具 `tests/fixtures/stage2/forecast/normal.json`：

```json
{
  "originTime": "2026-09-13T00:00:00Z",
  "targetStart": "2026-09-13T01:00:00Z",
  "targetEnd":   "2026-09-14T01:00:00Z",
  "horizonHours": 24,
  "points": [
    { "targetStart": "2026-09-13T01:00:00Z", "targetEnd": "2026-09-13T02:00:00Z", "horizonHours": 1 }
  ]
}
```

即时域 h 覆盖 `[T+h, T+h+1)`，顶层 `targetEnd` = T+25h。

## 新字段与示例

时域 h 覆盖 `[T+h-1, T+h)`，h=1 紧贴原点：

```json
{
  "originTime": "2026-09-13T00:00:00Z",
  "targetStart": "2026-09-13T00:00:00Z",
  "targetEnd":   "2026-09-14T00:00:00Z",
  "horizonHours": 24,
  "points": [
    { "targetStart": "2026-09-13T00:00:00Z", "targetEnd": "2026-09-13T01:00:00Z", "horizonHours": 1 }
  ]
}
```

约束表述：`points[0].targetStart == originTime`，顶层 `targetStart == originTime`，顶层 `targetEnd == originTime + horizonHours 小时`。工程上 `originTime` 取最后一个已观测小时的结束时刻。

## 修改原因

`docs/stage2/contracts.md` §4 原有两句互相矛盾：`targetStart` 写"逐小时请求为 T+1h 对齐整点"，`targetEnd` 写"如 T+24h"。两句不可能同时成立——24 个逐小时点从 T+1h 起算，结束必然是 T+25h。成员1的共享夹具按前一句制作，成员3的分析服务按后一句实现，成员2的 Qt 解析按夹具断言，于是同一个 `horizonHours` 在两端指向的小时相差一小时。

**《00-总规格与决策》§6 原文已经给出答案**："在整点 T 发布未来 24 个小时区间 `[T,T+1h)` 至 `[T+23h,T+24h)`"。首个区间就是 `[T, T+1h)`，即 h=1 紧贴原点。而本文件 §5 写明"与《00-总规格与决策》§5/§7 一致；冲突时以总规格为准并在此登记修订"，因此这不是两种口径二选一，而是 `contracts.md` 与共享夹具偏离了总规格，需要按总规格回正。

按后一句（`targetEnd` = T+24h）定稿，理由有三条：

1. **旧口径会留下预测空档**：`[T, T+1)` 这一小时既不属于历史（原点之后），也没有任何时域覆盖它。
2. **旧口径下 h=24 的基线无法构造**：目标 `[T+24, T+25)` 的昨日同小时是 `[T, T+1)`，位于原点之后、尚未发生，取不到值。新口径下 h=24 目标 `[T+23, T+24)`，昨日同小时为 `[T-1, T)`，已知可用。Spark 侧对应 `lead(h)` 与 `lag(24-h)`，`lead(h) + lag(24-h) = 24` 恒成立。
3. **与"特征只能使用 T 之前已知数据"自洽**：原点定义为最后一个已观测小时的结束时刻后，该小时的 `session_count`、`coverage_ratio` 与电量都是 T 之前已知量，可合法入特征；否则会构成同小时标签泄漏。

## 是否影响单位、状态或必填性

不影响。字段名、必填性、枚举、单位与两位字符串风格全部不变；`forecast_v1.schema.json` 无需修改（它不约束时间字段之间的关系）。

## 影响端

分析服务（成员3）、运营端 Qt、车主端 Qt（成员2）、Web 大屏（成员2）。模拟器不受影响。

## 兼容办法

无需协议协商：字段集合未变，旧解析器仍能解析新响应。三端只需统一按 `targetStart`/`targetEnd` 显示时间轴，不要用 `horizonHours` 自行推算钟点。Qt 侧 `forecast_parser.cpp` 使用 `rejectUnknownFields`，因此本变更刻意不新增任何顶层字段（训练侧的基线可比性说明放进既有 `reasons` 数组）。

## 待办分工

| 端 | 待办 | 状态 |
|---|---|---|
| 成员3 分析服务 | `forecast_service.py` 按新口径产出，`build_features.py` 标签改 `lead(h)`、基线改 `lag(24-h)` | 已完成（`714cbaf`），106 项本地测试通过 |
| 成员1 共享夹具 | `tests/fixtures/stage2/forecast/` 六份 JSON 的时间轴前移一小时（`normal`、`baseline`、`expired`、`model_failure`、`capacity_missing`、`no_data`） | 待处理 |
| 成员2 Qt/Web | `tests/ui/forecast_contract_test.cpp` 时间断言随夹具更新；分析页与 `AnalysisChart.vue` 确认直接使用 `targetStart` 而非按 `horizonHours` 推算 | 待处理 |

## 自动测试

- 成员3：`analytics/tests/test_flask_api.py` 已断言 `points[0].targetStart == originTime`、末点结束等于 T+horizon；`bigdata/spark/tests/test_features.py` 以 `lead(h) + lag(24-h) = 24` 守住训练侧对齐。
- 成员2：`forecast_contract_test.cpp` 更新后应保留一条"顶层 targetEnd == originTime + horizonHours"的断言，防止再次漂移。

## 人工验证

分析服务在虚拟机上产出真实产物后，用同一 `stationId` 分别请求 horizon=1/6/24，核对首点起点等于 `originTime`、末点结束等于 `originTime + horizon`，并与 Qt 分析页显示的钟点逐一对照。

## 数据迁移

无。历史产物尚未生成（Hive/Spark 未执行），不存在需要迁移的旧预测结果。

## 回退办法

回退到旧口径需同时改回三处：`build_features.py` 的 `target_lead_rows`（返回 h+1）与 `yesterday_baseline_lag`（返回 23-h）、`forecast_service.py` 的 `target_start` 偏移。但回退后 h=24 的昨日同小时基线不可构造，等于放弃该时域的基线对照，不建议。

## 三方复核

- 成员1批准：待签（共享夹具与 `contracts.md` 维护人）
- 成员2页面复核：待签
- 成员3数据复核：已复核（提出人）

未经三方复核，`docs/stage2/contracts.md` §4 的澄清与本单一并视为草案；分析服务实现已按新口径落地，若复核结论相反则按上节回退。
