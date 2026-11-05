"""Export the three required Hive business reports for read-only Flask consumption."""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import subprocess
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


SAFE_TOKEN = re.compile(r"^[A-Za-z0-9._-]+$")
REPORTS: dict[str, dict[str, Any]] = {
    "station_daily_energy": {
        "view": "station_daily_energy_v",
        "order": "day_utc, station_id",
        "columns": [
            "dataset_version", "run_id", "station_id", "day_utc",
            "energy_kwh", "session_count", "observation_kind",
        ],
        "integers": {"session_count"},
    },
    "hourly_load_rank": {
        "view": "hourly_load_rank_v",
        "order": "hour_utc, load_rank, station_id",
        "columns": [
            "dataset_version", "run_id", "hour_utc", "station_id",
            "energy_kwh", "load_rank",
        ],
        "integers": {"load_rank"},
    },
    "station_coverage": {
        "view": "station_coverage_v",
        "order": "station_id",
        "columns": [
            "dataset_version", "run_id", "station_id", "expected_hours",
            "observed_hours", "valid_hours", "missing_hours",
            "valid_hour_ratio", "missing_hour_ratio",
        ],
        "integers": {"expected_hours", "observed_hours", "valid_hours", "missing_hours"},
    },
}


def _safe_token(value: str, label: str) -> str:
    if not SAFE_TOKEN.fullmatch(value):
        raise ValueError(f"{label} must contain only letters, digits, dot, underscore or hyphen")
    return value


def parse_beeline_tsv(text: str, columns: list[str], integer_columns: set[str]) -> list[dict[str, Any]]:
    """Parse a beeline TSV2 result while ignoring connection/log preamble lines."""
    lines = [line.rstrip("\r") for line in text.splitlines() if line.strip()]
    header = "\t".join(columns)
    try:
        start = lines.index(header)
    except ValueError as error:
        raise ValueError(f"Hive output did not contain the expected header: {header}") from error
    rows: list[dict[str, Any]] = []
    reader = csv.DictReader(lines[start:], delimiter="\t")
    for raw in reader:
        if raw.get(columns[0]) in (None, ""):
            continue
        row: dict[str, Any] = {}
        for column in columns:
            value = raw.get(column)
            if value in (None, "NULL", "\\N"):
                row[column] = None
            elif column in integer_columns:
                row[column] = int(value)
            else:
                row[column] = value
        rows.append(row)
    return rows


def run_report(beeline: Path, jdbc_url: str, dataset_version: str, run_id: str, spec: dict[str, Any]) -> list[dict[str, Any]]:
    query = (
        "SET hive.local.time.zone=UTC; USE charge_platform_stage2; "
        f"SELECT {', '.join(spec['columns'])} FROM {spec['view']} "
        f"WHERE dataset_version='{dataset_version}' AND run_id='{run_id}' "
        f"ORDER BY {spec['order']};"
    )
    result = subprocess.run(
        [
            str(beeline), "-u", jdbc_url, "--silent=true", "--showHeader=true",
            "--outputformat=tsv2", "-e", query,
        ],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if result.returncode != 0:
        detail = (result.stderr or result.stdout)[-2000:]
        raise RuntimeError(f"beeline failed with exit code {result.returncode}: {detail}")
    return parse_beeline_tsv(result.stdout, spec["columns"], spec["integers"])


def publish_reports(beeline: Path, jdbc_url: str, dataset_version: str, run_id: str, output_dir: Path) -> dict[str, Any]:
    dataset_version = _safe_token(dataset_version, "datasetVersion")
    run_id = _safe_token(run_id, "runId")
    if not beeline.is_file():
        raise FileNotFoundError(f"beeline was not found: {beeline}")
    generated_at = datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    output_dir.mkdir(parents=True, exist_ok=True)
    manifest: dict[str, Any] = {
        "contractVersion": "member3_hive_reports_v1",
        "datasetVersion": dataset_version,
        "runId": run_id,
        "source": "HIVE",
        "generatedAt": generated_at,
        "reports": {},
    }
    with tempfile.TemporaryDirectory(prefix="hive-report-", dir=output_dir) as temporary:
        staging = Path(temporary)
        for name, spec in REPORTS.items():
            rows = run_report(beeline, jdbc_url, dataset_version, run_id, spec)
            payload = {
                "contractVersion": "member3_hive_reports_v1",
                "datasetVersion": dataset_version,
                "runId": run_id,
                "source": "HIVE",
                "generatedAt": generated_at,
                "kind": name,
                "rows": rows,
            }
            filename = f"hive-{name.replace('_', '-')}.json"
            (staging / filename).write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
            manifest["reports"][name] = {"file": filename, "rowCount": len(rows)}
        (staging / "hive-report-manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        for path in staging.iterdir():
            os.replace(path, output_dir / path.name)
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--beeline", type=Path, required=True)
    parser.add_argument("--jdbc-url", default="jdbc:hive2://127.0.0.1:10000/default")
    parser.add_argument("--dataset-version", required=True)
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    manifest = publish_reports(
        args.beeline.resolve(), args.jdbc_url, args.dataset_version, args.run_id, args.output_dir.resolve()
    )
    print(json.dumps(manifest, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
