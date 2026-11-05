from __future__ import annotations  # 3.10 延迟注解。

import math  # RMSE 需要开平方。
from dataclasses import dataclass  # 评测结果打包成 Evaluation。
from decimal import Decimal  # 高峰排序比较发布用的两位字符串，不退回二进制浮点。
from typing import Any  # 小时特征行为 dict。

import numpy as np  # 特征矩阵与误差向量。
from sklearn.ensemble import HistGradientBoostingRegressor  # 直方图梯度提升回归，适合表格特征。

from .dataset import time_split  # 按时间切训练/测试，禁止随机划分。


MINIMUM_BASELINE_SAMPLES = 30  # 少于这个数就不认为"打赢基线"这句话有统计意义。

PEAK_TOP_N = 3  # 总规格固定为 Top3，不可按数据情况自行放宽。
PEAK_QUANTILE = 0.90  # 总规格要求的"训练集历史 P90"高负荷线。
MINIMUM_PEAK_SAMPLES = 20  # 训练小时太少时分位数没有意义，宁可不发布阈值。

STAGE2_FEATURE_NAMES = (  # Spark 导出的正式特征顺序；保存进模型产物，推理时不得另排。
    "hour_of_day", "weekday_monday_0", "is_weekend", "month", "day_of_month",
    "week_of_year", "session_count", "coverage_ratio", "lag1_kwh", "lag24_kwh",
    "rolling_mean_3_kwh", "rolling_mean_24_kwh", "station_code",
)


@dataclass  # 一次时域评测的误差与选用模式。
class Evaluation:  # 1/6/24 小时 MAE/RMSE 及 trained/baseline。
    horizon_hours: int  # 预测时域：1 / 6 / 24 小时。
    model_mae: float  # 树模型平均绝对误差。
    model_rmse: float  # 树模型均方根误差。
    baseline_mae: float  # 昨日同时刻基线 MAE。
    baseline_rmse: float  # 昨日同时刻基线 RMSE。
    sample_count: int  # 测试集样本数。
    selected_mode: str  # 模型更好则 trained，否则 baseline。


def _features(rows: list[dict[str, Any]], station_codes: dict[str, int]) -> np.ndarray:  # 小时、星期、节假日、可用桩、站点编码五列。
    return np.array([[float(row["hour"]), float(row["weekday"]), float(row.get("is_holiday", 0)), float(row["available_piles"]), float(station_codes[str(row["station_id"])])] for row in rows])  # 五列特征。


def _metrics(actual: np.ndarray, predicted: np.ndarray) -> tuple[float, float]:  # 残差向量。
    errors = actual - predicted  # 残差向量。
    return float(np.mean(np.abs(errors))), float(math.sqrt(np.mean(errors ** 2)))  # MAE, RMSE。


def build_supervised(rows: list[dict[str, Any]], horizon_hours: int) -> tuple[list[dict[str, Any]], dict[str, int]]:  # 当前小时特征 → 未来 horizon 小时负荷；基线取目标窗口的昨日同时刻。
    if horizon_hours not in {1, 6, 24}:  # 只评这三个时域，与 /forecast 查询参数一致。
        raise ValueError("horizon must be 1, 6 or 24")  # 其它时域不训练。
    stations = sorted({str(row["station_id"]) for row in rows})  # 出现过的站点，排序保证编码稳定。
    station_codes = {station: index for index, station in enumerate(stations)}  # 字符串站号 → 整数特征。
    by_station = {station: sorted([row for row in rows if str(row["station_id"]) == station], key=lambda r: str(r["hour_start"])) for station in stations}  # 每站按小时排好。
    supervised = []  # 带 target 与 baseline 的监督样本。
    for station_rows in by_station.values():  # 分站构造，避免跨站用别人的负荷当昨天。
        for index in range(24, len(station_rows) - horizon_hours):  # 需要至少 24 小时历史，且后面还有 horizon 步。
            row = dict(station_rows[index])  # 拷贝当前小时特征；特征时刻不得晚于预测起点。
            row["target"] = float(station_rows[index + horizon_hours]["load_kwh"])  # 未来 horizon 小时的真实负荷。
            row["target_hour_start"] = str(station_rows[index + horizon_hours]["hour_start"])  # 目标窗口起点，供泄漏测试。
            row["baseline"] = float(station_rows[index + horizon_hours - 24]["load_kwh"])  # 该未来点的昨日同时刻。
            supervised.append(row)  # 加入监督表。
    if len(supervised) < 10:  # 样本太少拟合无意义。
        raise ValueError("insufficient history for model evaluation")  # 公开聚合 72 小时×3 站应能过门。
    return supervised, station_codes  # 监督表与稳定站点编码。


