from __future__ import annotations

import json
import math
from datetime import datetime, timedelta, timezone
from pathlib import Path

from src.train import train_stage2_models


# 每个时域都要能通过时间切分，而 h=24 的标签清洗会吃掉验证段的前 24 小时，
# 因此原点数必须超过 0.15N - 24 >= 3，否则 h=24 直接 DATA_INSUFFICIENT。
ORIGIN_COUNT = 200


def _publish_training(tmp_path: Path) -> tuple[Path, Path]:
    feature_dir = tmp_path / "features"
    model_dir = tmp_path / "models"
    feature_dir.mkdir()
    start = datetime(2015, 1, 1, tzinfo=timezone.utc)
    lines: list[str] = []
    for index in range(ORIGIN_COUNT):
        origin = start + timedelta(hours=index)
        hour = origin.hour
        observed = 6.0 + 3.0 * math.sin(hour / 24 * 2 * math.pi)
        for horizon in range(1, 25):
            target_start = origin + timedelta(hours=horizon - 1)
            target_hour = target_start.hour
            lines.append(json.dumps({
                "station_id": "course:369001",
                "feature_available_at": origin.isoformat().replace("+00:00", "Z"),
                "origin_time": origin.isoformat().replace("+00:00", "Z"),
                "target_end": (target_start + timedelta(hours=1)).isoformat().replace("+00:00", "Z"),
                "horizon_hours": horizon,
                "target_energy_kwh": round(6.0 + 3.0 * math.sin(target_hour / 24 * 2 * math.pi), 6),
                "hour_of_day": hour,
                "weekday_monday_0": origin.weekday(),
                "is_weekend": origin.weekday() >= 5,
                "month": origin.month,
                "day_of_month": origin.day,
                "week_of_year": int(origin.strftime("%V")),
                "session_count": 2,
                "coverage_ratio": 1.0,
                "lag1_kwh": round(observed, 6),
                "lag24_kwh": round(observed, 6),
                "rolling_mean_3_kwh": round(observed, 6),
                "rolling_mean_24_kwh": 6.0,
                "baseline_yesterday_kwh": round(6.0 + 3.0 * math.sin(target_hour / 24 * 2 * math.pi), 6),
                "baseline_recent3_kwh": round(observed, 6),
            }))
    (feature_dir / "training.jsonl").write_text("\n".join(lines), encoding="utf-8")
    (feature_dir / "feature-manifest.json").write_text(json.dumps({
        "contractVersion": "member3_features_v1",
        "datasetVersion": "course-local-v1",
        "runId": "course-local-test",
        "dataMode": "REAL_ANONYMIZED",
        "observationKind": "SESSION_ALLOCATED",
        "generatedAt": "2026-09-17T00:00:00Z",
    }), encoding="utf-8")
    return feature_dir, model_dir


def test_training_bundle_publishes_peak_threshold_and_precision(tmp_path):
    """The peak contract is produced at training time, so the bundle must carry both halves of it."""
    feature_dir, model_dir = _publish_training(tmp_path)
    bundle = train_stage2_models(feature_dir, model_dir)

    thresholds = bundle["peakThresholds"]
    assert thresholds["status"] == "OK"
    assert thresholds["quantile"] == 0.90
    assert thresholds["source"] == "TRAINING_SPLIT_DISTINCT_TARGET_HOURS"
    station = thresholds["stations"]["course:369001"]
    assert float(station["thresholdEnergyKwh"]) > 0
    assert station["sampleCount"] >= thresholds["minimumSamples"]

    scored = bundle["peakEvaluation"]
    assert scored["status"] == "OK"
    assert scored["topN"] == 3
    assert scored["originsEvaluated"] > 0
    assert 0.0 <= scored["precisionAtTopN"] <= 1.0
    assert scored["peakHits"] <= scored["originsEvaluated"] * 3
    assert "strictly above" in bundle["peakRule"]


def test_training_bundle_stays_json_serialisable_and_publishes_every_horizon(tmp_path):
    feature_dir, model_dir = _publish_training(tmp_path)
    train_stage2_models(feature_dir, model_dir)
    published = json.loads((model_dir / "stage2-evaluation.json").read_text(encoding="utf-8"))
    assert sorted(int(key) for key in published["artifacts"]) == list(range(1, 25))
    assert all((model_dir / name).exists() for name in published["artifacts"].values())
    assert len(published["evaluations"]) == 24
