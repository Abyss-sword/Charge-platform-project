from __future__ import annotations

import pytest

from bigdata.hive.export_reports import _safe_token, parse_beeline_tsv


def test_parse_beeline_tsv_preserves_decimals_and_converts_counts():
    output = "Connecting to jdbc:hive2://127.0.0.1\n" + (
        "dataset_version\trun_id\tstation_id\tday_utc\tenergy_kwh\tsession_count\tobservation_kind\n"
        "course-v1\tcourse-001\tcourse:369001\t2015-09-30\t12.340000000000\t7\tSESSION_ALLOCATED\n"
    )
    rows = parse_beeline_tsv(
        output,
        ["dataset_version", "run_id", "station_id", "day_utc", "energy_kwh", "session_count", "observation_kind"],
        {"session_count"},
    )
    assert rows == [{
        "dataset_version": "course-v1", "run_id": "course-001", "station_id": "course:369001",
        "day_utc": "2015-09-30", "energy_kwh": "12.340000000000", "session_count": 7,
        "observation_kind": "SESSION_ALLOCATED",
    }]


def test_hive_identifiers_reject_sql_injection_characters():
    assert _safe_token("course-local-v1", "datasetVersion") == "course-local-v1"
    with pytest.raises(ValueError):
        _safe_token("course-v1' OR 1=1", "datasetVersion")