def fit_horizon(rows: list[dict[str, Any]], horizon_hours: int) -> tuple[Evaluation, HistGradientBoostingRegressor, dict[str, int]]:  # 拟合一个时域并同时返回估计器，便于落盘。
    supervised, station_codes = build_supervised(rows, horizon_hours)  # 造样本。
    train, test = time_split(supervised, .8)  # 80% 时间训练，20% 测试。
    estimator = HistGradientBoostingRegressor(max_depth=6, learning_rate=.08, random_state=42)  # 浅树+固定随机种子。
    estimator.fit(_features(train, station_codes), np.array([row["target"] for row in train]))  # 用训练特征拟合目标负荷。
    actual = np.array([row["target"] for row in test])  # 测试真实值。
    predicted = estimator.predict(_features(test, station_codes))  # 测试预测。
    baseline = np.array([row["baseline"] for row in test])  # 测试基线。
    model_mae, model_rmse = _metrics(actual, predicted)  # 模型误差。
    baseline_mae, baseline_rmse = _metrics(actual, baseline)  # 基线误差。
    selected = "trained" if model_mae < baseline_mae else "baseline"  # 未稳定优于基线则该时域单独发布基线。
    evaluation = Evaluation(horizon_hours, model_mae, model_rmse, baseline_mae, baseline_rmse, len(test), selected)  # 打包。
    return evaluation, estimator, station_codes  # 评测 + 模型 + 编码。


def evaluate(rows: list[dict[str, Any]], horizon_hours: int) -> Evaluation:  # 兼容 demo_pipeline：只返回评测，不暴露估计器。
    evaluation, _estimator, _codes = fit_horizon(rows, horizon_hours)  # 复用同一套切分与超参。
    return evaluation  # 教学流水线只要字典。


def predict_load(estimator: HistGradientBoostingRegressor, origin: dict[str, Any], station_codes: dict[str, int]) -> float:  # 用已发布估计器对单个预测起点打分。
    return float(estimator.predict(_features([origin], station_codes))[0])  # 单行特征 → 标量负荷。


def _as_float(value: Any) -> float:
    """Spark JSON nulls remain NaN; HGBR supports missing numeric features without inventing zero."""
    if value is None or value == "":
        return float("nan")
    if isinstance(value, bool):
        return 1.0 if value else 0.0
    return float(value)


def stage2_station_codes(rows: list[dict[str, Any]]) -> dict[str, int]:
    return {station: index for index, station in enumerate(sorted({str(row["station_id"]) for row in rows}))}


def stage2_feature_matrix(rows: list[dict[str, Any]], station_codes: dict[str, int]) -> np.ndarray:
    matrix = []
    for row in rows:
        values = [
            _as_float(row.get("hour_of_day")),
            _as_float(row.get("weekday_monday_0")),
            _as_float(row.get("is_weekend")),
            _as_float(row.get("month")),
            _as_float(row.get("day_of_month")),
            _as_float(row.get("week_of_year")),
            _as_float(row.get("session_count")),
            _as_float(row.get("coverage_ratio")),
            _as_float(row.get("lag1_kwh")),
            _as_float(row.get("lag24_kwh")),
            _as_float(row.get("rolling_mean_3_kwh")),
            _as_float(row.get("rolling_mean_24_kwh")),
            float(station_codes[str(row["station_id"])]),
        ]
        matrix.append(values)
    return np.asarray(matrix, dtype=float)


