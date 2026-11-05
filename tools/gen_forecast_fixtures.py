# -*- coding: utf-8 -*-
"""一次性生成 tests/fixtures/stage2/forecast/ 六类响应夹具（T01）。
运行：python tools/gen_forecast_fixtures.py（在仓库根执行；生成后本文件保留备查）。"""
from __future__ import annotations

import json
from datetime import datetime, timedelta, timezone
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "tests" / "fixtures" / "stage2" / "forecast"
ORIGIN = datetime(2026, 9, 13, 0, 0, 0, tzinfo=timezone.utc)
STATION = "acn:caltech-1"
VERSION = "fixture-v1"


def utc(dt: datetime) -> str:
    return dt.strftime("%Y-%m-%dT%H:%M:%SZ")


def dec2(value: Decimal) -> str:
    return format(value.quantize(Decimal("0.01"), rounding=ROUND_HALF_UP), ".2f")


def make_points(with_piles: bool, scale: Decimal = Decimal("1")) -> list[dict]:
    points = []
    for h in range(1, 25):
        start = ORIGIN + timedelta(hours=h)
        # 确定性变化曲线：白天高、夜间低，每点不同，避免同值占位。
        base = Decimal("8") + Decimal("6") * Decimal(__import__("math").sin(h / 3.0)).__abs__()
        energy = (base * scale).quantize(Decimal("0.0001"), rounding=ROUND_HALF_UP)
        point = {
            "targetStart": utc(start),
            "targetEnd": utc(start + timedelta(hours=1)),
            "horizonHours": h,
            "energyKwh": dec2(energy),
            "meanPowerKw": dec2(energy),  # 1 小时区间内平均功率 kW 数值等于该小时电量 kWh。
            "freePilesExpected": (3 + (h % 5)) if with_piles else None,
            "nullReason": None if with_piles else None,
        }
        points.append(point)
    return points


def envelope(model_mode: str, status: str, degraded: bool, reasons: list[str], points: list[dict]) -> dict:
    return {
        "contractVersion": "forecast_v1",
        "originTime": utc(ORIGIN),
        "targetStart": utc(ORIGIN + timedelta(hours=1)),
        "targetEnd": utc(ORIGIN + timedelta(hours=25)),
        "horizonHours": 24,
        "analysisStationId": STATION,
        "businessStationId": None,
        "dataMode": "PUBLIC_AGGREGATED",
        "datasetVersion": VERSION,
        "modelMode": model_mode,
        "status": status,
        "degraded": degraded,
        "reasons": reasons,
        "points": points,
        "sourceMode": "FIXTURE",
        "generatedAt": utc(ORIGIN + timedelta(seconds=30)),
    }


FIXTURES = {
    "normal.json": envelope("TRAINED", "OK", False, [], make_points(True)),
    "baseline.json": envelope(
        "BASELINE", "OK", False,
        ["VALIDATION_SELECTED_BASELINE: 树模型在验证集不优于基线，按发布规则选择基线"],
        make_points(True, Decimal("0.9")),
    ),
    "no_data.json": envelope(
        "UNAVAILABLE", "NO_DATA", True,
        ["NO_HOURLY_DATA: fixture-v1 在 acn:caltech-1 无任何小时表数据，不制造预测值"],
        [],
    ),
    "expired.json": envelope(
        "TRAINED", "EXPIRED", True,
        ["ARTIFACT_EXPIRED: 发布产物生成于 2026-09-10T00:00:00Z，超过 72 小时新鲜度上限，仅允许读旧产物并标注"],
        [],
    ),
    "model_failure.json": envelope(
        "UNAVAILABLE", "MODEL_UNAVAILABLE", True,
        ["MODEL_ARTIFACT_UNREADABLE: model joblib 加载失败，且基线产物缺失，无法给出数值"],
        [],
    ),
    "capacity_missing.json": envelope(
        "BASELINE", "OK", False,
        ["NO_CAPACITY_DATA: 来源无容量/占用标签，空闲桩预测不可用，仅发布负荷曲线"],
        make_points(False, Decimal("1.1")),
    ),
}

OUT.mkdir(parents=True, exist_ok=True)
for name, payload in FIXTURES.items():
    (OUT / name).write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("WROTE", OUT / name)
