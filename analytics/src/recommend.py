from __future__ import annotations  # 3.10 延迟注解。

from dataclasses import asdict, dataclass  # 冻结候选站；asdict 把字段摊进返回 JSON。
from decimal import Decimal  # 归一化和加权打分不用 float，避免排序抖动。
from typing import Iterable  # 允许传入 list 或生成器。


@dataclass(frozen=True)  # 推荐候选站不可变。
class StationCandidate:  # 站号、距离、等待、价格、可选预测负荷。
    station_id: str  # 内部站号，字符串便于 JSON。
    name: str  # 展示名，如东软北门站。
    distance_km: float  # 距用户公里数，教学接口用固定演示值。
    wait_minutes: float  # 预计等待分钟。
    price_yuan_per_kwh: str  # 单价用字符串，与业务 Decimal 口径一致。
    predicted_load_ratio: float | None  # 有模型时为 0~1 负荷比；None 则走无模型权重。
    available_piles: int  # 空闲桩数，供前端展示，不直接进当前权重公式。


def _normalize(values: list[Decimal]) -> list[Decimal]:  # min-max 缩放到 0~1，全相等则全 0。
    low, high = min(values), max(values)  # 该指标在候选集上的最小最大。
    return [Decimal("0") for _ in values] if high == low else [(value - low) / (high - low) for value in values]  # 全相等则全 0，避免除零；否则缩放到 [0,1]。


def rank_stations(stations: Iterable[StationCandidate], limit: int = 3) -> list[dict[str, object]]:  # 指标越小越好：距离、等待、价格、预测负荷；加权求和后取升序前 limit 名。
    items = list(stations)  # 物化，便于按下标对齐归一化向量。
    if not items:  # 没有候选站。
        return []  # 空推荐。
    has_model = all(item.predicted_load_ratio is not None for item in items)  # 全部都有预测才启用负荷权重。
    weights = {"distance": Decimal("0.3"), "wait": Decimal("0.3"), "price": Decimal("0.2"), "load": Decimal("0.2")} if has_model else {"distance": Decimal("0.4"), "wait": Decimal("0.35"), "price": Decimal("0.25")}  # 无模型时把权重分给距离/等待/价格。
    normalized = {  # 各指标 0~1，0 表示相对最优。
        "distance": _normalize([Decimal(str(item.distance_km)) for item in items]),  # 距离归一化。
        "wait": _normalize([Decimal(str(item.wait_minutes)) for item in items]),  # 等待归一化。
        "price": _normalize([Decimal(item.price_yuan_per_kwh) for item in items]),  # 价格归一化。
    }  # 结束：各指标 0~1，0 表示相对最优。
    if has_model:  # 有预测负荷才加第四维。
        normalized["load"] = _normalize([Decimal(str(item.predicted_load_ratio)) for item in items])  # 负荷比归一化。
    ranked = []  # 带 score 和中文 reasons 的字典列表。
    for index, item in enumerate(items):  # 与归一化下标对齐。
        score = sum((weights[key] * normalized[key][index] for key in weights), Decimal("0"))  # 加权和，越小越好。
        reasons = []  # 可解释短句。
        if normalized["wait"][index] <= .34: reasons.append("预计等待短")  # 相对等待落在较低三分位附近。
        if normalized["distance"][index] <= .34: reasons.append("距离较近")  # 相对更近。
        if normalized["price"][index] <= .34: reasons.append("价格较低")  # 相对更便宜。
        if has_model and normalized["load"][index] <= .34: reasons.append("预测负荷较低")  # 相对更空。
        if not reasons: reasons.append("综合条件较均衡")  # 没有单项突出时给兜底说明。
        ranked.append({**asdict(item), "score": format(score.quantize(Decimal("0.000001")), "f"), "reasons": reasons, "model_mode": "trained" if has_model else "baseline"})  # 展开字段并带上打分模式。
    return sorted(ranked, key=lambda row: (Decimal(str(row["score"])), Decimal(str(row["distance_km"])), str(row["station_id"])))[:limit]  # 分数、距离、站号三重排序后截断。

"""以下为第二阶段正式推荐口径，与上面第一阶段的教学 rank_stations 并存。

两者不能合并：教学接口按"距离/等待/价格"打分，而总规格 §7 的正式口径没有等待项，
并明确写"无需假设有排队系统，不显示虚构等待分钟"。旧函数仍由第一阶段 FastAPI 教学
路由使用，因此原样保留；正式 Flask 服务只能走下面这套。
"""

# 总规格 §7 的初始权重，缺失项剔除后按剩余项重新归一。
SPEC_WEIGHTS: dict[str, Decimal] = {
    "distance": Decimal("0.30"),
    "price": Decimal("0.20"),
    "currentAvailability": Decimal("0.25"),
    "predictedAvailability": Decimal("0.25"),
}
COST_CRITERIA = frozenset({"distance", "price"})  # 成本项：值越大越差，归一时反向。
PROMINENT_CRITERION_SCORE = Decimal("0.67")  # 归一值高于此线才在理由里点名该项。
CRITERION_REASONS = {
    "distance": "CLOSEST",
    "price": "CHEAPEST",
    "currentAvailability": "MOST_AVAILABLE_NOW",
    "predictedAvailability": "MOST_AVAILABLE_ON_ARRIVAL",
}


