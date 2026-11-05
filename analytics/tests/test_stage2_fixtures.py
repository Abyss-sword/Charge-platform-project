# Stage2 契约夹具统一校验：三个新 schema + 六类响应夹具 + 治理输入夹具（T01）。
# 同一份夹具供 Qt/Web/API 测试复用；这里负责“能按 schema 校验、篡改必须被拒”。
import json  # 夹具与 schema 都是 UTF-8 JSON。
from pathlib import Path  # 夹具在仓库根 tests/fixtures/stage2，不在 analytics 包内。

import pytest  # 参数化六个响应夹具。
from jsonschema import Draft202012Validator  # 与 schema 声明的 $schema 保持同一方言。

REPO_ROOT = Path(__file__).resolve().parents[2]  # analytics/tests/xxx.py → analytics → 仓库根。
CONTRACTS = REPO_ROOT / "analytics" / "contracts"  # 契约 schema 目录。
FIXTURES = REPO_ROOT / "tests" / "fixtures" / "stage2"  # 统一夹具根目录。

FORECAST_STATES = [  # T01 规定的六种响应状态夹具文件名（不含扩展名）。
    "normal", "no_data", "expired", "baseline", "model_failure", "capacity_missing",
]


def _load_json(path: Path) -> dict:  # 小工具：读 JSON 并保证是对象。
    data = json.loads(path.read_text(encoding="utf-8"))  # 统一 encoding，避免 Windows 默认编码差异。
    assert isinstance(data, dict), f"{path} 必须是 JSON 对象"  # 顶层必须是对象，方便 schema 处理。
    return data  # 原样返回。


def _validator(schema_name: str) -> Draft202012Validator:  # 按文件名构造校验器并先自检 schema。
    schema = _load_json(CONTRACTS / f"{schema_name}.schema.json")  # 读 schema。
    Draft202012Validator.check_schema(schema)  # schema 本身必须合法，防止用坏 schema 骗过测试。
    return Draft202012Validator(schema)  # 返回可复用校验器。


# 六种响应夹具都必须完全符合 forecast_v1，包括 status/modelMode/degraded/reasons 的组合约束。
def test_forecast_response_fixtures_match_schema():
    validator = _validator("forecast_v1")  # forecast_v1 校验器。
    for state in FORECAST_STATES:  # 逐个夹具校验。
        fixture = _load_json(FIXTURES / "forecast" / f"{state}.json")  # 读夹具。
        errors = sorted(validator.iter_errors(fixture), key=lambda e: list(e.path))  # 收集全部错误便于定位。
        assert not errors, f"{state} 夹具违反 forecast_v1: {[e.message for e in errors]}"  # 有错即失败。


# 篡改1：删除 datasetVersion 必须被拒——版本是追溯与降级判断的必要字段。
def test_forecast_fixture_missing_dataset_version_rejected():
    validator = _validator("forecast_v1")  # 校验器复用。
    fixture = _load_json(FIXTURES / "forecast" / "normal.json")  # 以正常夹具为底。
    fixture.pop("datasetVersion")  # 模拟下游漏带版本。
    errors = list(validator.iter_errors(fixture))  # 应产生 required 类错误。
    assert any("datasetVersion" in e.message for e in errors), "删除 datasetVersion 未被拒绝"  # 明确断言。


# 篡改2：energyKwh 从两位字符串改成数值类型必须被拒——金额电量一律两位字符串，禁科学计数法/数字。
def test_forecast_fixture_numeric_energy_rejected():
    validator = _validator("forecast_v1")  # 校验器复用。
    fixture = _load_json(FIXTURES / "forecast" / "normal.json")  # 以正常夹具为底。
    assert fixture["points"], "normal 夹具必须有预测点才能测篡改"  # 前置自检。
    fixture["points"][0]["energyKwh"] = 10.27  # 数字类型——契约只允许 "10.27" 字符串。
    errors = list(validator.iter_errors(fixture))  # 类型错误必须出现。
    assert errors, "energyKwh 改为数值未被拒绝"  # 明确断言。


