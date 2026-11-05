from __future__ import annotations  # 3.10 延迟注解。

import csv  # 写出固定列顺序的 hourly_station.csv。
import json  # 写出 curated 旁路 metadata.json。
from collections import defaultdict  # 按 (站,整点) 归组事件。
from datetime import datetime  # 把 occurred_at 截到小时。
from decimal import Decimal, ROUND_HALF_UP  # 小时电量求和仍用 Decimal。
from pathlib import Path  # curated 输出目录。
from typing import Any  # 规范事件 dict。

FIELDS = ["station_id", "hour_start", "order_count", "load_kwh", "average_duration_seconds", "available_piles", "hour", "weekday", "is_holiday", "data_mode", "dataset_name", "dataset_version", "ingest_batches"]  # CSV 列顺序锁定，训练与 API 都按这个读。


def aggregate_hourly(records: list[dict[str, Any]], available_piles: dict[str, int] | None = None) -> list[dict[str, Any]]:  # 按站点和整点汇总订单数、电量、平均时长；禁止不同 data_mode 混在一次聚合里。
    available_piles = available_piles or {}  # 缺省没有可用桩数字，get 时当 0。
    modes = {row["data_mode"] for row in records}  # 本批出现的数据模式集合。
    if len(modes) > 1:  # 模拟和真实混在一起会污染负荷特征。
        raise ValueError("DATA_MODE_MIXING_REQUIRES_EXPLICIT_SELECTION")  # 必须先筛选再聚合。
    groups: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)  # (station_id, hour_start) → 事件列表。
    for row in records:  # 遍历清洗通过的事件。
        event_time = datetime.fromisoformat(str(row["occurred_at"]))  # 规范 ISO 时间。
        hour_start = event_time.replace(minute=0, second=0, microsecond=0).isoformat()  # 截到整点，保留时区偏移。
        groups[(str(row["station_id"]), hour_start)].append(row)  # 归入对应桶。
    output = []  # 按站、小时排序后的特征行。
    for (station_id, hour_start), rows in sorted(groups.items()):  # 稳定顺序，方便测试快照。
        stamp = datetime.fromisoformat(hour_start)  # 再解析一次，取 hour/weekday。
        output.append({  # 一行 curated 特征。
            "station_id": station_id, "hour_start": hour_start,  # 聚合键。
            "order_count": sum(int(r.get("source_event_count", 1)) for r in rows),  # 聚合源累加原始会话数，普通事件每行计 1。
            "load_kwh": format(sum((Decimal(str(r["energy_kwh"])) for r in rows), Decimal("0")).quantize(Decimal("0.01"), rounding=ROUND_HALF_UP), ".2f"),  # 电量加总两位小数。
            "average_duration_seconds": round(sum(int(r["duration_seconds"]) for r in rows) / len(rows)),  # 平均时长四舍五入到整数秒。
            "available_piles": int(available_piles.get(station_id, 0)),  # 教学演示用的可用桩数，不是实时 SQLite。
            "hour": stamp.hour, "weekday": stamp.weekday(), "is_holiday": 0,  # 小时、星期；节假日教学数据恒为 0。
            "data_mode": rows[0]["data_mode"], "dataset_name": rows[0]["dataset_name"],  # 同组模式已校验一致，取首行。
            "dataset_version": rows[0]["dataset_version"],  # 版本。
            "ingest_batches": ";".join(sorted({str(r["ingest_batch"]) for r in rows}))  # 多批次并入时用分号连接。
        })
    return output  # 给 write_curated 和 model.evaluate。


def write_curated(records: list[dict[str, Any]], output_dir: str | Path, rejected_count: int) -> Path:  # 写 hourly_station.csv 和 metadata.json；空表也写文件，coverage 为 None。
    target = Path(output_dir)  # 通常 runtime/curated/<dataset_version>。
    target.mkdir(parents=True, exist_ok=True)  # 确保目录存在。
    csv_path = target / "hourly_station.csv"  # 训练与 forecast 辅助读取的表。
    with csv_path.open("w", encoding="utf-8", newline="") as stream:  # newline="" 避免 Windows 多空行。
        writer = csv.DictWriter(stream, fieldnames=FIELDS)  # 按 FIELDS 顺序写，多出来的键忽略。
        writer.writeheader()  # 表头行。
        writer.writerows(records)  # 全部小时行。
    metadata = {  # 给 API _metadata 和评测结果引用。
        "row_count": len(records), "rejected_count": rejected_count,  # curated 行数与清洗拒绝条数。
        "coverage_start": records[0]["hour_start"] if records else None,  # 最早整点。
        "coverage_end": records[-1]["hour_start"] if records else None,  # 最晚整点（已按 sort 输出）。
        "data_mode": records[0]["data_mode"] if records else None,  # 空则 None。
        "dataset_name": records[0]["dataset_name"] if records else None,  # 空则 None。
        "dataset_version": records[0]["dataset_version"] if records else None,  # 空则 None。
        "input_batches": sorted({batch for row in records for batch in str(row["ingest_batches"]).split(";") if batch})  # 拆分分号得到批次列表。
    }  # 结束：给 API _metadata 和评测结果引用。
    (target / "metadata.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8")  # 与 CSV 同目录。
    return csv_path  # 返回 CSV 路径供调用方确认。

# 【文件讲解·答辩用】
# 功能作用：把规范事件聚成“站点×小时”特征表，作为树模型输入和离线评测数据。
# 技术原理：整点截断 + Decimal 电量求和；data_mode 混用直接失败；列顺序用 FIELDS 钉死。
# 代码思路：先分组再排序输出；write_curated 同时落 metadata，API 健康检查读这份 JSON。
# 与其他文件关系：输入来自 normalize 的 accepted；被 demo_pipeline 调用；model.evaluate 读 hourly 行。
# 答辩时可以怎么讲：预测的是小时负荷不是单笔订单；大屏实时电量另走 sqlite_dashboard 的电表差分，两套口径要分开讲。
