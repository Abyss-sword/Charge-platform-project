"""Read-only stage2 forecast publication used by the formal Flask API."""

from __future__ import annotations

import json
import os
from datetime import datetime, timedelta, timezone
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path
from typing import Any

import joblib

from .model import PEAK_TOP_N, predict_stage2, rank_peak_hours

# 规格只允许建议值守/调度，不得自动控制真实设备。
PEAK_ADVICE = ("STAFF_ON_SITE", "RESCHEDULE_NON_URGENT_CHARGING")


def runtime_publication_dir() -> Path:
    override = os.environ.get("CHARGE_STAGE2_FEATURE_DIR")
    if override:
        return Path(override)
    return Path(__file__).resolve().parents[2] / "runtime" / "analytics" / "stage2" / "course-v1" / "course-001"


def model_publication_dir() -> Path:
    override = os.environ.get("CHARGE_STAGE2_MODEL_DIR")
    if override:
        return Path(override)
    return Path(__file__).resolve().parents[1] / "models" / "stage2"


def _utc(value: Any) -> datetime:
    text = str(value).replace(" ", "T")
    if text.endswith("Z"):
        text = text[:-1] + "+00:00"
    parsed = datetime.fromisoformat(text)
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return parsed.astimezone(timezone.utc)


def _iso(value: datetime) -> str:
    return value.astimezone(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _two(value: float | Decimal) -> str:
    return format(Decimal(str(value)).quantize(Decimal("0.01"), rounding=ROUND_HALF_UP), "f")


class ForecastPublication:
    def __init__(self, feature_dir: Path | None = None, model_dir: Path | None = None):
        self.feature_dir = feature_dir or runtime_publication_dir()
        self.model_dir = model_dir or model_publication_dir()
        self.feature_manifest = self._read_json(self.feature_dir / "feature-manifest.json", {})
        self.model_manifest = self._read_json(self.model_dir / "stage2-evaluation.json", {})
        self.latest_features = self._read_json(self.feature_dir / "latest-features.json", [])
        self.hive_manifest = self._read_json(self.feature_dir / "hive-report-manifest.json", {})

    @staticmethod
    def _read_json(path: Path, default: Any) -> Any:
        if not path.exists():
            return default
        return json.loads(path.read_text(encoding="utf-8"))

    def health(self) -> dict[str, Any]:
        version_matches = bool(self.feature_manifest) and (
            self.feature_manifest.get("datasetVersion") == self.model_manifest.get("datasetVersion")
            and self.feature_manifest.get("runId") == self.model_manifest.get("runId")
        )
        required_hive_reports = {"station_daily_energy", "hourly_load_rank", "station_coverage"}
        published_hive_reports = set(self.hive_manifest.get("reports", {}))
        hive_reports_available = bool(self.hive_manifest) and required_hive_reports <= published_hive_reports
        hive_version_matches = hive_reports_available and (
            self.hive_manifest.get("datasetVersion") == self.feature_manifest.get("datasetVersion")
            and self.hive_manifest.get("runId") == self.feature_manifest.get("runId")
        )
        return {
            "featuresAvailable": bool(self.feature_manifest and self.latest_features),
            "modelsAvailable": bool(self.model_manifest),
            "versionMatches": version_matches,
            "datasetVersion": self.feature_manifest.get("datasetVersion"),
            "runId": self.feature_manifest.get("runId"),
            "featureGeneratedAt": self.feature_manifest.get("generatedAt"),
            "modelGeneratedAt": self.model_manifest.get("generatedAt"),
            "capacityStatus": "NOT_COLLECTED",
            "hiveReportsAvailable": hive_reports_available,
            "hiveVersionMatches": hive_version_matches,
            "hiveReportGeneratedAt": self.hive_manifest.get("generatedAt"),
        }

    def stations(self) -> list[str]:
        return sorted(str(item["station_id"]) for item in self.latest_features if item.get("station_id"))

    def _evaluation(self, horizon: int) -> dict[str, Any] | None:
        for item in self.model_manifest.get("evaluations", []):
            if int(item.get("horizonHours", 0)) == horizon:
                return item
        return None

    def _feature(self, station_id: str) -> dict[str, Any] | None:
        return next((item for item in self.latest_features if str(item.get("station_id")) == station_id), None)

    @staticmethod
    def _origin(feature: dict[str, Any]) -> datetime:
        """Origin T is the end of the newest observed hour, matching the Spark label convention.

        Spark publishes it as origin_time; falling back to hour_start + 1h keeps the same meaning
        so that horizon h always resolves to [T+h-1, T+h) on both the training and serving side.
        """
        published = feature.get("origin_time") or feature.get("feature_available_at")
        if published:
            return _utc(published)
        return _utc(feature["hour_start"]) + timedelta(hours=1)

    def _artifact(self, horizon: int) -> dict[str, Any] | None:
        filename = self.model_manifest.get("artifacts", {}).get(str(horizon))
        if not filename:
            return None
        root = self.model_dir.resolve()
        path = (root / str(filename)).resolve()
        if path.parent != root or not path.exists():
            return None
        try:
            item = joblib.load(path)
        except Exception:
            return None
        if not isinstance(item, dict) or item.get("trustedProjectArtifact") is not True:
            return None
        if item.get("datasetVersion") != self.feature_manifest.get("datasetVersion") or item.get("runId") != self.feature_manifest.get("runId"):
            return None
        return item

    def forecast(self, horizon: int, station_id: str | None = None) -> dict[str, Any]:
        generated_at = _iso(datetime.now(timezone.utc))
        if horizon not in {1, 6, 24}:
            return self._empty(horizon, station_id or "course:unknown", "PARAM_INVALID", ["HORIZON_MUST_BE_1_6_OR_24"], generated_at)
        if not self.feature_manifest or not self.latest_features:
            return self._empty(horizon, station_id or "course:unknown", "NO_DATA", ["NO_FEATURE_PUBLICATION"], generated_at)
        station = station_id or self.stations()[0]
        feature = self._feature(station)
        if feature is None:
            return self._empty(horizon, station, "NO_DATA", ["ANALYSIS_STATION_NOT_FOUND"], generated_at)
        if not self.model_manifest:
            return self._empty(horizon, station, "MODEL_UNAVAILABLE", ["NO_MODEL_PUBLICATION"], generated_at)
        origin = self._origin(feature)
        points: list[dict[str, Any]] = []
        point_modes: list[str] = []
        reasons = ["NO_CAPACITY_DATA", "SESSION_ALLOCATED_NOT_METERED"]
        for step in range(1, horizon + 1):
            evaluation = self._evaluation(step)
            selected = str((evaluation or {}).get("selectedMode") or "BASELINE")
            caveat = (evaluation or {}).get("selectionCaveat")
            if caveat:
                reasons.append(str(caveat))
            value: float | None = None
            used_mode = "BASELINE"
            if selected == "TRAINED":
                artifact = self._artifact(step)
                if artifact is not None and station in artifact.get("stationCodes", {}):
                    value = predict_stage2(artifact["estimator"], feature, artifact["stationCodes"])
                    used_mode = "TRAINED"
                elif artifact is not None:
                    # 稀疏站点可能在某个 horizon 没有可用监督样本，因此该时域模型没有该站编码。
                    # 这是局部模型覆盖不足，不是请求参数错误；应降级到同一发布版本的基线并显式说明。
                    reasons.append(f"MODEL_STATION_H{step:02d}_UNAVAILABLE")
                else:
                    reasons.append(f"MODEL_ARTIFACT_H{step:02d}_UNAVAILABLE")
            if value is None:
                baseline_name = str((evaluation or {}).get("selectedBaseline") or "recent_3h_mean")
                if baseline_name == "yesterday_same_hour":
                    raw = feature.get(f"baseline_yesterday_h{step:02d}_kwh")
                else:
                    raw = feature.get("rolling_mean_3_kwh")
                if raw in (None, ""):
                    raw = feature.get("rolling_mean_3_kwh") or feature.get("lag1_kwh")
                if raw not in (None, ""):
                    value = max(0.0, float(raw))
            # 与 Spark 标签一致：h 对应 [T+h-1, T+h)，h=1 就是紧邻原点的那一小时，中间不留空档。
            target_start = origin + timedelta(hours=step - 1)
            target_end = target_start + timedelta(hours=1)
            points.append({
                "targetStart": _iso(target_start),
                "targetEnd": _iso(target_end),
                "horizonHours": step,
                "energyKwh": _two(value) if value is not None else None,
                "meanPowerKw": _two(value) if value is not None else None,
                "freePilesExpected": None,
                "nullReason": "NO_CAPACITY_DATA" if value is not None else "DATA_INSUFFICIENT",
            })
            point_modes.append(used_mode if value is not None else "UNAVAILABLE")
        if all(item["energyKwh"] is None for item in points):
            return self._empty(horizon, station, "MODEL_UNAVAILABLE", ["MODEL_AND_BASELINE_UNAVAILABLE"], generated_at, origin)
        model_mode = "TRAINED" if "TRAINED" in point_modes else "BASELINE"
        if model_mode == "BASELINE":
            reasons.append("VALIDATION_SELECTED_BASELINE")
        return {
            "contractVersion": "forecast_v1",
            "originTime": _iso(origin),
            "targetStart": points[0]["targetStart"],
            "targetEnd": points[-1]["targetEnd"],
            "horizonHours": horizon,
            "analysisStationId": station,
            "businessStationId": None,
            "dataMode": self.feature_manifest.get("dataMode", "REAL_ANONYMIZED"),
            "datasetVersion": self.feature_manifest.get("datasetVersion", "unknown"),
            "modelMode": model_mode,
            "status": "OK",
            "degraded": model_mode != "TRAINED" or bool(reasons),
            "reasons": sorted(set(reasons)),
            "points": points,
            "sourceMode": "LIVE_ARTIFACT",
            "generatedAt": generated_at,
        }

    def _empty(
        self,
        horizon: int,
        station: str,
        status: str,
        reasons: list[str],
        generated_at: str,
        origin: datetime | None = None,
    ) -> dict[str, Any]:
        base = origin or datetime.now(timezone.utc).replace(minute=0, second=0, microsecond=0)
        safe_horizon = horizon if 1 <= horizon <= 24 else 1
        return {
            "contractVersion": "forecast_v1",
            "originTime": _iso(base),
            "targetStart": _iso(base),
            "targetEnd": _iso(base + timedelta(hours=safe_horizon)),
            "horizonHours": safe_horizon,
            "analysisStationId": station if ":" in station else f"course:{station}",
            "businessStationId": None,
            "dataMode": self.feature_manifest.get("dataMode", "REAL_ANONYMIZED"),
            "datasetVersion": self.feature_manifest.get("datasetVersion", "unavailable"),
            "modelMode": "UNAVAILABLE",
            "status": status,
            "degraded": True,
            "reasons": reasons,
            "points": [],
            "sourceMode": "LIVE_ARTIFACT",
            "generatedAt": generated_at,
        }

    def _peak_threshold(self, station: str) -> dict[str, Any]:
        published = self.model_manifest.get("peakThresholds")
        if not isinstance(published, dict) or not published.get("stations"):
            return {
                "source": None,
                "quantile": None,
                "thresholdEnergyKwh": None,
                "sampleCount": 0,
                "reason": "PEAK_THRESHOLD_NOT_PUBLISHED",
            }
        entry = published.get("stations", {}).get(station)
        if not isinstance(entry, dict):
            return {
                "source": published.get("source"),
                "quantile": published.get("quantile"),
                "thresholdEnergyKwh": None,
                "sampleCount": 0,
                "reason": "STATION_NOT_IN_TRAINING_SPLIT",
            }
        return {
            "source": published.get("source"),
            "quantile": published.get("quantile"),
            "thresholdEnergyKwh": entry.get("thresholdEnergyKwh"),
            "sampleCount": entry.get("sampleCount", 0),
            "reason": entry.get("reason"),
        }

    @staticmethod
    def _high_load_windows(points: list[dict[str, Any]], threshold: str | None) -> list[dict[str, Any]]:
        """Merge consecutive hours strictly above the high-load line into windows.

        The spec says "above the P90 threshold", so an hour sitting exactly on the line is not a
        high-load hour. A missing hour breaks the run instead of extending it: an unknown hour cannot
        be asserted to be part of a high-load stretch.
        """
        if threshold in (None, ""):
            return []
        limit = Decimal(str(threshold))
        windows: list[dict[str, Any]] = []
        current: dict[str, Any] | None = None
        for item in points:
            value = item.get("energyKwh")
            if value in (None, "") or Decimal(str(value)) <= limit:
                current = None
                continue
            if current is None:
                current = {
                    "start": item["targetStart"],
                    "end": item["targetEnd"],
                    "hours": 1,
                    "peakEnergyKwh": value,
                }
                windows.append(current)
                continue
            current["end"] = item["targetEnd"]
            current["hours"] += 1
            if Decimal(str(value)) > Decimal(str(current["peakEnergyKwh"])):
                current["peakEnergyKwh"] = value
        return windows

    def peak_analysis(self, horizon: int, station_id: str | None = None) -> dict[str, Any]:
        """Report the future peak hours and a relative high-load warning.

        Both halves are fixed by the spec: the Top-N hours of the forecast curve, and the windows
        sitting above the training-split P90 line. A capacity-derived power threshold would take
        precedence, but this dataset has no capacity column, so the warning can only ever say the
        load is high relative to history. It never claims the grid is overloaded and never returns a
        control action.
        """
        forecast = self.forecast(horizon, station_id)
        station = forecast["analysisStationId"]
        threshold = self._peak_threshold(station)
        base = {
            "status": forecast["status"],
            "datasetVersion": forecast["datasetVersion"],
            "runId": self.feature_manifest.get("runId"),
            "analysisStationId": station,
            "originTime": forecast["originTime"],
            "horizonHours": forecast["horizonHours"],
            "modelMode": forecast["modelMode"],
            "topN": PEAK_TOP_N,
            "peakRule": self.model_manifest.get("peakRule"),
            "threshold": threshold,
            "evaluation": self.model_manifest.get("peakEvaluation"),
        }
        if forecast["status"] != "OK":
            return {
                **base,
                "peaks": [],
                "highLoadWindows": [],
                "warning": {
                    "level": "UNKNOWN",
                    "basis": "NONE",
                    "capacityThreshold": {"available": False, "reason": "NO_CAPACITY_DATA"},
                    "advice": [],
                    "automaticControl": False,
                    "reasons": ["NO_FORECAST_CURVE"],
                },
                "reasons": forecast["reasons"],
            }
        peaks = rank_peak_hours(forecast["points"])
        windows = self._high_load_windows(forecast["points"], threshold["thresholdEnergyKwh"])
        reasons = list(forecast["reasons"])
        if threshold["thresholdEnergyKwh"] in (None, ""):
            level, basis = "UNKNOWN", "NONE"
            warning_reasons = [threshold["reason"] or "PEAK_THRESHOLD_UNAVAILABLE", "NO_CAPACITY_DATA"]
        elif windows:
            level, basis = "RELATIVE_HIGH_LOAD", "TRAINING_SET_P90"
            warning_reasons = ["ABOVE_TRAINING_P90", "NO_CAPACITY_DATA"]
        else:
            level, basis = "NONE", "TRAINING_SET_P90"
            warning_reasons = ["BELOW_TRAINING_P90", "NO_CAPACITY_DATA"]
        if not peaks:
            reasons.append("NO_FORECAST_POINT_WITH_VALUE")
        return {
            **base,
            "peaks": peaks,
            "highLoadWindows": windows,
            "warning": {
                "level": level,
                "basis": basis,
                "capacityThreshold": {"available": False, "reason": "NO_CAPACITY_DATA"},
                "advice": list(PEAK_ADVICE) if level == "RELATIVE_HIGH_LOAD" else [],
                "automaticControl": False,
                "reasons": warning_reasons,
            },
            "reasons": sorted(set(reasons)),
        }

    def comparisons(self, kind: str, limit: int = 1000) -> dict[str, Any]:
        if kind not in {"station_hour", "weekday_hour"}:
            raise ValueError("COMPARE_KIND_MUST_BE_station_hour_OR_weekday_hour")
        filename = "station-hour.json" if kind == "station_hour" else "weekday-hour.json"
        rows = self._read_json(self.feature_dir / filename, [])
        return {
            "status": "OK" if rows else "NO_DATA",
            "kind": kind,
            "datasetVersion": self.feature_manifest.get("datasetVersion"),
            "runId": self.feature_manifest.get("runId"),
            "coverage": {
                "observationKind": self.feature_manifest.get("observationKind"),
                "generatedAt": self.feature_manifest.get("generatedAt"),
            },
            "rows": rows[: max(1, min(limit, 5000))],
        }

    def hive_report(self, kind: str, limit: int = 1000) -> dict[str, Any]:
        if kind not in {"station_daily_energy", "hourly_load_rank", "station_coverage"}:
            raise ValueError("HIVE_REPORT_KIND_INVALID")
        report_meta = self.hive_manifest.get("reports", {}).get(kind)
        if not isinstance(report_meta, dict) or not report_meta.get("file"):
            return {
                "status": "DEPENDENCY_UNAVAILABLE", "kind": kind, "source": "HIVE",
                "datasetVersion": self.feature_manifest.get("datasetVersion"),
                "runId": self.feature_manifest.get("runId"), "rows": [],
                "reasons": ["HIVE_REPORT_NOT_PUBLISHED"],
            }
        root = self.feature_dir.resolve()
        path = (root / str(report_meta["file"])).resolve()
        if path.parent != root:
            raise ValueError("HIVE_REPORT_PATH_INVALID")
        payload = self._read_json(path, {})
        version_matches = (
            payload.get("datasetVersion") == self.feature_manifest.get("datasetVersion")
            and payload.get("runId") == self.feature_manifest.get("runId")
        )
        if not version_matches:
            return {
                "status": "DEPENDENCY_UNAVAILABLE", "kind": kind, "source": "HIVE",
                "datasetVersion": self.feature_manifest.get("datasetVersion"),
                "runId": self.feature_manifest.get("runId"), "rows": [],
                "reasons": ["HIVE_REPORT_VERSION_MISMATCH"],
            }
        rows = payload.get("rows", [])
        return {
            "status": "OK" if rows else "NO_DATA", "kind": kind, "source": "HIVE",
            "datasetVersion": payload.get("datasetVersion"), "runId": payload.get("runId"),
            "generatedAt": payload.get("generatedAt"),
            "rowCount": int(report_meta.get("rowCount", len(rows))),
            "rows": rows[: max(1, min(limit, 5000))], "reasons": [],
        }
