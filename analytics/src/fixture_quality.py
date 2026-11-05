from __future__ import annotations  # 3.10 延迟注解。

import csv  # 统计夹具非空行，作为质量报告的 input_count。
import json  # 读写期望表、staging/rejected 与质量报告。
from pathlib import Path  # 夹具、映射、runtime 路径。
from typing import Any  # 流水线返回 dict。

from .build_curated import aggregate_hourly, write_curated  # 通过记录聚成小时表。
from .dataset import attach_yesterday_baseline  # 首日基线标不可用。
from .ingest_raw import ingest_source  # 隐私样例走准入，期望整批拒绝。
from .normalize import normalize_csv  # 六类清洗分流。
from .quality import build_quality_report, enforce_min_valid_rate  # 对账与清单阈值。
from .source_registry import AdmissionError, load_manifest  # 夹具清单与准入失败。
from .station_mapping import load_station_mapping  # 只用人工映射表。

ANALYTICS_DIR = Path(__file__).resolve().parents[1]  # analytics 根。
FIXTURE_DIR = ANALYTICS_DIR / "tests" / "fixtures"  # 六类夹具目录。
DEFAULT_MAPPING = ANALYTICS_DIR / "manifests" / "station_mapping.json"  # 含 PUB-STATION-*。


def _non_empty_rows(path: Path) -> int:  # 与 ingest_raw 相同口径：全空行不计。
    with path.open("r", encoding="utf-8-sig", newline="") as stream:  # 允许 Excel BOM。
        reader = csv.reader(stream)  # 拆列。
        next(reader, None)  # 丢掉表头。
        return sum(1 for row in reader if any(cell.strip() for cell in row))  # 非空数据行。


def run_six_class_quality(runtime_root: Path | None = None) -> dict[str, Any]:  # 清洗六类夹具，写出 staging/rejected/curated，不训练模型。
    runtime = runtime_root or ANALYTICS_DIR.parent / "runtime" / "analytics"  # 默认仓库 runtime。
    source_csv = FIXTURE_DIR / "six_class_events.csv"  # 2 合法 + 4 类拒绝。
    manifest_path = FIXTURE_DIR / "source.six-class.json"  # 声明 PUBLIC_AGGREGATED 与 min_valid_rate。
    expected = json.loads((FIXTURE_DIR / "expected.json").read_text(encoding="utf-8"))  # 手工对账表。
    manifest = load_manifest(manifest_path)  # 先过准入字段。
    mapping = load_station_mapping(DEFAULT_MAPPING)  # PUB-UNKNOWN 不在表中。
    input_count = _non_empty_rows(source_csv)  # 应为 6。
    metadata = {  # 清洗血缘，模式不得改成 SIMULATED。
        "ingested_at": str(manifest.values["acquired_at"]),
        "data_mode": manifest.mode,
        "dataset_name": manifest.values["dataset_name"],
        "dataset_version": manifest.values["dataset_version"],
        "ingest_batch": manifest.batch,
    }  # 结束血缘。
    accepted, rejected = normalize_csv(source_csv, mapping, metadata)  # 分流。
    report = build_quality_report(input_count, accepted, rejected)  # 条数守恒。
    enforce_min_valid_rate(report, manifest.values.get("min_valid_rate"))  # 阈值来自清单 0.25。
    wanted = expected["six_class_events.csv"]  # 手工期望。
    if (  # 与夹具期望不一致就失败，防止静默改规则。
        report["input_count"] != wanted["input_count"]
        or report["accepted_count"] != wanted["accepted_count"]
        or report["rejected_count"] != wanted["rejected_count"]
        or report["rejection_reasons"] != wanted["rejection_reasons"]
        or report["duplicate_count"] != wanted["duplicate_count"]
        or report["missing_count"] != wanted["missing_count"]
        or report["unmapped_count"] != wanted["unmapped_count"]
        or report["unit_error_count"] != wanted["unit_error_count"]
        or report["privacy_count"] != wanted["privacy_count"]
    ):
        raise ValueError("FIXTURE_EXPECTATION_MISMATCH")  # 夹具或清洗规则被改坏。
    by_id = {row["source_record_id"]: row for row in accepted}  # 核对规范化字段。
    for record_id, expect in wanted["normalized"].items():  # 两条合法行。
        row = by_id[record_id]  # 必须在通过集。
        for key, value in expect.items():  # station_id / energy / duration / amount / status。
            if row[key] != value:  # 单位换算或映射错了。
                raise ValueError(f"NORMALIZED_EXPECTATION_MISMATCH:{record_id}:{key}")  # 点名字段。
    batch = manifest.batch  # fixture-six-class-001。
    stage_dir = runtime / "staging" / batch  # 通过记录。
    reject_dir = runtime / "rejected" / batch  # 拒绝清单。
    stage_dir.mkdir(parents=True, exist_ok=True)  # 建目录。
    reject_dir.mkdir(parents=True, exist_ok=True)  # 建目录。
    (stage_dir / "accepted.json").write_text(json.dumps(accepted, ensure_ascii=False, indent=2), encoding="utf-8")  # 规范事件。
    (reject_dir / "rejected.json").write_text(json.dumps(rejected, ensure_ascii=False, indent=2), encoding="utf-8")  # 含 reason_code。
    (stage_dir / "quality.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")  # 对账报告。
    curated = aggregate_hourly(accepted, {"1": 4, "2": 3, "3": 2})  # 两行合法事件 → 两个小时桶。
    curated_with_baseline = attach_yesterday_baseline(curated)  # 首日应全部 baseline_available=False。
    if any(item["baseline_available"] for item in curated_with_baseline):  # 夹具没有昨天。
        raise ValueError("FIRST_DAY_BASELINE_SHOULD_BE_UNAVAILABLE")  # 禁止把缺失基线填成 0。
    curated_dir = runtime / "curated" / str(manifest.values["dataset_version"])  # fixture-v1。
    write_curated(curated, curated_dir, len(rejected))  # 小时表 + metadata。
    (curated_dir / "baseline.json").write_text(  # 单独留下可用性，便于答辩展示。
        json.dumps(curated_with_baseline, ensure_ascii=False, indent=2), encoding="utf-8"
    )  # 结束 baseline.json。
    return {  # 给测试与命令行。
        "quality": report,
        "accepted": accepted,
        "rejected": rejected,
        "curated_rows": len(curated),
        "baseline_available_count": sum(1 for item in curated_with_baseline if item["baseline_available"]),
        "data_mode": manifest.mode,
    }  # 结束返回。


