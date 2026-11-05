from __future__ import annotations

import importlib.util
from pathlib import Path

import pytest


MODULE_PATH = Path(__file__).resolve().parents[1] / "build_features.py"
SPEC = importlib.util.spec_from_file_location("member3_build_features", MODULE_PATH)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def test_dimension_contract_has_ten_or_more_available_dimensions():
    assert len(MODULE.DIMENSIONS) >= 10
    assert MODULE.available_dimension_count() >= 8
    names = {item["name"] for item in MODULE.DIMENSIONS}
    assert {"station_id", "hour_utc", "origin_time", "hour_of_day", "weekday_monday_0", "energy_kwh"} <= names


def test_missing_capacity_and_amount_are_declared_not_collected():
    status = {item["name"]: item["availability"] for item in MODULE.DIMENSIONS}
    assert status["capacity_piles"] == "NOT_COLLECTED"
    assert status["occupied_piles"] == "NOT_COLLECTED"
    assert status["amount_yuan"] == "NOT_COLLECTED"


@pytest.mark.parametrize("horizon,lead,lag", [(1, 1, 23), (6, 6, 18), (24, 24, 0)])
def test_target_and_yesterday_baseline_alignment(horizon, lead, lag):
    """Origin T = end of the feature row hour, so horizon h labels [T+h-1, T+h) = lead h rows."""
    assert MODULE.target_lead_rows(horizon) == lead
    assert MODULE.yesterday_baseline_lag(horizon) == lag


@pytest.mark.parametrize("horizon", [1, 6, 24])
def test_label_and_yesterday_baseline_stay_exactly_24_hours_apart(horizon):
    """lead h targets t0+h; its yesterday counterpart is t0+h-24, so the two offsets differ by 24 rows."""
    assert MODULE.target_lead_rows(horizon) + MODULE.yesterday_baseline_lag(horizon) == 24


def test_invalid_horizon_and_path_identifiers_are_rejected():
    with pytest.raises(ValueError, match="between 1 and 24"):
        MODULE.target_lead_rows(0)
    with pytest.raises(ValueError, match="between 1 and 24"):
        MODULE.yesterday_baseline_lag(25)
    with pytest.raises(ValueError, match="dataset_version"):
        MODULE.validate_version("../../escape", "dataset_version")
