from __future__ import annotations  # 3.10 延迟注解。

import csv  # 写出教学用模拟充电事件 CSV。
import json  # 读写批次清单、accepted/rejected/quality、评测结果。
import math  # 用正弦波造日内负荷形状。
from datetime import datetime, timedelta, timezone  # 固定从 2026-08-30 东八区开始的小时序列。
from pathlib import Path  # 定位 analytics 根、runtime、manifests。
from typing import Any  # 流水线返回 dict。

from .build_curated import aggregate_hourly, write_curated  # 小时聚合与落盘。
from .ingest_raw import ingest_source  # 清单准入并固化 raw。
from .model import evaluate  # 1/6/24 小时评测。
from .normalize import normalize_csv  # 清洗为 charge_event_v2。
from .quality import build_quality_report  # 条数对账。
from .station_mapping import load_station_mapping  # 来源站 → 内部站。

ANALYTICS_DIR = Path(__file__).resolve().parents[1]  # analytics/ 目录（src 的上一级）。
PROJECT_DIR = ANALYTICS_DIR.parent  # 仓库根，runtime 建在这里。


def generate_demo_csv(path: Path, hours: int = 96) -> int:  # 3 个来源站 × hours 个整点，默认 288 条 COMPLETED 模拟订单。
    path.parent.mkdir(parents=True, exist_ok=True)  # 确保 input 目录存在。
    start = datetime(2026, 8, 30, tzinfo=timezone(timedelta(hours=8)))  # 固定起点，评测可复现。
    fields = ["source_record_id", "source_station_id", "event_time", "timezone", "energy", "energy_unit", "duration", "duration_unit", "amount", "amount_unit", "event_status"]  # 与 normalize 必填列一致。
    stations = ["SRC-STATION-A", "SRC-STATION-B", "SRC-STATION-C"]  # 对应 mapping 里的 1/2/3。
    with path.open("w", encoding="utf-8", newline="") as stream:  # 写模拟源文件。
        writer = csv.DictWriter(stream, fieldnames=fields)  # 按契约列写。
        writer.writeheader()  # 表头。
        count = 0  # 实际写出的数据行。
        for hour_index in range(hours):  # 逐小时。
            stamp = start + timedelta(hours=hour_index)  # 当前整点。
            for station_index, station in enumerate(stations):  # 三站各一条。
                wave = 5.0 + 2.2 * math.sin((stamp.hour - 7) / 24 * 2 * math.pi)  # 早高峰附近抬升的正弦负荷。
                energy_kwh = round(max(1.0, wave + station_index * .7 + (hour_index % 5) * .08), 3)  # 站间差 + 轻微小时抖动，下限 1 kWh。
                writer.writerow({  # 一行来源事件。
                    "source_record_id": f"SIM-{hour_index:03d}-{station_index}", "source_station_id": station,  # 主键含小时和站下标。
                    "event_time": stamp.isoformat(), "timezone": "Asia/Shanghai", "energy": energy_kwh,  # 东八区 ISO 时间与 kWh。
                    "energy_unit": "kwh", "duration": 38 + station_index * 4, "duration_unit": "minutes",  # 分钟时长随站递增。
                    "amount": round(energy_kwh * (1.36 + station_index * .08), 2), "amount_unit": "yuan", "event_status": "COMPLETED"  # 金额随单价，状态完成。
                })
                count += 1  # 计数。
    return count  # 作为 quality 的 input_count。


