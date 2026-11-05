# StationCandidate 是推荐输入结构；rank_stations 输出带 reasons 和 model_mode 的排序列表。
import pytest

from src.recommend import SPEC_WEIGHTS, FormalCandidate, StationCandidate, rank_formal_candidates, rank_stations


def _candidate(
    station_id: int,
    distance: float | None = 1.0,
    price: str | None = "1.20",
    availability: str | None = "0.50",
    predicted: str | None = None,
    idle: int = 2,
    capacity: int = 4,
) -> FormalCandidate:
    return FormalCandidate(
        station_id=station_id,
        name=f"站{station_id}",
        distance_km=distance,
        price_yuan_per_kwh=price,
        current_availability_ratio=availability,
        predicted_availability_ratio=predicted,
        idle_piles=idle,
        capacity_piles=capacity,
    )


# 有预测负荷时，等待更短的站点应排第一，并且理由里出现“预计等待短”。
def test_recommendation_explains_top_station_and_stable_ties():
    # 站点 2：更近、等待 2 分钟、更便宜、负荷 0.2、空闲桩更多；站点 1 全面更差。两者都有预测负荷。
    stations = [StationCandidate("2", "B", 1.0, 2, "1.20", .2, 3), StationCandidate("1", "A", 2.0, 10, "1.50", .8, 1)]
    # 默认取 Top3，这里只有两站，返回顺序应由综合分决定。
    ranked = rank_stations(stations)
    # 归一化后站点 2 的等待落在低分位，reasons 必须包含可向用户展示的“预计等待短”。
    assert ranked[0]["station_id"] == "2" and "预计等待短" in ranked[0]["reasons"]


# 两站都没有 predicted_load_ratio 时，应降级为 baseline 权重，而不是假装用了模型。
def test_missing_prediction_uses_baseline_weights():
    # 负荷位传 None；距离/等待/价格仍可比，算法会去掉 load 权重并改用 0.4/0.35/0.25。
    stations = [StationCandidate("1", "A", 1, 2, "1.20", None, 3), StationCandidate("2", "B", 2, 5, "1.30", None, 1)]
    # model_mode 写入每条结果，供大屏和答辩区分“训练模型分”还是“规则基线分”。
    assert rank_stations(stations)[0]["model_mode"] == "baseline"


def test_tied_scores_break_by_distance_then_station_id():
    # 指标完全相同，分数应相同，次序改由距离、再由站点号决定。
    stations = [
        StationCandidate("2", "B", 1.0, 5, "1.20", None, 1),
        StationCandidate("1", "A", 1.0, 5, "1.20", None, 1),
    ]
    ranked = rank_stations(stations)
    assert ranked[0]["station_id"] == "1" and ranked[1]["station_id"] == "2"


def test_spec_weights_match_the_master_specification():
    assert {name: str(weight) for name, weight in SPEC_WEIGHTS.items()} == {
        "distance": "0.30", "price": "0.20", "currentAvailability": "0.25", "predictedAvailability": "0.25",
    }


def test_formal_ranking_prefers_the_closer_cheaper_and_freer_station():
    ranked, summary = rank_formal_candidates([
        _candidate(1, distance=8.0, price="1.90", availability="0.10", idle=1),
        _candidate(2, distance=0.5, price="1.10", availability="0.90", idle=9, capacity=10),
    ])
    assert [row["stationId"] for row in ranked] == [2, 1]
    assert [row["rank"] for row in ranked] == [1, 2]
    assert set(ranked[0]["reasons"]) == {"CLOSEST", "CHEAPEST", "MOST_AVAILABLE_NOW"}
    assert summary["criteriaUsed"] == ["currentAvailability", "distance", "price"]