def chronological_stage2_split(
    rows: list[dict[str, Any]],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]], list[dict[str, Any]]]:
    """Split by origin timestamp and purge labels crossing the next split boundary."""
    ordered = sorted(rows, key=lambda item: (str(item["feature_available_at"]), str(item["station_id"])))
    times = sorted({str(row["feature_available_at"]) for row in ordered})
    if len(times) < 10:
        raise ValueError("DATA_INSUFFICIENT: fewer than 10 distinct origins")
    train_boundary = times[max(1, int(len(times) * 0.70))]
    test_boundary = times[max(2, int(len(times) * 0.85))]
    train = [row for row in ordered if str(row["feature_available_at"]) < train_boundary and str(row["target_end"]) <= train_boundary]
    validation = [row for row in ordered if train_boundary <= str(row["feature_available_at"]) < test_boundary and str(row["target_end"]) <= test_boundary]
    test = [row for row in ordered if str(row["feature_available_at"]) >= test_boundary]
    if min(len(train), len(validation), len(test)) < 3:
        raise ValueError("DATA_INSUFFICIENT: chronological split too small")
    return train, validation, test


def _baseline_values(rows: list[dict[str, Any]], field: str) -> tuple[np.ndarray, np.ndarray]:
    pairs = [
        (float(row["target_energy_kwh"]), float(row[field]))
        for row in rows
        if row.get(field) not in (None, "")
    ]
    if not pairs:
        return np.asarray([], dtype=float), np.asarray([], dtype=float)
    return np.asarray([item[0] for item in pairs]), np.asarray([item[1] for item in pairs])


def fit_stage2_horizon(
    all_rows: list[dict[str, Any]], horizon: int
) -> tuple[dict[str, Any], HistGradientBoostingRegressor, dict[str, int]]:
    """Train one horizon, select against two baselines on validation, and report untouched test metrics."""
    rows = [row for row in all_rows if int(row.get("horizon_hours", 0)) == horizon]
    train, validation, test = chronological_stage2_split(rows)
    station_codes = stage2_station_codes(rows)
    estimator = HistGradientBoostingRegressor(
        max_depth=6,
        learning_rate=0.06,
        max_iter=180,
        l2_regularization=0.1,
        random_state=42,
    )
    estimator.fit(stage2_feature_matrix(train, station_codes), np.asarray([float(row["target_energy_kwh"]) for row in train]))

    val_actual = np.asarray([float(row["target_energy_kwh"]) for row in validation])
    val_model = estimator.predict(stage2_feature_matrix(validation, station_codes))
    val_model_mae, val_model_rmse = _metrics(val_actual, val_model)
    baseline_candidates: dict[str, tuple[float, float, int]] = {}
    for field, label in (("baseline_yesterday_kwh", "yesterday_same_hour"), ("baseline_recent3_kwh", "recent_3h_mean")):
        actual, predicted = _baseline_values(validation, field)
        if len(actual):
            mae, rmse = _metrics(actual, predicted)
            baseline_candidates[label] = (mae, rmse, len(actual))
    if not baseline_candidates:
        baseline_name, baseline_mae, baseline_rmse, baseline_samples = "unavailable", math.inf, math.inf, 0
    else:
        baseline_name, (baseline_mae, baseline_rmse, baseline_samples) = min(
            baseline_candidates.items(), key=lambda item: (item[1][0], item[0])
        )
    # 稀疏小时轴上 lag24 大量缺失，验证集可能几乎没有可比基线样本。
    # 那种情况下 val_model_mae < inf 恒成立，不能据此宣布"模型打赢基线"。
    baseline_comparable = baseline_samples >= MINIMUM_BASELINE_SAMPLES and math.isfinite(baseline_mae)
    if not baseline_comparable:
        selected_mode, selection_caveat = "TRAINED", "NO_COMPARABLE_BASELINE"
    elif val_model_mae < baseline_mae:
        selected_mode, selection_caveat = "TRAINED", None
    else:
        selected_mode, selection_caveat = "BASELINE", None

    test_actual = np.asarray([float(row["target_energy_kwh"]) for row in test])
    test_model = estimator.predict(stage2_feature_matrix(test, station_codes))
    test_model_mae, test_model_rmse = _metrics(test_actual, test_model)
    baseline_field = "baseline_yesterday_kwh" if baseline_name == "yesterday_same_hour" else "baseline_recent3_kwh"
    base_actual, base_predicted = _baseline_values(test, baseline_field)
    test_baseline_mae, test_baseline_rmse = _metrics(base_actual, base_predicted) if len(base_actual) else (math.inf, math.inf)
    evaluation = {
        "horizonHours": horizon,
        "selectedMode": selected_mode,
        "selectedBaseline": baseline_name,
        "selectionCaveat": selection_caveat,
        "baselineComparable": baseline_comparable,
        "minimumBaselineSamples": MINIMUM_BASELINE_SAMPLES,
        "trainSamples": len(train),
        "validationSamples": len(validation),
        "testSamples": len(test),
        "baselineValidationSamples": baseline_samples,
        "validation": {
            "modelMae": val_model_mae,
            "modelRmse": val_model_rmse,
            "baselineMae": baseline_mae,
            "baselineRmse": baseline_rmse,
        },
        "test": {
            "modelMae": test_model_mae,
            "modelRmse": test_model_rmse,
            "baselineMae": test_baseline_mae,
            "baselineRmse": test_baseline_rmse,
            "baselineSamples": len(base_actual),
        },
    }
    return evaluation, estimator, station_codes


