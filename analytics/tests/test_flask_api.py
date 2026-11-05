from __future__ import annotations

import json
from pathlib import Path

import joblib
from jsonschema import Draft202012Validator

from src.flask_api import create_app


def _publications(tmp_path: Path, selection_caveat: str | None = None, peak_threshold: str | None = None):
    features = tmp_path / "features"
    models = tmp_path / "models"
    features.mkdir(); models.mkdir()
    manifest = {
        "contractVersion": "member3_features_v1",
        "datasetVersion": "course-v1",
        "runId": "course-001",
        "dataMode": "REAL_ANONYMIZED",
        "observationKind": "SESSION_ALLOCATED",
        "generatedAt": "2026-09-15T00:00:00Z",
        "availableDimensionCount": 12,
    }
    latest = [{
        # 原点是最后一个已观测小时 [23:00, 00:00) 的结束时刻，与 Spark 发布的 origin_time 一致。
        "station_id": "course:369001", "hour_start": "2015-09-30T23:00:00Z",
        "origin_time": "2015-10-01T00:00:00Z", "feature_available_at": "2015-10-01T00:00:00Z",
        "hour_of_day": 0, "weekday_monday_0": 3, "is_weekend": False, "month": 10,
        "day_of_month": 1, "week_of_year": 40, "session_count": 1, "coverage_ratio": 0.5,
        "lag1_kwh": 4.0, "lag24_kwh": 5.0, "rolling_mean_3_kwh": 6.0,
        "rolling_mean_24_kwh": 5.5,
        **{f"baseline_yesterday_h{horizon:02d}_kwh": 5.0 + horizon / 10 for horizon in range(1, 25)},
    }]
    evaluations = [{
        "horizonHours": horizon, "selectedMode": "BASELINE", "selectedBaseline": "yesterday_same_hour",
        "selectionCaveat": selection_caveat, "baselineComparable": selection_caveat is None,
        "validation": {"modelMae": 2.0, "baselineMae": 1.0},
        "test": {"modelMae": 2.1, "baselineMae": 1.1},
    } for horizon in range(1, 25)]
    model_manifest = {
        "contractVersion": "member3_model_v1", "datasetVersion": "course-v1", "runId": "course-001",
        "generatedAt": "2026-09-15T00:10:00Z", "evaluations": evaluations, "artifacts": {},
    }
    if peak_threshold is not None:
        model_manifest["peakRule"] = "top 3 by load then time; windows above the training-split P90 line"
        model_manifest["peakThresholds"] = {
            "status": "OK", "quantile": 0.90, "minimumSamples": 20,
            "source": "TRAINING_SPLIT_DISTINCT_TARGET_HOURS",
            "stations": {"course:369001": {
                "thresholdEnergyKwh": peak_threshold, "sampleCount": 240, "reason": None,
            }},
        }
        model_manifest["peakEvaluation"] = {
            "status": "OK", "reason": None, "topN": 3,
            "originsEvaluated": 40, "peakHits": 96, "precisionAtTopN": 0.8,
        }
    (features / "feature-manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    (features / "latest-features.json").write_text(json.dumps(latest), encoding="utf-8")
    (features / "station-hour.json").write_text(json.dumps([{"station_id": "course:369001", "hour_of_day": 8, "avg_energy_kwh": 5.2}]), encoding="utf-8")
    (features / "weekday-hour.json").write_text(json.dumps([{"weekday_monday_0": 1, "hour_of_day": 8, "avg_energy_kwh": 5.1}]), encoding="utf-8")
    hive_reports = {
        "station_daily_energy": [{"station_id": "course:369001", "day_utc": "2015-09-30", "energy_kwh": "5.20"}],
        "hourly_load_rank": [{"hour_utc": "2015-09-30T23:00:00Z", "station_id": "course:369001", "load_rank": 1}],
        "station_coverage": [{"station_id": "course:369001", "observed_hours": 1, "missing_hours": 0}],
    }
    report_index = {}
    for name, rows in hive_reports.items():
        filename = f"hive-{name.replace('_', '-')}.json"
        payload = {
            "contractVersion": "member3_hive_reports_v1", "datasetVersion": "course-v1",
            "runId": "course-001", "source": "HIVE", "generatedAt": "2026-09-15T00:05:00Z",
            "kind": name, "rows": rows,
        }
        (features / filename).write_text(json.dumps(payload), encoding="utf-8")
        report_index[name] = {"file": filename, "rowCount": len(rows)}
    (features / "hive-report-manifest.json").write_text(json.dumps({
        "contractVersion": "member3_hive_reports_v1", "datasetVersion": "course-v1",
        "runId": "course-001", "source": "HIVE", "generatedAt": "2026-09-15T00:05:00Z",
        "reports": report_index,
    }), encoding="utf-8")
    (models / "stage2-evaluation.json").write_text(json.dumps(model_manifest), encoding="utf-8")
    return features, models


def _business_snapshot(tmp_path: Path) -> Path:
    path = tmp_path / "business.json"
    path.write_text(json.dumps([{
        "stationId": 1, "name": "滨海示范站", "latitude": 24.4798, "longitude": 118.0894,
        "priceYuanPerKwh": "1.20", "status": "OPEN", "capacityPiles": 4,
        "idlePiles": 2, "chargingPiles": 1, "faultPiles": 1,
    }], ensure_ascii=False), encoding="utf-8")
    return path


def test_health_forecast_comparison_peak_and_dynamic_recommendation(tmp_path, monkeypatch):
    features, models = _publications(tmp_path)
    monkeypatch.setenv("CHARGE_BUSINESS_SNAPSHOT", str(_business_snapshot(tmp_path)))
    client = create_app(features, models).test_client()

    health = client.get("/health")
    assert health.status_code == 200
    assert health.json["framework"] == "Flask"
    assert health.json["artifacts"]["versionMatches"] is True
    assert health.json["artifacts"]["hiveReportsAvailable"] is True
    assert health.json["artifacts"]["hiveVersionMatches"] is True

    forecast = client.get("/forecast?horizon=6&stationId=course:369001")
    assert forecast.status_code == 200
    assert forecast.json["status"] == "OK"
    assert forecast.json["modelMode"] == "BASELINE"
    assert len(forecast.json["points"]) == 6
    # h=1 必须紧贴原点，中间不留空档；末点结束正好是 T+horizon。
    assert forecast.json["originTime"] == "2015-10-01T00:00:00Z"
    assert forecast.json["points"][0]["targetStart"] == forecast.json["originTime"]
    assert forecast.json["points"][0]["targetEnd"] == "2015-10-01T01:00:00Z"
    assert forecast.json["targetEnd"] == "2015-10-01T06:00:00Z"
    assert all(point["freePilesExpected"] is None and point["nullReason"] == "NO_CAPACITY_DATA" for point in forecast.json["points"])
    schema = json.loads((Path(__file__).resolve().parents[1] / "contracts" / "forecast_v1.schema.json").read_text(encoding="utf-8"))
    assert list(Draft202012Validator(schema).iter_errors(forecast.json)) == []

    compare = client.get("/analysis/compare?kind=weekday_hour")
    assert compare.status_code == 200 and compare.json["rows"]
    for kind in ("station_daily_energy", "hourly_load_rank", "station_coverage"):
        hive = client.get(f"/analysis/hive?kind={kind}")
        assert hive.status_code == 200
        assert hive.json["status"] == "OK"
        assert hive.json["source"] == "HIVE"
        assert hive.json["datasetVersion"] == "course-v1"
        assert hive.json["runId"] == "course-001"
        assert hive.json["rows"]
    peak = client.get("/analysis/peak?horizon=24&stationId=course:369001")
    assert peak.status_code == 200 and peak.json["peaks"]
    # 夹具基线是 5.0 + h/10，所以最高的三个小时必然是 h=24/23/22，且排名连续。
    assert [item["horizonHours"] for item in peak.json["peaks"]] == [24, 23, 22]
    assert [item["rank"] for item in peak.json["peaks"]] == [1, 2, 3]
    recommendation = client.get("/recommend?latitude=24.48&longitude=118.09")
    assert recommendation.status_code == 200
    assert recommendation.json["contractVersion"] == "member3_recommendation_v1"
    assert recommendation.json["source"] == "READ_ONLY_BUSINESS_SNAPSHOT"
    top = recommendation.json["recommendations"][0]
    assert top["name"] == "滨海示范站"
    assert top["rank"] == 1
    assert top["currentAvailabilityRatio"] == "0.50"
    assert top["predictedAvailabilityRatio"] is None
    assert "waitMinutes" not in top and "wait_minutes" not in top
    assert recommendation.json["scoring"]["criteriaExcluded"] == ["predictedAvailability"]
    assert "PREDICTED_AVAILABILITY_UNAVAILABLE" in recommendation.json["reasons"]


def test_unknown_route_is_json_not_flask_html(tmp_path):
    features, models = _publications(tmp_path)
    response = create_app(features, models).test_client().get("/does-not-exist")
    assert response.status_code == 404
    assert response.content_type.startswith("application/json")
    assert response.json["status"] == "DEPENDENCY_UNAVAILABLE"


def test_training_selection_caveat_reaches_the_forecast_reasons(tmp_path):
    """If training could not compare against a baseline, the API must say so instead of looking clean."""
    features, models = _publications(tmp_path, selection_caveat="NO_COMPARABLE_BASELINE")
    client = create_app(features, models).test_client()
    payload = client.get("/forecast?horizon=6&stationId=course:369001").json
    assert "NO_COMPARABLE_BASELINE" in payload["reasons"]
    assert payload["degraded"] is True


def test_trained_model_without_requested_station_falls_back_instead_of_returning_400(tmp_path):
    features, models = _publications(tmp_path)
    manifest_path = models / "stage2-evaluation.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["evaluations"][0]["selectedMode"] = "TRAINED"
    manifest["artifacts"]["1"] = "course_load_h01.joblib"
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
    joblib.dump({
        "trustedProjectArtifact": True,
        "datasetVersion": "course-v1",
        "runId": "course-001",
        "horizonHours": 1,
        "stationCodes": {"course:another-station": 0},
        "estimator": None,
    }, models / "course_load_h01.joblib")

    payload = create_app(features, models).test_client().get(
        "/forecast?horizon=1&stationId=course:369001"
    )
    assert payload.status_code == 200
    assert payload.json["modelMode"] == "BASELINE"
    assert "MODEL_STATION_H01_UNAVAILABLE" in payload.json["reasons"]


def test_peak_reports_high_load_windows_against_the_published_p90_line(tmp_path):
    """Warning must rest on the training P90 line, name its reason, and never claim grid overload."""
    features, models = _publications(tmp_path, peak_threshold="7.00")
    payload = create_app(features, models).test_client().get(
        "/analysis/peak?horizon=24&stationId=course:369001"
    ).json
    assert payload["threshold"]["thresholdEnergyKwh"] == "7.00"
    assert payload["threshold"]["quantile"] == 0.90
    # 夹具第 21—24 小时是 7.10/7.20/7.30/7.40，恰好合并成一个连续窗口。
    assert len(payload["highLoadWindows"]) == 1
    window = payload["highLoadWindows"][0]
    assert window["hours"] == 4
    assert window["start"] == "2015-10-01T20:00:00Z"
    assert window["end"] == "2015-10-02T00:00:00Z"
    assert window["peakEnergyKwh"] == "7.40"
    assert payload["warning"]["level"] == "RELATIVE_HIGH_LOAD"
    assert payload["warning"]["basis"] == "TRAINING_SET_P90"
    assert payload["warning"]["automaticControl"] is False
    assert payload["warning"]["advice"] == ["STAFF_ON_SITE", "RESCHEDULE_NON_URGENT_CHARGING"]
    assert "NO_CAPACITY_DATA" in payload["warning"]["reasons"]
    assert payload["evaluation"]["precisionAtTopN"] == 0.8


def test_peak_warning_stays_unknown_when_no_threshold_was_published(tmp_path):
    """Without a P90 line the endpoint may still rank hours, but it must not invent a warning level."""
    features, models = _publications(tmp_path)
    payload = create_app(features, models).test_client().get(
        "/analysis/peak?horizon=24&stationId=course:369001"
    ).json
    assert payload["peaks"]
    assert payload["highLoadWindows"] == []
    assert payload["warning"]["level"] == "UNKNOWN"
    assert payload["warning"]["advice"] == []
    assert "PEAK_THRESHOLD_NOT_PUBLISHED" in payload["warning"]["reasons"]


def test_peak_windows_break_on_hours_without_a_value(tmp_path):
    """An unknown hour splits a high-load stretch instead of silently bridging it."""
    features, models = _publications(tmp_path, peak_threshold="7.00")
    latest_path = features / "latest-features.json"
    latest = json.loads(latest_path.read_text(encoding="utf-8"))
    latest[0]["baseline_yesterday_h23_kwh"] = None
    latest[0]["rolling_mean_3_kwh"] = None
    latest[0]["lag1_kwh"] = None
    latest_path.write_text(json.dumps(latest), encoding="utf-8")
    payload = create_app(features, models).test_client().get(
        "/analysis/peak?horizon=24&stationId=course:369001"
    ).json
    assert [window["hours"] for window in payload["highLoadWindows"]] == [2, 1]
    assert [item["horizonHours"] for item in payload["peaks"]] == [24, 22, 21]


def test_recommend_takes_arrival_time_but_says_it_did_not_score_with_it(tmp_path, monkeypatch):
    """The parameter exists so clients can enable the control, but claiming it was used would lie."""
    features, models = _publications(tmp_path)
    monkeypatch.setenv("CHARGE_BUSINESS_SNAPSHOT", str(_business_snapshot(tmp_path)))
    client = create_app(features, models).test_client()

    payload = client.get("/recommend?arrivalTime=2015-10-01T08:00:00Z").json
    assert payload["origin"]["arrivalTime"] == "2015-10-01T08:00:00Z"
    assert payload["origin"]["arrivalTimeUsed"] is False
    assert payload["origin"]["arrivalTimeReason"] == "NO_CAPACITY_DATA"

    without = client.get("/recommend").json
    assert without["origin"]["arrivalTime"] is None
    assert client.get("/recommend?arrivalTime=not-a-time").status_code == 400


def test_invalid_horizon_and_compare_kind_return_400(tmp_path):
    features, models = _publications(tmp_path)
    client = create_app(features, models).test_client()
    assert client.get("/forecast?horizon=5").status_code == 400
    assert client.get("/analysis/compare?kind=not-real").status_code == 400
    assert client.get("/analysis/hive?kind=not-real").status_code == 400
    assert client.get("/analysis/peak?horizon=5").status_code == 400
