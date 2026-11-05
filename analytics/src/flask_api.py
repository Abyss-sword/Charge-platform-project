"""Formal stage2 Flask API. It reads published artifacts and never writes the business database."""

from __future__ import annotations

import os
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from flask import Flask, jsonify, request

from .business_snapshot import build_dynamic_candidates, load_business_snapshot
from .forecast_service import ForecastPublication
from .recommend import rank_formal_candidates


PROJECT_DIR = Path(__file__).resolve().parents[2]

RECOMMENDATION_CONTRACT = "member3_recommendation_v1"


def business_snapshot_path() -> Path:
    override = os.environ.get("CHARGE_BUSINESS_SNAPSHOT")
    return Path(override) if override else PROJECT_DIR / "runtime" / "analytics" / "business_snapshot.json"


def _integer_argument(name: str, default: int) -> int:
    raw = request.args.get(name)
    if raw is None:
        return default
    try:
        return int(raw)
    except ValueError as error:
        raise ValueError(f"{name.upper()}_MUST_BE_INTEGER") from error


def _float_argument(name: str, default: float) -> float:
    raw = request.args.get(name)
    if raw is None:
        return default
    try:
        return float(raw)
    except ValueError as error:
        raise ValueError(f"{name.upper()}_MUST_BE_NUMBER") from error