@dataclass(frozen=True)
class FormalCandidate:
    """正式推荐候选。没有等待分钟，规格禁止虚构该值。"""

    station_id: int
    name: str
    distance_km: float | None
    price_yuan_per_kwh: str | None
    current_availability_ratio: str | None  # 当前空闲桩 / 容量，两位字符串。
    predicted_availability_ratio: str | None  # 预计到达小时的可用比例；无容量标签时为 None。
    idle_piles: int
    capacity_piles: int


def _criterion_values(candidates: list[FormalCandidate]) -> dict[str, dict[int, Decimal]]:
    raw: dict[str, dict[int, Decimal]] = {name: {} for name in SPEC_WEIGHTS}
    for item in candidates:
        readings = {
            "distance": item.distance_km,
            "price": item.price_yuan_per_kwh,
            "currentAvailability": item.current_availability_ratio,
            "predictedAvailability": item.predicted_availability_ratio,
        }
        for name, value in readings.items():
            if value is None or value == "":
                continue
            raw[name][item.station_id] = Decimal(str(value))
    return raw


def _scale(values: dict[int, Decimal], cost: bool) -> dict[int, Decimal]:
    """Min-max scale into [0,1] where 1 is always the better end.

    Cost criteria are inverted so that "high is good" holds for every criterion, which keeps the
    weighted sum readable. When every candidate reads the same the spec fixes the result at 1: no
    candidate is worse than another on a criterion that does not separate them.
    """
    if not values:
        return {}
    low, high = min(values.values()), max(values.values())
    if high == low:
        return {station: Decimal(1) for station in values}
    span = high - low
    if cost:
        return {station: (high - value) / span for station, value in values.items()}
    return {station: (value - low) / span for station, value in values.items()}


def rank_formal_candidates(
    candidates: Iterable[FormalCandidate], limit: int = 3
) -> tuple[list[dict[str, object]], dict[str, object]]:
    """Score candidates on the spec weights and return the ranking plus an auditable summary.

    Weights are renormalised per candidate over the criteria that candidate actually has, so one
    station missing a price does not silently score as if price were free. A candidate with no
    readings at all is dropped rather than ranked on nothing.
    """
    items = list(candidates)
    if not items:
        return [], {"consideredCandidates": 0, "excludedForNoCriteria": [], "criteriaUsed": []}
    scaled = {
        name: _scale(values, name in COST_CRITERIA)
        for name, values in _criterion_values(items).items()
    }
    ranked: list[dict[str, object]] = []
    dropped: list[int] = []
    for item in items:
        available = [name for name in SPEC_WEIGHTS if item.station_id in scaled[name]]
        if not available:
            dropped.append(item.station_id)
            continue
        total_weight = sum((SPEC_WEIGHTS[name] for name in available), Decimal(0))
        applied = {name: SPEC_WEIGHTS[name] / total_weight for name in available}
        score = sum((applied[name] * scaled[name][item.station_id] for name in available), Decimal(0))
        reasons = [
            CRITERION_REASONS[name]
            for name in available
            if scaled[name][item.station_id] >= PROMINENT_CRITERION_SCORE
        ]
        ranked.append({
            "stationId": item.station_id,
            "name": item.name,
            "distanceKm": item.distance_km,
            "priceYuanPerKwh": item.price_yuan_per_kwh,
            "currentAvailabilityRatio": item.current_availability_ratio,
            "predictedAvailabilityRatio": item.predicted_availability_ratio,
            "idlePiles": item.idle_piles,
            "capacityPiles": item.capacity_piles,
            "score": format(score.quantize(Decimal("0.000001")), "f"),
            "criteria": {
                name: format(scaled[name][item.station_id].quantize(Decimal("0.000001")), "f")
                for name in available
            },
            "appliedWeights": {name: format(applied[name].quantize(Decimal("0.0001")), "f") for name in available},
            "reasons": reasons or ["BALANCED"],
        })
    # 规格的同分次序：距离升序（未知排末）、当前空闲桩降序、站点ID升序。
    ranked.sort(key=lambda row: (
        -Decimal(str(row["score"])),
        Decimal("Infinity") if row["distanceKm"] is None else Decimal(str(row["distanceKm"])),
        -int(row["idlePiles"]),
        int(row["stationId"]),
    ))
    for index, row in enumerate(ranked, start=1):
        row["rank"] = index
    criteria_used = sorted(name for name, values in scaled.items() if values)
    summary = {
        "consideredCandidates": len(items),
        "excludedForNoCriteria": sorted(dropped),
        "criteriaUsed": criteria_used,
        "criteriaExcluded": sorted(set(SPEC_WEIGHTS) - set(criteria_used)),
        "declaredWeights": {name: format(weight, "f") for name, weight in SPEC_WEIGHTS.items()},
        "tieBreak": ["score", "distanceKm", "idlePiles", "stationId"],
    }
    return ranked[: max(1, limit)], summary

# 【文件讲解·答辩用】
# 功能作用：把候选站按距离、等待、价格（及可选预测负荷）加权排序，并给出中文理由。
# 技术原理：min-max 归一化后加权；分数越小越好；无预测时自动改权重，不拿 0 填充假装有模型。
# 代码思路：先判断 has_model → 归一化 → 逐站打分和 reasons → 稳定排序取 TopN。
# 与其他文件关系：api.py 的 GET /recommend 构造 StationCandidate 后调用；大屏实时推荐在 sqlite_dashboard 里另做可用桩排序。
# 答辩时可以怎么讲：这是可解释规则推荐，不是黑盒；模型只提供负荷比一项，没有模型也能推荐。
