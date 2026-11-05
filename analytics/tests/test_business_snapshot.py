from __future__ import annotations

import json

import pytest

from src.business_snapshot import build_dynamic_candidates, haversine_km, load_business_snapshot


def _snapshot():
    return [
        {"stationId": 1, "name": "甲站", "latitude": 24.48, "longitude": 118.09, "priceYuanPerKwh": "1.20", "status": "OPEN", "capacityPiles": 4, "idlePiles": 2, "chargingPiles": 1, "faultPiles": 1},
        {"stationId": 2, "name": "乙站", "latitude": 24.50, "longitude": 118.18, "priceYuanPerKwh": "1.35", "status": "CLOSED", "capacityPiles": 4, "idlePiles": 4, "chargingPiles": 0, "faultPiles": 0},
    ]


def test_snapshot_builds_dynamic_candidates_and_excludes_closed(tmp_path):
    path = tmp_path / "business.json"
    path.write_text(json.dumps(_snapshot(), ensure_ascii=False), encoding="utf-8")
    stations = load_business_snapshot(path)
    candidates = build_dynamic_candidates(stations, 24.48, 118.09)
    assert [item.station_id for item in candidates] == [1]
    assert candidates[0].distance_km == pytest.approx(0.0)
    assert candidates[0].current_availability_ratio == "0.50"
    # 预计到达小时的可用比例需要容量标签，缺失时保持 None，不补 0。
    assert candidates[0].predicted_availability_ratio is None
    # 规格禁止虚构等待分钟，候选结构里不应再有这个字段。
    assert not hasattr(candidates[0], "wait_minutes")


def test_snapshot_excludes_stations_whose_piles_are_all_faulted(tmp_path):
    payload = _snapshot()
    payload[0]["idlePiles"] = 0
    payload[0]["chargingPiles"] = 0
    payload[0]["faultPiles"] = 4
    path = tmp_path / "faulted.json"
    path.write_text(json.dumps(payload, ensure_ascii=False), encoding="utf-8")
    candidates = build_dynamic_candidates(load_business_snapshot(path), 24.48, 118.09)
    assert candidates == []


def test_snapshot_rejects_inconsistent_counts(tmp_path):
    payload = _snapshot()
    payload[0]["idlePiles"] = 9
    path = tmp_path / "bad.json"
    path.write_text(json.dumps(payload), encoding="utf-8")
    with pytest.raises(ValueError, match="COUNT_INCONSISTENT"):
        load_business_snapshot(path)


def test_haversine_is_symmetric():
    forward = haversine_km(24.48, 118.09, 24.50, 118.18)
    backward = haversine_km(24.50, 118.18, 24.48, 118.09)
    assert forward == pytest.approx(backward)
    assert forward > 0
