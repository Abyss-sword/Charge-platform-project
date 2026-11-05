# 接口变更单

- 编号：ICR-20260917-02
- 提出人 / 日期：成员3 / 2026-09-17
- 需求编号：S2-10（车主推荐）
- 动作名：分析服务 `GET /recommend`，契约冻结为 `member3_recommendation_v1`

## 旧字段与示例

```json
{
  "status": "OK",
  "degraded": true,
  "reasons": ["NO_CONFIRMED_ANALYSIS_TO_BUSINESS_STATION_MAPPING", "PREDICTED_LOAD_EXCLUDED"],
  "modelMode": "BASELINE",
  "source": "READ_ONLY_BUSINESS_SNAPSHOT",
  "origin": { "latitude": 24.48, "longitude": 118.09, "arrivalTimeMode": "USER_SELECTED_OR_CURRENT" },
  "recommendations": [{
    "station_id": "1", "name": "滨海示范站", "distance_km": 0.0,
    "wait_minutes": 0.0, "price_yuan_per_kwh": "1.20",
    "predicted_load_ratio": null, "available_piles": 2,
    "score": "0.000000", "reasons": ["预计等待短"], "model_mode": "baseline"
  }]
}
```

旧口径权重为距离 0.40 / 等待 0.35 / 价格 0.25，`score` 越小越好，无契约版本号。

## 新字段与示例

```json
{
  "contractVersion": "member3_recommendation_v1",
  "status": "OK",
  "degraded": true,
  "reasons": ["NO_CAPACITY_DATA", "NO_CONFIRMED_ANALYSIS_TO_BUSINESS_STATION_MAPPING",
              "PREDICTED_AVAILABILITY_UNAVAILABLE"],
  "datasetVersion": "course-local-v1",
  "runId": "course-local-20260915",
  "modelMode": "BASELINE",
  "source": "READ_ONLY_BUSINESS_SNAPSHOT",
  "origin": {
    "latitude": 24.48, "longitude": 118.09,
    "arrivalTime": "2015-10-01T08:00:00Z",
    "arrivalTimeUsed": false,
    "arrivalTimeReason": "NO_CAPACITY_DATA"
  },
  "scoring": {
    "consideredCandidates": 3,
    "excludedForNoCriteria": [],
    "criteriaUsed": ["currentAvailability", "distance", "price"],
    "criteriaExcluded": ["predictedAvailability"],
    "declaredWeights": { "distance": "0.30", "price": "0.20",
                         "currentAvailability": "0.25", "predictedAvailability": "0.25" },
    "tieBreak": ["score", "distanceKm", "idlePiles", "stationId"]
  },
  "recommendations": [{
    "rank": 1, "stationId": 1, "name": "滨海示范站",
    "distanceKm": 0.0, "priceYuanPerKwh": "1.20",
    "currentAvailabilityRatio": "0.50", "predictedAvailabilityRatio": null,
    "idlePiles": 2, "capacityPiles": 4,
    "score": "1.000000",
    "criteria": { "distance": "1.000000", "price": "1.000000", "currentAvailability": "1.000000" },
    "appliedWeights": { "distance": "0.4000", "price": "0.2667", "currentAvailability": "0.3333" },
    "reasons": ["CLOSEST", "CHEAPEST", "MOST_AVAILABLE_NOW"]
  }]
}
```

`score` 改为**越大越好**（各项统一"1 为最优"，成本项归一时反向）。

## 修改原因

《00-总规格与决策》§7 对推荐的规定与旧实现有四处冲突：

1. 规格权重是距离 0.30、价格 0.20、当前可用比例 0.25、预测可用比例 0.25。旧实现是距离 0.40、等待 0.35、价格 0.25，既缺"当前可用比例"这一项（`available_piles` 只作展示、明确不进公式），权重数值也全不对。
2. 规格写"无需假设有排队系统，不显示虚构等待分钟"。旧实现的等待分钟由 `business_snapshot.py` 按 `max(5, 充电中桩数×15 + 故障桩数×5)` 推算——业务快照里根本没有排队数据，这是凭空构造的值，还占了 0.35 权重并向用户输出"预计等待短"的理由。
3. 规格要求"缺失项剔除后剩余权重重新归一"、"同值成本项取 1"、"全部指标缺失时返回数据不足"。旧实现在缺预测时改用一套硬编码的三项权重，且同值时归一为 0（当作最差），方向相反。
4. 规格的同分次序是距离（未知排末）→ 当前空闲桩降序 → 站点 ID 升序。旧实现是距离 → 站点 ID，缺空闲桩一档。