def test_missing_criterion_is_dropped_and_remaining_weights_are_renormalised():
    """No capacity labels means the predicted ratio is absent; the other three must sum back to 1."""
    ranked, summary = rank_formal_candidates([_candidate(1), _candidate(2, distance=3.0)])
    assert summary["criteriaExcluded"] == ["predictedAvailability"]
    applied = ranked[0]["appliedWeights"]
    assert "predictedAvailability" not in applied
    assert sum(float(value) for value in applied.values()) == pytest.approx(1.0)
    # 0.30 / (0.30+0.20+0.25) 与 0.20 / 0.75 与 0.25 / 0.75。
    assert applied == {"distance": "0.4000", "price": "0.2667", "currentAvailability": "0.3333"}


def test_identical_cost_criterion_scores_one_for_everyone():
    """A criterion that does not separate candidates must not penalise any of them."""
    ranked, _summary = rank_formal_candidates([_candidate(1), _candidate(2)])
    assert {row["criteria"]["price"] for row in ranked} == {"1.000000"}
    assert {row["criteria"]["distance"] for row in ranked} == {"1.000000"}
    assert {row["score"] for row in ranked} == {"1.000000"}


def test_ties_break_by_distance_then_idle_piles_then_station_id():
    ranked, _summary = rank_formal_candidates([
        _candidate(7, idle=2, capacity=4),
        _candidate(3, idle=2, capacity=4),
        _candidate(5, idle=2, capacity=4),
    ])
    assert [row["stationId"] for row in ranked] == [3, 5, 7]


def test_unknown_distance_sorts_last_among_tied_scores():
    ranked, _summary = rank_formal_candidates([
        _candidate(1, distance=None),
        _candidate(2, distance=4.0),
    ])
    assert [row["stationId"] for row in ranked] == [2, 1]
    assert "distance" not in ranked[1]["appliedWeights"]


def test_candidate_without_any_criterion_is_excluded_rather_than_ranked():
    ranked, summary = rank_formal_candidates([
        _candidate(1),
        _candidate(2, distance=None, price=None, availability=None),
    ])
    assert [row["stationId"] for row in ranked] == [1]
    assert summary["excludedForNoCriteria"] == [2]
    assert summary["consideredCandidates"] == 2


def test_formal_ranking_has_no_wait_criterion_at_all():
    """The spec forbids a fabricated queue time, so no wait term may reappear in the weights."""
    assert not any("wait" in name.lower() for name in SPEC_WEIGHTS)
    ranked, summary = rank_formal_candidates([_candidate(1)])
    assert not any("wait" in key.lower() for key in ranked[0])
    assert not any("wait" in name.lower() for name in summary["criteriaUsed"])

# 【文件讲解·答辩用】
# 功能作用：推荐不是黑盒打分。本文件确认两件事：排序能解释（理由文本来自指标分位），
#   以及没有负荷预测时必须标明 baseline，不能把规则分包装成模型分。
# 技术原理：rank_stations 把距离、等待、价格（以及可选负荷）各自 min-max 到 [0,1]，
#   再按权重加总。有全员 predicted_load_ratio 时权重含 load=0.2，model_mode=trained；
#   否则三因子权重，model_mode=baseline。某维归一化值 ≤0.34 就往 reasons 里塞对应中文。
#   同分再用距离、station_id 做稳定次序，避免刷新时两站来回跳。价格用 Decimal 字符串，
#   与营收一样避开二进制浮点。
# 代码思路：第一条构造“明显更好”的站点 2，检查既排第一又有“预计等待短”。第二条把
#   负荷全改 None，只断言头名的 model_mode，不纠结谁第一，重点是降级标记。
# 与其他文件关系：实现 analytics/src/recommend.py。api.py 的 /recommend 会组装
#   StationCandidate 再调用本函数。dashboard 的 RecommendationPanel.vue 展示 reasons。
#   负荷预测来自模型或大屏规则估算；缺预测时本测试保证前端仍能显示基线模式。
# 答辩讲法：打开第一条用例：“等待 2 分钟对 10 分钟，理由里会写预计等待短，用户能看懂。”
#   再强调没有模型就写 baseline，这是诚信披露，不是功能缺失。被问权重时背：有模型
#   距离 0.3、等待 0.3、价格 0.2、负荷 0.2；无模型 0.4 / 0.35 / 0.25。