def run_pipeline(runtime_root: Path | None = None) -> dict[str, Any]:  # 生成 → 摄入 → 清洗 → 质量 → staging → 小时聚合 → 三时域评测；只写模拟 runtime。
    runtime = runtime_root or PROJECT_DIR / "runtime" / "analytics"  # 默认仓库 runtime/analytics。
    generated = runtime / "input" / "simulated-charge-events.csv"  # 生成文件落在 input。
    input_count = generate_demo_csv(generated)  # 写 CSV 并得到行数。
    batch_dir = ingest_source(ANALYTICS_DIR / "manifests" / "source.example.json", generated, runtime)  # 用示例清单准入。
    batch_meta = json.loads((batch_dir / "batch_manifest.json").read_text(encoding="utf-8"))  # 批次指纹与模式。
    source_meta = json.loads((batch_dir / "source_manifest.json").read_text(encoding="utf-8"))  # 来源清单，含 dataset_version。
    metadata = {**batch_meta, "ingested_at": "2026-09-05T09:00:00+08:00"}  # 固定摄入时间，测试快照稳定。
    mapping = load_station_mapping(ANALYTICS_DIR / "manifests" / "station_mapping.json")  # SRC-STATION-* → 1/2/3。
    accepted, rejected = normalize_csv(batch_dir / generated.name, mapping, metadata)  # 清洗固化后的 CSV。
    quality = build_quality_report(input_count, accepted, rejected)  # 对账。
    stage_dir = runtime / "staging" / batch_meta["ingest_batch"]  # 通过记录目录。
    reject_dir = runtime / "rejected" / batch_meta["ingest_batch"]  # 拒绝记录目录。
    stage_dir.mkdir(parents=True, exist_ok=True)  # 建 staging。
    reject_dir.mkdir(parents=True, exist_ok=True)  # 建 rejected。
    (stage_dir / "accepted.json").write_text(json.dumps(accepted, ensure_ascii=False, indent=2), encoding="utf-8")  # 规范事件。
    (reject_dir / "rejected.json").write_text(json.dumps(rejected, ensure_ascii=False, indent=2), encoding="utf-8")  # 拒绝明细。
    (stage_dir / "quality.json").write_text(json.dumps(quality, ensure_ascii=False, indent=2), encoding="utf-8")  # API _quality 读最新这份。
    curated = aggregate_hourly(accepted, {"1": 4, "2": 3, "3": 2})  # 教学可用桩数 4/3/2。
    curated_dir = runtime / "curated" / source_meta["dataset_version"]  # 按数据集版本分目录。
    write_curated(curated, curated_dir, len(rejected))  # hourly_station.csv + metadata.json。
    evaluations = [evaluate(curated, horizon).__dict__ for horizon in (1, 6, 24)]  # 三个时域评测字典。
    results = {"quality": quality, "curated_rows": len(curated), "evaluations": evaluations, "metadata": json.loads((curated_dir / "metadata.json").read_text(encoding="utf-8"))}  # 汇总给 API 判断 trained/baseline。
    (curated_dir / "evaluation.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")  # 模拟评测留在 sim-v2，绝不覆盖 D9 正式模型。
    return results  # 供 main 打印和测试断言。


def main() -> None:  # 跑完整教学流水线。
    result = run_pipeline()  # 跑完整教学流水线。
    print("CONTRACT=charge_event_v2")  # 契约名，脚本可 grep。
    print("DATA_MODE=SIMULATED")  # 声明模拟数据。
    print(f"INPUT_ROWS={result['quality']['input_count']}")  # 输入行。
    print(f"VALID_ROWS={result['quality']['accepted_count']}")  # 通过行。
    print(f"INVALID_ROWS={result['quality']['rejected_count']}")  # 拒绝行。
    print(f"CURATED_ROWS={result['curated_rows']}")  # 小时特征行。
    for item in result["evaluations"]:  # 逐时域打印选用模式。
        print(f"HORIZON_{item['horizon_hours']}H={item['selected_mode'].upper()}")  # TRAINED 或 BASELINE。


if __name__ == "__main__":  # python -m src.demo_pipeline 或直接跑本文件。
    main()  # 进入流水线入口。

# 【文件讲解·答辩用】
# 功能作用：一条命令跑通“造数→准入→清洗→质量→小时表→模型评测”，并打印答辩可读的 KEY=VALUE。
# 技术原理：各阶段写到 runtime 不同目录，raw 只读拷贝；模拟评测只写 runtime/curated/sim-v2/evaluation.json。
# 代码思路：generate_demo_csv 固定种子时间；ingest 用 manifests 示例清单；清洗后对账再聚合；三时域 evaluate。
# 与其他文件关系：串联 ingest_raw、normalize、quality、build_curated、model、station_mapping。D9 正式训练入口是 train.py（public-v1），不再走本文件。
# 答辩时可以怎么讲：这是离线分析链，和大屏只读 SQLite 是两条线；大屏电量看成员1库，这条链证明我们会做可追溯训练数据。