def reject_privacy_fixture(runtime_root: Path | None = None) -> str:  # 隐私表头必须在 raw 拷贝前失败。
    runtime = runtime_root or ANALYTICS_DIR.parent / "runtime" / "analytics"  # 默认 runtime。
    try:  # 用六类清单去吃带 phone_number 的 CSV，证明拦的是表头不是清单声明。
        ingest_source(FIXTURE_DIR / "source.six-class.json", FIXTURE_DIR / "privacy_events.csv", runtime)
    except AdmissionError as error:  # 预期 privacy。
        if "privacy" not in str(error).lower():  # 错成别的准入失败不算过。
            raise  # 原样抛。
        return str(error)  # 给测试断言。
    raise AdmissionError("privacy fixture was admitted")  # 居然拷进 raw 了。


def main() -> None:  # 连续跑两遍，打印对账数字；不调用 evaluate，不写模型。
    first = run_six_class_quality()  # 第一遍。
    second = run_six_class_quality()  # 第二遍必须完全一致。
    if first["quality"] != second["quality"] or first["curated_rows"] != second["curated_rows"]:  # 夹具应稳定。
        raise SystemExit("FIXTURE_RUNS_DIFFER")  # 禁止随机或 now() 污染。
    privacy = reject_privacy_fixture()  # 隐私样例不得生成可用 raw。
    report = first["quality"]  # 打印用。
    print("CONTRACT=charge_event_v2")  # 契约未改。
    print(f"DATA_MODE={first['data_mode']}")  # PUBLIC_AGGREGATED。
    print(f"INPUT={report['input_count']}")  # 6。
    print(f"ACCEPTED={report['accepted_count']}")  # 2。
    print(f"REJECTED={report['rejected_count']}")  # 4。
    print(f"DUPLICATE={report['duplicate_count']}")  # 1。
    print(f"MISSING={report['missing_count']}")  # 1。
    print(f"UNMAPPED={report['unmapped_count']}")  # 1。
    print(f"UNIT_ERROR={report['unit_error_count']}")  # 1。
    print(f"PRIVACY_QUALITY={report['privacy_count']}")  # 清洗层 0。
    print(f"PRIVACY_ADMISSION={privacy}")  # 准入层拒绝。
    print(f"CURATED_ROWS={first['curated_rows']}")  # 2。
    print(f"BASELINE_AVAILABLE={first['baseline_available_count']}")  # 0。
    print("D8_RERUN_REQUIRED=yes")  # 周末夹具，D8 在主线正式重跑。
    print("MODEL_TRAINED=no")  # 明确没训练。


if __name__ == "__main__":  # python -m src.fixture_quality
    main()  # 入口。

# 【文件讲解·答辩用】
# 功能作用：把六类质量夹具跑成可对账的 staging/rejected/curated，并证明隐私列在准入就被拦。
# 技术原理：normalize 分流 + quality 守恒 + 昨日基线 None；隐私 CSV 不进 raw。
# 代码思路：对照 expected.json → 写三层目录 → 再跑一遍比 JSON。
# 与其他文件关系：夹具在 tests/fixtures；映射用 manifests/station_mapping.json；不调用 model.evaluate。
# 答辩时可以怎么讲：6 条进、2 条过、4 条拒，原因码各 1；第一天没有基线；手机号表头进不了 raw。
