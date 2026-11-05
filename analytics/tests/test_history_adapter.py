# T03 历史数据适配器测试：拒绝记账、Decimal 守恒分摊、覆盖并集、跨批去重。
import csv  # 夹具是 CSV，直接按行读再喂给适配器。
from decimal import Decimal  # 分摊断言用精确十进制，不用 float。
from pathlib import Path  # 定位 tests/fixtures/stage2/governance。

import pytest  # 断言异常路径。

from src.history_adapter import (  # 被测模块（先写测试后实现）。
    adapt_csv,
    allocate_energy_cents,
    split_interval_hours,
)

REPO_ROOT = Path(__file__).resolve().parents[2]  # analytics/tests → 仓库根。
GOV = REPO_ROOT / "tests" / "fixtures" / "stage2" / "governance"  # 治理夹具目录。

META = {  # 与 manifest_simulated_v1.json 对齐的批次元数据。
    "source_id": "sim",
    "dataset_version": "fixture-v1",
    "data_mode": "SIMULATED",
    "ingest_batch": "batch-fixture-001",
    "ingested_at": "2026-09-13T00:00:00Z",
}


def _rows(path: Path) -> list[dict[str, str]]:  # 读治理夹具 CSV 为 dict 列表。
    with path.open("r", encoding="utf-8-sig", newline="") as stream:  # 与 ingest 口径一致。
        return list(csv.DictReader(stream))  # 返回全部数据行。


# 合法行必须产出 historical_interval_v1 形态记录：命名空间站点、UTC、空值带原因。
def test_parse_valid_row_produces_contract_record():
    result = adapt_csv(GOV / "intervals_valid.csv", META)  # 整批复用，取第一条。
    assert len(result.accepted) == 3 and not result.rejected  # 合法夹具应全部通过。
    record = result.accepted[0]  # rec-001。
    assert record["analysis_station_id"] == "sim:station-a"  # 命名空间站点。
    assert record["record_id"] == "rec-001"  # 来源主键保留。
    assert record["energy_kwh"] == "7.30"  # 保留两位来源精度，不提前舍入。
    assert record["observation_kind"] == "METER_INTERVAL"  # 观测类型透传。
    assert record["amount_yuan"] == "23.05"  # 金额两位字符串。
    assert record["interval_start_utc"] == "2026-09-01T09:00:00Z"  # 规范化 UTC。
    second = result.accepted[1]  # rec-002：金额为空。
    assert second["amount_yuan"] is None  # 未知金额必须是 null。
    assert second["null_reasons"]["amount_yuan"] == "NOT_COLLECTED"  # 并说明原因。


# S2-T04：5 条输入（3 合法 + 1 重复 + 1 坏时间）→ 3 通过 2 拒绝；主原因数之和=2。
def test_s2_t04_mixed_batch_rejects_duplicate_and_bad_time():
    result = adapt_csv(GOV / "s2_t04_mixed.csv", META)  # 混合批次。
    assert len(result.accepted) == 3  # 恰好 3 条通过。
    assert len(result.rejected) == 2  # 恰好 2 条拒绝。
    primaries = [r["primary_reason"] for r in result.rejected]  # 每条一个主原因。
    assert sum(1 for p in primaries if p) == 2  # 主原因数之和=2，诊断标签不重复计数。
    assert sorted(primaries) == ["DUPLICATE", "INVALID_TIMESTAMP"]  # 主原因对应重复与坏时间。
    duplicate = next(r for r in result.rejected if r["primary_reason"] == "DUPLICATE")  # 重复行。
    assert duplicate["record_id"] == "m-003"  # 重复的是第二条 m-003。


# 负电量必须拒绝且不改写成 0。
def test_negative_energy_rejected():
    result = adapt_csv(GOV / "intervals_negative_energy.csv", META)  # 单条负电量。
    assert not result.accepted  # 不得通过。
    assert result.rejected[0]["primary_reason"] == "INVALID_ENERGY"  # 主原因明确。


# S2-T06 前半：10.27 kWh 跨两个等长小时 → 5.14 + 5.13，总量守恒。
def test_decimal_split_two_equal_hours_conserves_total():
    hours = split_interval_hours("2026-09-03T09:00:00Z", "2026-09-03T11:00:00Z")  # 两个整点小时。
    allocated = allocate_energy_cents("10.27", [(h, Decimal(3600)) for h in hours])  # 等长覆盖。
    values = [(hour, text) for hour, text in allocated]  # [(hour, "x.xx")]。
    assert values[0][1] == "5.14" and values[1][1] == "5.13"  # 尾差给最早并列最大片段。
    assert Decimal(values[0][1]) + Decimal(values[1][1]) == Decimal("10.27")  # 分摊总量守恒。