def predict_stage2(
    estimator: HistGradientBoostingRegressor,
    feature_row: dict[str, Any],
    station_codes: dict[str, int],
) -> float:
    station = str(feature_row["station_id"])
    if station not in station_codes:
        raise ValueError(f"UNKNOWN_ANALYSIS_STATION:{station}")
    return max(0.0, float(estimator.predict(stage2_feature_matrix([feature_row], station_codes))[0]))


def rank_peak_hours(points: list[dict[str, Any]], top_n: int = PEAK_TOP_N) -> list[dict[str, Any]]:
    """Take the Top-N forecast hours ordered by load descending, then target time ascending.

    The tie order is part of the contract, so equal loads must resolve by time rather than by the
    order the points happen to arrive in. Points without a value are not candidates at all: a missing
    hour is unknown, not low.
    """
    usable = [item for item in points if item.get("energyKwh") not in (None, "")]
    ordered = sorted(usable, key=lambda item: (-Decimal(str(item["energyKwh"])), str(item["targetStart"])))
    return [
        {
            "rank": index + 1,
            "targetStart": item["targetStart"],
            "targetEnd": item["targetEnd"],
            "horizonHours": item.get("horizonHours"),
            "energyKwh": item["energyKwh"],
            "meanPowerKw": item.get("meanPowerKw"),
        }
        for index, item in enumerate(ordered[: max(1, top_n)])
    ]


