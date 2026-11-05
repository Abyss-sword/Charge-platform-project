from __future__ import annotations

import argparse
import csv
import hashlib
from collections import defaultdict
from datetime import datetime, timedelta, timezone
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
from pathlib import Path
from typing import Any
from zoneinfo import ZoneInfo

from .source_registry import PRIVACY_FIELDS

# Leeds City Council / Data Mill North 2021 Q1 EV chargepoint usage export.
# The original file contains pseudonymous user and connector identifiers. They
# are read only to aggregate a row and are never copied to the repository data.
UPSTREAM_FIELDS = {
    "Charging event",
    "Unique User ID",
    "CP ID",
    "Connector",
    "Start Date",
    "Start Time",
    "End Date",
    "End Time",
    "Total kWh",
    "Site",
    "Model",
}
FIELDS = [
    "source_record_id",
    "source_station_id",
    "event_time",
    "timezone",
    "energy",
    "energy_unit",
    "duration",
    "duration_unit",
    "amount",
    "amount_unit",
    "event_status",
    "source_event_count",
]
SOURCE_TIMEZONE = ZoneInfo("Europe/London")
SITE_IDS = {
    "Woodhouse Lane Car Park": "LEEDS-WOODHOUSE-LANE",
    "Wellington Place": "LEEDS-WELLINGTON-PLACE",
    "Elland Road Park and Ride": "LEEDS-ELLAND-ROAD",
}


def _parse_local(date_text: str, time_text: str) -> datetime:
    value = datetime.strptime(f"{date_text.strip()} {time_text.strip()}", "%d/%m/%Y %H:%M")
    return value.replace(tzinfo=SOURCE_TIMEZONE)


def _decimal_kwh(value: str) -> Decimal | None:
    text = value.strip()
    if not text:
        return None
    try:
        parsed = Decimal(text)
    except InvalidOperation:
        return None
    if not parsed.is_finite() or parsed < 0:
        return None
    return parsed


def _hour_utc(value: datetime) -> datetime:
    return value.astimezone(timezone.utc).replace(minute=0, second=0, microsecond=0)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def transform_leeds_sessions(source: Path, output: Path) -> dict[str, Any]:
    """Create a PII-free, dense site-hour dataset from the public session CSV.

    Energy is assigned to the start hour because the source has no interval
    meter curve. Missing station-hours are zero-filled because this is a full
    quarterly event export. Amount stays zero: the source publishes no tariff,
    and this field is excluded from load modelling and operational revenue.
    """
    if set(name.lower() for name in FIELDS).intersection(PRIVACY_FIELDS):
        raise ValueError("derived public dataset declares a forbidden privacy field")

    buckets: dict[tuple[str, datetime], dict[str, Any]] = defaultdict(
        lambda: {"energy": Decimal("0"), "duration_seconds": 0, "events": 0}
    )
    source_rows = 0
    accepted_sessions = 0
    incomplete_rows = 0
    invalid_energy_rows = 0
    earliest: datetime | None = None
    latest: datetime | None = None

    with source.open("r", encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        actual_fields = {str(name).strip() for name in (reader.fieldnames or []) if str(name).strip()}
        missing = sorted(UPSTREAM_FIELDS - actual_fields)
        if missing:
            raise ValueError("upstream Leeds CSV is missing fields: " + ", ".join(missing))
        for row in reader:
            if not any(str(value or "").strip() for value in row.values()):
                continue
            source_rows += 1
            site = str(row.get("Site") or "").strip()
            if site not in SITE_IDS:
                raise ValueError(f"unknown Leeds site: {site}")
            required_values = (
                row.get("Start Date"),
                row.get("Start Time"),
                row.get("End Date"),
                row.get("End Time"),
                row.get("Total kWh"),
            )
            if not all(str(value or "").strip() for value in required_values):
                incomplete_rows += 1
                continue
            started = _parse_local(str(row["Start Date"]), str(row["Start Time"]))
            ended = _parse_local(str(row["End Date"]), str(row["End Time"]))
            if ended < started:
                raise ValueError(f"session ends before it starts at source row {source_rows + 1}")
            energy = _decimal_kwh(str(row.get("Total kWh") or ""))
            if energy is None:
                invalid_energy_rows += 1
                continue
            bucket_time = _hour_utc(started)
            bucket = buckets[(SITE_IDS[site], bucket_time)]
            bucket["energy"] += energy
            bucket["duration_seconds"] += int((ended - started).total_seconds())
            bucket["events"] += 1
            accepted_sessions += 1
            earliest = bucket_time if earliest is None else min(earliest, bucket_time)
            latest = bucket_time if latest is None else max(latest, bucket_time)

    if earliest is None or latest is None:
        raise ValueError("upstream Leeds CSV contains no usable sessions")

    output.parent.mkdir(parents=True, exist_ok=True)
    output_rows = 0
    with output.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS)
        writer.writeheader()
        stamp = earliest
        while stamp <= latest:
            local_stamp = stamp.astimezone(SOURCE_TIMEZONE)
            for station in SITE_IDS.values():
                aggregate = buckets.get(
                    (station, stamp),
                    {"energy": Decimal("0"), "duration_seconds": 0, "events": 0},
                )
                event_count = int(aggregate["events"])
                average_duration = (
                    round(int(aggregate["duration_seconds"]) / event_count)
                    if event_count
                    else 0
                )
                energy = Decimal(aggregate["energy"]).quantize(Decimal("0.01"), rounding=ROUND_HALF_UP)
                writer.writerow(
                    {
                        "source_record_id": f"{station}-{stamp:%Y%m%d%H}",
                        "source_station_id": station,
                        "event_time": local_stamp.isoformat(),
                        "timezone": "Europe/London",
                        "energy": format(energy, ".2f"),
                        "energy_unit": "kwh",
                        "duration": average_duration,
                        "duration_unit": "seconds",
                        "amount": "0.00",
                        "amount_unit": "yuan",
                        "event_status": "COMPLETED",
                        "source_event_count": event_count,
                    }
                )
                output_rows += 1
            stamp += timedelta(hours=1)

    return {
        "source_rows": source_rows,
        "accepted_sessions": accepted_sessions,
        "incomplete_rows": incomplete_rows,
        "invalid_energy_rows": invalid_energy_rows,
        "output_rows": output_rows,
        "coverage_start": earliest.astimezone(SOURCE_TIMEZONE).isoformat(),
        "coverage_end": latest.astimezone(SOURCE_TIMEZONE).isoformat(),
        "source_sha256": _sha256(source),
        "output_sha256": _sha256(output),
    }


def default_dataset_path() -> Path:
    return Path(__file__).resolve().parents[1] / "datasets" / "public_aggregated" / "hourly_charge_events.csv"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Derive a privacy-safe station-hour dataset from Leeds City Council EVCP sessions."
    )
    parser.add_argument("--source", required=True, help="Downloaded 2021 EVCP use Q1.csv")
    parser.add_argument("--output", default=str(default_dataset_path()))
    args = parser.parse_args(argv)
    result = transform_leeds_sessions(Path(args.source), Path(args.output))
    for key, value in result.items():
        print(f"{key.upper()}={value}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