def _arrival_time_argument() -> str | None:
    """Accept the planned arrival time so the parameter exists, and reject anything unparseable.

    The spec scores the availability ratio at the arrival hour, which needs capacity labels this
    dataset does not have. Taking the parameter now keeps the client contract stable, but the
    response must state that it did not feed the ranking rather than implying it did.
    """
    raw = request.args.get("arrivalTime")
    if raw is None:
        return None
    text = raw.strip().replace(" ", "T")
    if text.endswith("Z"):
        text = text[:-1] + "+00:00"
    try:
        parsed = datetime.fromisoformat(text)
    except ValueError as error:
        raise ValueError("ARRIVALTIME_MUST_BE_ISO8601") from error
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return parsed.astimezone(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def create_app(feature_dir: Path | None = None, model_dir: Path | None = None) -> Flask:
    app = Flask(__name__)

    def publication() -> ForecastPublication:
        return ForecastPublication(feature_dir=feature_dir, model_dir=model_dir)

    @app.after_request
    def add_contract_headers(response: Any) -> Any:
        response.headers["X-Content-Type-Options"] = "nosniff"
        response.headers["Cache-Control"] = "no-store"
        response.headers["Access-Control-Allow-Origin"] = "*"
        return response

    @app.errorhandler(ValueError)
    def invalid_parameter(error: ValueError) -> tuple[Any, int]:
        return jsonify({"status": "PARAM_INVALID", "reasons": [str(error)]}), 400

    @app.errorhandler(404)
    def route_not_found(error: Any) -> tuple[Any, int]:
        """Keep API failures machine-readable; never leak Flask's HTML error page."""
        return jsonify({"status": "DEPENDENCY_UNAVAILABLE", "degraded": True,
                        "reasons": ["ROUTE_NOT_FOUND"]}), 404

    @app.errorhandler(500)
    def internal_error(error: Any) -> tuple[Any, int]:
        return jsonify({"status": "DEPENDENCY_UNAVAILABLE", "degraded": True,
                        "reasons": ["INTERNAL_ANALYSIS_ERROR"]}), 500

    @app.get("/health")
    def health() -> Any:
        service = publication()
        state = service.health()
        snapshot = business_snapshot_path()
        status = "OK" if (
            state["featuresAvailable"] and state["modelsAvailable"] and state["versionMatches"]
            and state["hiveReportsAvailable"] and state["hiveVersionMatches"]
        ) else "DEPENDENCY_UNAVAILABLE"
        return jsonify({
            "service": "member3-stage2-flask",
            "framework": "Flask",
            "status": status,
            "degraded": status != "OK" or state["capacityStatus"] != "AVAILABLE",
            "reasons": ["NO_CAPACITY_DATA"] if state["capacityStatus"] != "AVAILABLE" else [],
            "artifacts": state,
            "businessSnapshot": {"available": snapshot.exists(), "readOnly": True},
            "routes": ["/health", "/forecast", "/recommend", "/analysis/compare", "/analysis/hive", "/analysis/peak"],
        })

    @app.get("/forecast")
    def forecast() -> Any:
        horizon = _integer_argument("horizon", 24)
        station = request.args.get("stationId")
        payload = publication().forecast(horizon, station)
        status_code = 400 if payload["status"] == "PARAM_INVALID" else 200
        return jsonify(payload), status_code

    @app.get("/analysis/compare")
    def compare() -> Any:
        kind = request.args.get("kind", "station_hour")
        limit = _integer_argument("limit", 1000)
        return jsonify(publication().comparisons(kind, limit))

    @app.get("/analysis/hive")
    def hive_report() -> Any:
        kind = request.args.get("kind", "station_daily_energy")
        limit = _integer_argument("limit", 1000)
        return jsonify(publication().hive_report(kind, limit))

    @app.get("/analysis/peak")
    def peak() -> Any:
        horizon = _integer_argument("horizon", 24)
        station = request.args.get("stationId")
        payload = publication().peak_analysis(horizon, station)
        status_code = 400 if payload["status"] == "PARAM_INVALID" else 200
        return jsonify(payload), status_code

    @app.get("/recommend")
    def recommend() -> Any:
        path = business_snapshot_path()
        if not path.exists():
            return jsonify({
                "contractVersion": RECOMMENDATION_CONTRACT,
                "status": "DEPENDENCY_UNAVAILABLE",
                "degraded": True,
                "reasons": ["BUSINESS_SNAPSHOT_MISSING"],
                "recommendations": [],
            }), 503
        stations = load_business_snapshot(path)
        if not stations:
            return jsonify({
                "contractVersion": RECOMMENDATION_CONTRACT,
                "status": "NO_DATA", "degraded": True,
                "reasons": ["NO_BUSINESS_STATIONS"], "recommendations": [],
            })
        origin_latitude = _float_argument("latitude", stations[0].latitude)
        origin_longitude = _float_argument("longitude", stations[0].longitude)
        arrival_time = _arrival_time_argument()
        limit = max(1, min(_integer_argument("limit", 3), 20))
        candidates = build_dynamic_candidates(stations, origin_latitude, origin_longitude)
        ranked, summary = rank_formal_candidates(candidates, limit=limit)
        service = publication()
        reasons = ["NO_CONFIRMED_ANALYSIS_TO_BUSINESS_STATION_MAPPING", "NO_CAPACITY_DATA"]
        if "predictedAvailability" in summary.get("criteriaExcluded", []):
            reasons.append("PREDICTED_AVAILABILITY_UNAVAILABLE")
        if not ranked:
            reasons.append("NO_CANDIDATE_WITH_ANY_CRITERION")
        return jsonify({
            "contractVersion": RECOMMENDATION_CONTRACT,
            "status": "OK" if ranked else "NO_DATA",
            "degraded": True,
            "reasons": sorted(set(reasons)),
            "datasetVersion": service.feature_manifest.get("datasetVersion"),
            "runId": service.feature_manifest.get("runId"),
            "modelMode": "BASELINE",
            "source": "READ_ONLY_BUSINESS_SNAPSHOT",
            "origin": {
                "latitude": origin_latitude,
                "longitude": origin_longitude,
                "arrivalTime": arrival_time,
                # 参数已收到，但预测项需要容量标签，因此没有参与排序，如实标注。
                "arrivalTimeUsed": False,
                "arrivalTimeReason": "NO_CAPACITY_DATA",
            },
            "scoring": summary,
            "recommendations": ranked,
        })

    return app


app = create_app()


def main() -> None:
    host = os.environ.get("CHARGE_FLASK_HOST", "127.0.0.1")
    port = int(os.environ.get("CHARGE_FLASK_PORT", "8001"))
    app.run(host=host, port=port, debug=False, threaded=True)


if __name__ == "__main__":
    main()
