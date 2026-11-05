"""Read member1's consistent SQLite export and build dynamic recommendation candidates."""

from __future__ import annotations

import json
import math
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from pathlib import Path
from typing import Any

from .recommend import FormalCandidate


@dataclass(frozen=True)
class BusinessStation:
    station_id: int
    name: str
    latitude: float
    longitude: float
    price_yuan_per_kwh: str
    status: str
    capacity_piles: int
    idle_piles: int
    charging_piles: int
    fault_piles: int


def _station_from_json(item: dict[str, Any]) -> BusinessStation:
    required = {
        "stationId", "name", "latitude", "longitude", "priceYuanPerKwh", "status",
        "capacityPiles", "idlePiles", "chargingPiles", "faultPiles",
    }
    missing = sorted(required - set(item))
    if missing:
        raise ValueError(f"BUSINESS_SNAPSHOT_FIELDS_MISSING:{','.join(missing)}")
    try:
        price = Decimal(str(item["priceYuanPerKwh"]))
    except InvalidOperation as error:
        raise ValueError("BUSINESS_SNAPSHOT_PRICE_INVALID") from error
    if price < 0:
        raise ValueError("BUSINESS_SNAPSHOT_PRICE_INVALID")
    station = BusinessStation(
        station_id=int(item["stationId"]),
        name=str(item["name"]),
        latitude=float(item["latitude"]),
        longitude=float(item["longitude"]),
        price_yuan_per_kwh=f"{price:.2f}",
        status=str(item["status"]),
        capacity_piles=int(item["capacityPiles"]),
        idle_piles=int(item["idlePiles"]),
        charging_piles=int(item["chargingPiles"]),
        fault_piles=int(item["faultPiles"]),
    )
    if station.station_id <= 0 or min(station.capacity_piles, station.idle_piles, station.charging_piles, station.fault_piles) < 0:
        raise ValueError("BUSINESS_SNAPSHOT_COUNT_INVALID")
    if station.idle_piles + station.charging_piles + station.fault_piles > station.capacity_piles:
        raise ValueError("BUSINESS_SNAPSHOT_COUNT_INCONSISTENT")
    return station


def load_business_snapshot(path: str | Path) -> list[BusinessStation]:
    payload = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(payload, list):
        raise ValueError("BUSINESS_SNAPSHOT_MUST_BE_ARRAY")
    stations = [_station_from_json(item) for item in payload if isinstance(item, dict)]
    if len(stations) != len(payload):
        raise ValueError("BUSINESS_SNAPSHOT_ITEM_INVALID")
    if len({item.station_id for item in stations}) != len(stations):
        raise ValueError("BUSINESS_SNAPSHOT_DUPLICATE_STATION")
    return stations


def haversine_km(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    radius_km = 6371.0088
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    delta_phi = math.radians(lat2 - lat1)
    delta_lambda = math.radians(lon2 - lon1)
    value = math.sin(delta_phi / 2) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(delta_lambda / 2) ** 2
    return radius_km * 2 * math.atan2(math.sqrt(value), math.sqrt(1 - value))


def build_dynamic_candidates(
    stations: list[BusinessStation], origin_latitude: float, origin_longitude: float
) -> list[FormalCandidate]:
    """Turn the read-only business snapshot into spec-shaped recommendation candidates.

    Only OPEN stations with real capacity are candidates, and a station whose piles are all faulted
    has nothing to offer either. There is deliberately no wait estimate: the spec forbids showing a
    fabricated queue time, and this snapshot carries no queue at all.
    """
    candidates: list[FormalCandidate] = []
    for station in stations:
        if station.status != "OPEN" or station.capacity_piles <= 0:
            continue
        if station.fault_piles >= station.capacity_piles:
            continue
        distance = haversine_km(origin_latitude, origin_longitude, station.latitude, station.longitude)
        ratio = Decimal(station.idle_piles) / Decimal(station.capacity_piles)
        candidates.append(
            FormalCandidate(
                station_id=station.station_id,
                name=station.name,
                distance_km=round(distance, 3),
                price_yuan_per_kwh=station.price_yuan_per_kwh,
                current_availability_ratio=f"{ratio:.2f}",
                # 预计到达小时的可用比例需要容量/占用标签，本数据集没有，按规格保持缺失而不是补 0。
                predicted_availability_ratio=None,
                idle_piles=station.idle_piles,
                capacity_piles=station.capacity_piles,
            )
        )
    return candidates