def training_peak_thresholds(
    all_rows: list[dict[str, Any]],
    quantile: float = PEAK_QUANTILE,
    minimum_samples: int = MINIMUM_PEAK_SAMPLES,
) -> dict[str, Any]:
    """Derive the per-station high-load line from the training split only.

    Two things make this narrower than a plain quantile over the training table. The line must not
    see validation or test hours, so the chronological split runs first. And the same clock hour is
    the label of many origins, so hours are collapsed by target_end before the quantile: otherwise
    hours reachable from more origins would count several times and pull the line around.
    """
    horizons = sorted({int(row["horizon_hours"]) for row in all_rows if row.get("horizon_hours")})
    if not horizons:
        return {"status": "UNAVAILABLE", "reason": "NO_TRAINING_ROWS", "quantile": quantile, "stations": {}}
    try:
        train, _validation, _test = chronological_stage2_split(
            [row for row in all_rows if int(row["horizon_hours"]) == horizons[0]]
        )
    except ValueError as error:
        return {"status": "UNAVAILABLE", "reason": str(error), "quantile": quantile, "stations": {}}
    observed: dict[str, dict[str, float]] = {}
    for row in train:
        value = row.get("target_energy_kwh")
        if value in (None, ""):
            continue
        observed.setdefault(str(row["station_id"]), {})[str(row["target_end"])] = float(value)
    stations: dict[str, Any] = {}
    for station, hours in sorted(observed.items()):
        values = sorted(hours.values())
        if len(values) < minimum_samples:
            stations[station] = {
                "thresholdEnergyKwh": None,
                "sampleCount": len(values),
                "reason": "TOO_FEW_TRAINING_HOURS",
            }
            continue
        stations[station] = {
            "thresholdEnergyKwh": f"{float(np.quantile(values, quantile)):.2f}",
            "sampleCount": len(values),
            "reason": None,
        }
    published = [item for item in stations.values() if item["thresholdEnergyKwh"] is not None]
    return {
        "status": "OK" if published else "UNAVAILABLE",
        "reason": None if published else "NO_STATION_MET_MINIMUM_TRAINING_HOURS",
        "quantile": quantile,
        "minimumSamples": minimum_samples,
        "source": "TRAINING_SPLIT_DISTINCT_TARGET_HOURS",
        "stations": stations,
    }


def precision_at_peak(
    curves: dict[tuple[str, str], list[dict[str, Any]]],
    top_n: int = PEAK_TOP_N,
    minimum_points: int = PEAK_TOP_N,
) -> dict[str, Any]:
    """Score predicted peak hours against the observed peak hours of the same origin window.

    Scoring is per origin: the forecast curve for one origin is compared with what actually happened
    over that window, and the metric is how many of the predicted Top-N hours were truly Top-N.
    Windows shorter than minimum_points are skipped rather than scored against a smaller N, which
    would inflate the average.
    """
    scores: list[float] = []
    total_hits = 0
    for series in curves.values():
        usable = [item for item in series if item.get("actual") is not None and item.get("predicted") is not None]
        if len(usable) < minimum_points:
            continue
        size = min(top_n, len(usable))
        predicted_top = {
            int(item["horizonHours"])
            for item in sorted(usable, key=lambda row: (-float(row["predicted"]), int(row["horizonHours"])))[:size]
        }
        actual_top = {
            int(item["horizonHours"])
            for item in sorted(usable, key=lambda row: (-float(row["actual"]), int(row["horizonHours"])))[:size]
        }
        hits = len(predicted_top & actual_top)
        total_hits += hits
        scores.append(hits / size)
    if not scores:
        return {
            "status": "UNAVAILABLE",
            "reason": "NO_TEST_WINDOW_LONG_ENOUGH",
            "topN": top_n,
            "originsEvaluated": 0,
        }
    return {
        "status": "OK",
        "reason": None,
        "topN": top_n,
        "originsEvaluated": len(scores),
        "peakHits": total_hits,
        "precisionAtTopN": round(sum(scores) / len(scores), 6),
    }

# 【文件讲解·答辩用】
# 功能作用：在 1/6/24 小时时域上训练直方图梯度提升，并与“昨日同时刻”基线比 MAE。
# 技术原理：监督样本是当前小时特征预测未来负荷；时间切分防泄漏；只有模型 MAE 更低才 selected_mode=trained。
# 代码思路：build_supervised 滑窗 → time_split → fit_horizon 拟合 → evaluate 给教学流水线。
# 与其他文件关系：特征来自 build_curated；切分用 dataset.time_split；train.py 把打赢基线的时域写成 joblib。
# 答辩时可以怎么讲：大屏实时曲线用的是规则 3 点均值；离线评测才用这棵树；某个时域打不过就只发布该时域基线。
