# 连续跑两遍夹具流水线，确认数量与规范化期望稳定。
import json
from pathlib import Path

from src.fixture_quality import reject_privacy_fixture, run_six_class_quality


FIXTURE_DIR = Path(__file__).resolve().parent / "fixtures"


def test_six_class_fixtures_match_hand_counts_twice(tmp_path):
    expected = json.loads((FIXTURE_DIR / "expected.json").read_text(encoding="utf-8"))["six_class_events.csv"]
    first = run_six_class_quality(tmp_path / "runtime")
    second = run_six_class_quality(tmp_path / "runtime")
    assert first["quality"] == second["quality"]
    report = first["quality"]
    assert report["input_count"] == expected["input_count"] == 6
    assert report["accepted_count"] == expected["accepted_count"] == 2
    assert report["rejected_count"] == expected["rejected_count"] == 4
    assert report["input_count"] == report["accepted_count"] + report["rejected_count"]
    assert report["rejection_reasons"] == expected["rejection_reasons"]
    assert report["duplicate_count"] == 1
    assert report["missing_count"] == 1
    assert report["unmapped_count"] == 1
    assert report["unit_error_count"] == 1
    assert report["privacy_count"] == 0
    assert first["data_mode"] == "PUBLIC_AGGREGATED"
    assert first["baseline_available_count"] == 0
    assert {row["source_record_id"] for row in first["accepted"]} == set(expected["accepted_ids"])
    rejected_path = tmp_path / "runtime" / "rejected" / "fixture-six-class-001" / "rejected.json"
    rejected = json.loads(rejected_path.read_text(encoding="utf-8"))
    assert all("reason_code" in item and "source_record_id" in item for item in rejected)


def test_privacy_fixture_is_rejected_before_raw(tmp_path):
    message = reject_privacy_fixture(tmp_path / "runtime")
    assert "privacy" in message.lower()
    raw_csv = tmp_path / "runtime" / "raw" / "fixture-six-class-001" / "privacy_events.csv"
    assert not raw_csv.exists()

# 【文件讲解·答辩用】
# 功能作用：把周末六类夹具的手工账变成自动门禁，连续两次结果必须相同。
# 技术原理：run_six_class_quality 对照 expected.json；隐私样例走 ingest_source，表头命中 PRIVACY_FIELDS。
# 代码思路：先对账条数和原因码，再检查 rejected.json 有记录号；隐私用例断言没有 raw 副本。
# 与其他文件关系：夹具在 tests/fixtures；实现 fixture_quality.py。D8 正式重跑前不要训练模型。
# 答辩讲法：6=2+4，四个拒绝码各一条；手机号进不了 raw。