另外成员2把 `/recommend` 标为 BLOCKED 的直接理由是"缺冻结的 recommendation 版本与 schema、null/时间/错误规则"，本单同时补上 `contractVersion` 与 `arrivalTime` 的语义。

## 是否影响单位、状态或必填性

字段名由 snake_case 统一为 camelCase，与 `forecast_v1` 一致。`stationId` 由字符串改为整数（与业务快照的 `stationId` 同型）。`score` 语义反转为越大越好。删除 `wait_minutes`、`predicted_load_ratio`、`available_piles`、`model_mode`；新增 `rank`、`currentAvailabilityRatio`、`predictedAvailabilityRatio`、`idlePiles`、`capacityPiles`、`criteria`、`appliedWeights` 与顶层 `contractVersion`、`scoring`。价格与比例仍是两位十进制字符串。

`status` 取值：`OK`、`NO_DATA`（无候选或无任何可用指标）、`DEPENDENCY_UNAVAILABLE`（业务快照缺失，HTTP 503）、`PARAM_INVALID`（HTTP 400，含 `arrivalTime` 非 ISO8601）。

## 影响端

分析服务（成员3）、Qt 车主端推荐卡（成员2）、Web 大屏推荐面板（成员2）。第一阶段 FastAPI 教学接口 `analytics/src/api.py` 的 `/recommend` **不受影响**：它继续使用旧的 `rank_stations` 与 `StationCandidate`，两套口径并存且各有测试。

## 兼容办法

无需兼容旧读法：已核查 `dashboard/` 与 `clients/` 均未引用 `wait_minutes`、`predicted_load_ratio`、`available_piles`，与成员2文档中"`/recommend` BLOCKED、尚未消费"一致。`scoring.declaredWeights` 与逐条 `appliedWeights` 一并返回，界面与验收都能直接核对权重是否符合规格，不必反推。

## 待办分工

| 端 | 待办 | 状态 |
|---|---|---|
| 成员3 | 规格权重、归一方向、缺失项重新归一、同分次序、删除虚构等待 | 已完成，本地 132 项测试通过 |
| 成员3 | 待有容量标签的数据源后，填充 `predictedAvailabilityRatio` 并启用 `arrivalTime` 计算 | 阻塞于数据（S2-08 同因） |
| 成员2 | 推荐卡按新字段渲染，显示"当前可用"与"到达时可用不可得"的区别、理由码本地化 | 待处理 |
| 成员1 | 复核权重与规格一致性，纳入 S2 矩阵 | 待处理 |

## 自动测试

- `analytics/tests/test_recommend.py`：权重值与规格逐字对齐；更近更便宜更空的站点排前；缺失项剔除后三项权重和为 1 且取值为 0.4000/0.2667/0.3333；同值成本项取 1；同分依次按距离、空闲桩、站点 ID；未知距离排末；无任何指标的候选被剔除；权重与输出里不存在任何 wait 字段。
- `analytics/tests/test_business_snapshot.py`：候选带 `current_availability_ratio`、`predicted_availability_ratio` 为 None、不再有 `wait_minutes` 属性；全部桩故障的站点被排除。
- `analytics/tests/test_flask_api.py`：响应含 `contractVersion`、`rank`、`scoring.criteriaExcluded`；`arrivalTime` 回显且 `arrivalTimeUsed=false`；`arrivalTime` 非法返回 400。

## 人工验证

启动正式 Flask 后请求 `/recommend?latitude=<纬度>&longitude=<经度>&limit=3&arrivalTime=<ISO8601>`，核对：`scoring.declaredWeights` 与总规格 §7 §一致；每条 `appliedWeights` 之和为 1；`recommendations` 中不含任何等待字段；`origin.arrivalTimeUsed` 为 false；距离最近且最空的站点排名靠前。

## 数据迁移

无。仅改变响应形状，不读写业务库。

## 回退办法

回退需还原 `recommend.py` 的正式口径段、`business_snapshot.build_dynamic_candidates` 与 `flask_api.py` 的 `/recommend`。回退会重新引入规格禁止的虚构等待分钟，不建议。

## 三方复核

- 成员1批准：待签
- 成员2页面复核：待签
- 成员3数据复核：已复核（提出人）
