from __future__ import annotations

from datetime import datetime, timedelta, timezone

import numpy as np

from src.model import (
    chronological_stage2_split,
    fit_stage2_horizon,
    precision_at_peak,
    rank_peak_hours,
    stage2_feature_matrix,
    stage2_station_codes,
    training_peak_thresholds,
)


def _point(hour: int, energy: str | None):
    start = datetime(2026, 3, 1, tzinfo=timezone.utc) + timedelta(hours=hour - 1)
    return {
        "targetStart": start.isoformat().replace("+00:00", "Z"),
        "targetEnd": (start + timedelta(hours=1)).isoformat().replace("+00:00", "Z"),
        "horizonHours": hour,
        "energyKwh": energy,
        "meanPowerKw": energy,
    }


def _rows(count: int = 120):
    start = datetime(2026, 1, 1, tzinfo=timezone.utc)
    rows = []
    for index in range(count):
        origin = start + timedelta(hours=index)
        target = 5.0 + (index % 24) * 0.2
        rows.append({
            "station_id": "course:1",
            "feature_available_at": origin.isoformat().replace("+00:00", "Z"),
            "target_end": (origin + timedelta(hours=1)).isoformat().replace("+00:00", "Z"),
            "horizon_hours": 1,
            "target_energy_kwh": target,
            "hour_of_day": index % 24,
            "weekday_monday_0": index // 24,
            "is_weekend": False,
            "month": 1,
            "day_of_month": 1 + index // 24,
            "week_of_year": 1,
            "session_count": 1,
            "coverage_ratio": 1.0,
            "lag1_kwh": target - 0.2,
            "lag24_kwh": target,
            "rolling_mean_3_kwh": target - 0.4,
            "rolling_mean_24_kwh": 7.3,
            "baseline_yesterday_kwh": target,
            "baseline_recent3_kwh": target - 0.4,
        })
    return rows


def test_chronological_split_is_ordered_and_purged():
    train, validation, test = chronological_stage2_split(_rows())
    assert max(row["target_end"] for row in train) <= min(row["feature_available_at"] for row in validation)
    assert max(row["target_end"] for row in validation) <= min(row["feature_available_at"] for row in test)


def test_feature_matrix_does_not_include_target_values():
    rows = _rows(20)
    codes = stage2_station_codes(rows)
    before = stage2_feature_matrix(rows, codes)
    rows[-1]["target_energy_kwh"] = 999999.0
    after = stage2_feature_matrix(rows, codes)
    np.testing.assert_array_equal(before, after)


def test_stage2_model_reports_validation_and_untouched_test_metrics():
    evaluation, _estimator, codes = fit_stage2_horizon(_rows(), 1)
    assert evaluation["horizonHours"] == 1
    assert evaluation["selectedMode"] in {"TRAINED", "BASELINE"}
    assert evaluation["trainSamples"] > evaluation["testSamples"] > 0
    assert evaluation["validation"]["modelMae"] >= 0
    assert evaluation["test"]["modelRmse"] >= 0
    assert codes == {"course:1": 0}


def test_missing_baselines_are_not_reported_as_a_win_over_the_baseline():
    """On the sparse hourly axis lag24 is mostly null; an absent baseline must not count as a model win."""
    rows = _rows()
    for row in rows:
        row["baseline_yesterday_kwh"] = None
        row["baseline_recent3_kwh"] = None
    evaluation, _estimator, _codes = fit_stage2_horizon(rows, 1)
    assert evaluation["baselineComparable"] is False
    assert evaluation["selectionCaveat"] == "NO_COMPARABLE_BASELINE"
    assert evaluation["baselineValidationSamples"] == 0


def test_comparable_baseline_keeps_a_plain_selection_without_caveat():
    evaluation, _estimator, _codes = fit_stage2_horizon(_rows(400), 1)
    assert evaluation["baselineComparable"] is True
    assert evaluation["selectionCaveat"] is None
    assert evaluation["baselineValidationSamples"] >= evaluation["minimumBaselineSamples"]


def test_peak_hours_take_top_three_by_load_then_time():
    peaks = rank_peak_hours([_point(1, "4.00"), _point(2, "9.50"), _point(3, "7.25"), _point(4, "9.50")])
    assert [item["rank"] for item in peaks] == [1, 2, 3]
    assert [item["horizonHours"] for item in peaks] == [2, 4, 3]
    assert peaks[0]["energyKwh"] == "9.50"


def test_peak_hours_ignore_unknown_hours_instead_of_treating_them_as_low():
    peaks = rank_peak_hours([_point(1, None), _point(2, "3.00"), _point(3, "")])
    assert [item["horizonHours"] for item in peaks] == [2]


def test_peak_ranking_compares_decimals_not_binary_floats():
    """0.1+0.2 style drift must not reorder equal-looking loads, so comparison stays on Decimal."""
    peaks = rank_peak_hours([_point(1, "8.30"), _point(2, "8.3")])
    assert [item["horizonHours"] for item in peaks] == [1, 2]


def test_training_threshold_uses_only_the_training_split_and_distinct_hours():
    published = training_peak_thresholds(_rows(400))
    entry = published["stations"]["course:1"]
    train, validation, test = chronological_stage2_split(_rows(400))
    distinct_training_hours = {row["target_end"] for row in train}
    assert published["status"] == "OK"
    assert published["quantile"] == 0.90
    assert entry["sampleCount"] == len(distinct_training_hours)
    held_out = max(float(row["target_energy_kwh"]) for row in validation + test)
    assert float(entry["thresholdEnergyKwh"]) <= max(float(row["target_energy_kwh"]) for row in train)
    assert held_out >= 0


def test_training_threshold_is_withheld_when_there_are_too_few_hours():
    published = training_peak_thresholds(_rows(60), minimum_samples=999)
    assert published["status"] == "UNAVAILABLE"
    assert published["stations"]["course:1"]["reason"] == "TOO_FEW_TRAINING_HOURS"
    assert published["stations"]["course:1"]["thresholdEnergyKwh"] is None


def test_training_threshold_reports_unavailable_instead_of_raising_on_short_history():
    published = training_peak_thresholds(_rows(5))
    assert published["status"] == "UNAVAILABLE"
    assert published["stations"] == {}
    assert "DATA_INSUFFICIENT" in published["reason"]


def test_peak_precision_counts_hits_against_the_observed_top_hours():
    curves = {
        ("course:1", "2026-03-01T00:00:00Z"): [
            {"horizonHours": 1, "actual": 9.0, "predicted": 8.0},
            {"horizonHours": 2, "actual": 8.0, "predicted": 9.0},
            {"horizonHours": 3, "actual": 7.0, "predicted": 7.5},
            {"horizonHours": 4, "actual": 1.0, "predicted": 1.5},
        ]
    }
    scored = precision_at_peak(curves)
    assert scored["status"] == "OK"
    assert scored["originsEvaluated"] == 1
    assert scored["peakHits"] == 3
    assert scored["precisionAtTopN"] == 1.0


def test_peak_precision_skips_windows_shorter_than_the_top_n():
    curves = {("course:1", "2026-03-01T00:00:00Z"): [{"horizonHours": 1, "actual": 9.0, "predicted": 8.0}]}
    scored = precision_at_peak(curves)
    assert scored["status"] == "UNAVAILABLE"
    assert scored["reason"] == "NO_TEST_WINDOW_LONG_ENOUGH"
    assert scored["originsEvaluated"] == 0
