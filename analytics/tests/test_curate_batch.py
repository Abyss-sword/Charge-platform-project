# D8：获准公开聚合 raw 清洗后哈希不变，小时电量可回算，首日基线不可用。
from src.curate_batch import curate_public_batch
from src.ingest_raw import file_digest
from src.public_aggregated import default_dataset_path


def test_public_batch_curation_is_immutable_and_recalculable(tmp_path):
    runtime = tmp_path / "runtime"
    first = curate_public_batch(runtime)
    source_digest = file_digest(default_dataset_path())
    raw_csv = runtime / "raw" / "public-leeds-2021q1-001" / "hourly_charge_events.csv"
    assert file_digest(raw_csv) == source_digest == first["sha256"]
    second = curate_public_batch(runtime)
    assert first["quality"] == second["quality"]
    assert first["sha256"] == second["sha256"] == file_digest(raw_csv)
    quality = first["quality"]
    assert quality["input_count"] == quality["accepted_count"] == 6450
    assert quality["rejected_count"] == 0
    assert quality["reconciled"] is True
    assert first["data_mode"] == "PUBLIC_AGGREGATED"
    assert first["curated_rows"] == 6450
    assert first["baseline_unavailable_count"] == 72
    assert first["baseline_available_count"] == 6378
    assert first["recalc"]["load_kwh"]
    assert first["recalc"]["accepted_rows"] == first["recalc"]["order_count"]

# 【文件讲解·答辩用】
# 功能作用：锁住 D8 门禁——6450 行 Leeds 公开聚合全部通过，raw SHA-256 不变，小时负荷和会话数能回算。
# 技术原理：两次 curate_public_batch；ingest 幂等不改副本；attach_yesterday_baseline 前 24 小时×3 站=72 个不可用点。
# 代码思路：先对指纹，再对账，再断言 72/144。不在这里训练树模型。
# 与其他文件关系：实现 curate_batch.py；源 CSV 在 datasets/public_aggregated。夹具测试见 test_fixture_quality.py。
# 答辩讲法：指 SHA256 与源文件相同；6450=6450+0；上游 1011 条会话的 12 条异常在派生前单独审计。