# S2-T06 后半：金额 10.275 元 HALF_UP 为 10.28，账务规则不受数据管线影响。
def test_amount_rounding_half_up():
    result = adapt_csv(GOV / "intervals_decimal_split.csv", META)  # 含 r-001 金额 10.275。
    by_id = {r["record_id"]: r for r in result.accepted}  # 按主键索引。
    assert by_id["r-001"]["amount_yuan"] == "10.28"  # HALF_UP。


# 会话分摊行进入小时片段：电量未舍入累加、会话开始只在起始小时计一次。
def test_fragments_energy_and_session_start_allocation():
    result = adapt_csv(GOV / "intervals_decimal_split.csv", META)  # d-001 会话 09:00-11:00。
    fragments = result.fragments  # (station, hour) → 聚合。
    station_hour_09 = fragments[("sim:station-a", "2026-09-03T09:00:00Z")]  # 起始小时。
    station_hour_10 = fragments[("sim:station-a", "2026-09-03T10:00:00Z")]  # 后续小时。
    assert station_hour_09["session_starts"] == 1  # 只在起始小时计一次。
    assert station_hour_10["session_starts"] == 0  # 跨小时不重复计数。
    assert station_hour_09["energy"] + station_hour_10["energy"] == Decimal("10.27")  # 未舍入守恒。


# 覆盖秒数取每站区间并集：同站同小时两条重叠 30 分钟区间，覆盖是 1800 而非 3600。
def test_coverage_is_union_not_sum():
    csv_path = GOV / "_overlap_case.csv"  # 动态写一条重叠夹具。
    csv_path.write_text(
        "source_record_id,source_station_id,interval_start_utc,interval_end_utc,energy,energy_unit,"
        "amount,amount_unit,observation_kind,source_timezone,session_started,pile_id,capacity_piles\n"
        "o-001,station-a,2026-09-06T09:00:00Z,2026-09-06T09:30:00Z,1.00,kwh,,yuan,METER_INTERVAL,UTC,1,pile-a1,10\n"
        "o-002,station-a,2026-09-06T09:15:00Z,2026-09-06T09:45:00Z,1.00,kwh,,yuan,METER_INTERVAL,UTC,1,pile-a2,10\n",
        encoding="utf-8",
    )  # 两条区间在 09:15-09:30 重叠。
    try:  # 测试结束前清理。
        result = adapt_csv(csv_path, META)  # 适配。
        fragment = result.fragments[("sim:station-a", "2026-09-06T09:00:00Z")]  # 同一小时。
        assert fragment["coverage_seconds"] == 2700  # 并集 45 分钟=2700 秒，不是 3600。
    finally:  # 清理动态文件。
        csv_path.unlink(missing_ok=True)  # 删除临时夹具。


# 跨批次重复：同一 (source_id, record_id) 在第二批次出现 → 拒绝并带 CROSS_BATCH 标签。
def test_cross_batch_duplicate_rejected():
    result = adapt_csv(GOV / "intervals_cross_batch_dup.csv", META, existing_keys={("sim", "rec-001")})  # 预先存在的键。
    dup = [r for r in result.rejected if r["record_id"] == "rec-001"]  # 命中已有键。
    assert dup and dup[0]["primary_reason"] == "DUPLICATE"  # 主原因仍是 DUPLICATE。
    assert "CROSS_BATCH" in dup[0]["diagnostic_tags"]  # 诊断标签区分跨批，不改变拒绝计数。


# 批级隐私阻断：CSV 带隐私列 → 整批拒绝，不允许任何行进入候选集。
def test_privacy_column_blocks_whole_batch():
    with pytest.raises(Exception, match="PRIVACY"):  # 阻断用异常表达。
        adapt_csv(GOV / "intervals_privacy.csv", META)  # 手机号列出现即停。


# 个人标识列：课程真实数据常见的 userId 一旦出现在表头，也必须整批阻断（可重识别标识）。
def test_user_identifier_column_blocks_whole_batch(tmp_path):
    csv_path = tmp_path / "course_userid.csv"  # 临时来源：带 userId 列的真实数据集形态。
    csv_path.write_text(
        "source_record_id,source_station_id,interval_start_utc,interval_end_utc,energy,energy_unit,"
        "observation_kind,source_timezone,userId\n"
        "s-001,station-a,2026-09-06T09:00:00Z,2026-09-06T09:30:00Z,1.00,kwh,"
        "SESSION_ALLOCATED,Asia/Shanghai,35897499\n",
        encoding="utf-8",
    )
    with pytest.raises(Exception, match="PRIVACY"):  # 未裁剪前不得进入 raw/候选。
        adapt_csv(csv_path, META)