# 历史区间记录：合法夹具过 schema；未知金额字段必须说明原因（null_reasons）。
def test_historical_interval_fixtures_match_schema():
    validator = _validator("historical_interval_v1")  # 区间记录校验器。
    samples = FIXTURES / "governance" / "expected_intervals"  # 规范化记录样例目录。
    seen = 0  # 至少要覆盖到一条样例，防止目录搬空后测试空转。
    for path in sorted(samples.glob("*.json")):  # 每个文件是 {"records": [...]}。
        for record in _load_json(path)["records"]:  # 逐条校验。
            errors = list(validator.iter_errors(record))  # 收集错误。
            assert not errors, f"{path.name} {record.get('record_id')} 违反 historical_interval_v1: {[e.message for e in errors]}"  # 有错即失败。
            seen += 1  # 计数。
    assert seen >= 3, "样例至少包含 3 条规范化记录"  # 防空转。


# 小时表：缺失小时必须用 energy=null + observation_kind=MISSING 表达，不得填 0。
def test_station_hour_fixture_missing_hour_is_null_not_zero():
    validator = _validator("station_hour_v1")  # 小时表校验器。
    fixture = _load_json(FIXTURES / "station_hour" / "sample_rows.json")  # 样例行集合。
    rows = fixture["rows"]  # rows 数组，含一个 MISSING 小时。
    missing = [r for r in rows if r["observationKind"] == "MISSING"]  # 找缺失小时行。
    assert missing, "样例必须包含缺失小时行"  # 必须演示缺失表达法。
    for row in missing:  # 缺失行约束。
        assert row["energyKwh"] is None, "缺失小时 energyKwh 必须为 null"  # 不填 0。
        assert row["coverageSeconds"] == 0, "缺失小时覆盖秒数为 0"  # 覆盖可以明确为 0 秒。
        assert row["nullReasons"]["energyKwh"] in {"NOT_COLLECTED", "NOT_APPLICABLE", "ANOMALY"}, "缺失必须说明原因"  # 原因枚举。
    for row in rows:  # 全部行都要过 schema。
        errors = list(validator.iter_errors(row))  # 逐行校验。
        assert not errors, f"小时行违反 station_hour_v1: {[e.message for e in errors]}"  # 有错即失败。


# 治理输入夹具完整性：S2-T04 需要“5条输入=3合法+1重复+1坏时间”，文件必须按此构成。
def test_governance_s2_t04_fixture_composition():
    path = FIXTURES / "governance" / "s2_t04_mixed.csv"  # 混合批次夹具。
    lines = [line for line in path.read_text(encoding="utf-8").strip().splitlines() if line.strip()]  # 非空行。
    assert len(lines) == 6, "表头+5条数据行"  # 1 表头 + 5 数据。
    header = lines[0].split(",")  # 表头列。
    assert "source_record_id" in header and "interval_start_utc" in header, "夹具必须带主键与时间列"  # 关键列存在。
    record_ids = [line.split(",")[header.index("source_record_id")] for line in lines[1:]]  # 主键列。
    assert len(record_ids) == 5 and len(set(record_ids)) == 4, "5条中恰有1条重复主键"  # 3 合法 + 1 重复 + 1 坏时间。


# 隐私夹具：真实脱敏来源清单声明隐私列必须被准入层拒绝（S2-T03 的输入侧证据）。
def test_governance_privacy_fixture_declares_blocked_fields():
    manifest = _load_json(FIXTURES / "governance" / "manifest_privacy_blocked.json")  # 带隐私列声明的清单。
    from src.source_registry import PRIVACY_FIELDS, validate_manifest  # 复用准入层常量与校验。

    declared = {str(v).lower() for v in manifest.get("fields", [])}  # 清单声明字段。
    assert declared & PRIVACY_FIELDS, "隐私夹具必须真的声明了隐私字段"  # 确保夹具有效。
    with pytest.raises(Exception, match="privacy"):  # validate_manifest 必须拒绝（AdmissionError）。
        validate_manifest(manifest)  # 准入闸门执行。
