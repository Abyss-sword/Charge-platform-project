# -*- coding: utf-8 -*-
"""课程真实充电会话数据适配器（T03，成员1）。

来源事实（2026-09-14 实测，文件 md5=d0d5d4f49bbde206886c586fcb6d7c75）：
  列：sessionId,kwhTotal,totalFees,created,ended,startTime,endTime,chargeTimeHrs,
      weekday,platform,userId,stationId,locationId,managerVehicle,facilityType,facilityTypeName
  3340 行、105 站、84 用户；UTF-8 无 BOM；sessionId 唯一；created<ended 全部自洽。
  时间年份被脱敏减 2000（0014/0015），星期几字段与真实日历一致（0014-11-18 标 Tue，
  真实 2014-11-18 亦为周二），据此还原为 2014/2015。

映射到历史区间契约（historical_interval_v1 的 8 个必填来源列）：
  sessionId->source_record_id、stationId->source_station_id、
  created/ended->interval_start_utc/interval_end_utc（+2000 还原、Asia/Shanghai 转 UTC）、
  kwhTotal->energy（十进制文本，保留来源精度）、
  注入常量 energy_unit=kwh、observation_kind=SESSION_ALLOCATED、source_timezone=Asia/Shanghai。

隐私与冗余列一律不写出：userId、managerVehicle、platform、weekday、
totalFees（单位未证实且为二进制浮点，发布层置 null）、startTime/endTime（仅小时，与 created/ended 冗余）。

金额处理（已确认决策）：发布产物不含金额；本模块另写一份仅含聚合统计的金额核对文件
（不落原始行、不落 userId），仅供本地核对，不入仓库、不入共享区。

运行方式：
  python -m src.adapters.course_sessions --input <原始csv> --output <规范csv> \
      [--rejected <jsonl>] [--stats <json>] [--amount-crosscheck <json>] [--no-station-filter]
"""
from __future__ import annotations  # 延迟注解，兼容 3.10。

import argparse  # 命令行入口，非作者可按文档复跑。
import csv  # 读写来源 CSV。
import hashlib  # 记录输入文件指纹，便于追溯。
import json  # 拒绝记账与统计输出。
import sys  # 退出码。
from collections import Counter, defaultdict  # 站点分布与拒绝原因直方图。
from datetime import datetime, timedelta, timezone  # 时间还原与 UTC 归一。
from decimal import Decimal, InvalidOperation  # 金额核对用精确十进制。
from pathlib import Path  # 路径处理。
from typing import Any  # 松散字典类型。
from zoneinfo import ZoneInfo  # Asia/Shanghai 时区换算。

SOURCE_ID = "course"  # 来源命名空间。
DATASET_VERSION = "course-v1"  # 数据版本。
YEAR_OFFSET = 2000  # 年份脱敏偏移：0014 -> 2014。
SOURCE_TZ = "Asia/Shanghai"  # 来源声明时区（原文件无时区列，按国内数据推定并由 manifest 背书）。
ENERGY_UNIT = "kwh"  # 电量单位（原文件无单位列，按列名 kwhTotal 推定并由 manifest 背书）。
OBSERVATION_KIND = "SESSION_ALLOCATED"  # 会话分摊估计，不是逐小时实测。
MIN_DISTINCT_DAYS = 28  # 站点入选门槛：至少 28 个有数据日（00 总规格要求）。
MIN_RECORDS = 50  # 站点入选门槛：至少 50 条会话，避免单条站点污染小时序列。
MAX_DURATION_HOURS = Decimal("24")  # 单次会话时长上限，超过按 ANOMALY 拒绝。

CANONICAL_COLUMNS = (  # 输出规范 CSV 的列（8 必填 + session_started 供小时级会话起始计数）。
    "source_record_id", "source_station_id", "interval_start_utc", "interval_end_utc",
    "energy", "energy_unit", "observation_kind", "source_timezone", "session_started",
)  # 结束：输出列。
TIME_FORMAT = "%Y-%m-%d %H:%M:%S"  # 来源时间格式（无时区，按 SOURCE_TZ 解释）。
OUTPUT_STAMP = "%Y-%m-%dT%H:%M:%SZ"  # 契约要求的 UTC 文本格式。


def file_md5(path: str | Path) -> str:  # 输入指纹：进统计文件，便于核对"是否同一份数据"。
    digest = hashlib.md5()  # 分块读取，避免大文件一次性入内存。
    with open(path, "rb") as stream:  # 二进制读取。
        for chunk in iter(lambda: stream.read(1 << 20), b""):  # 1MiB 分块。
            digest.update(chunk)  # 累积。
    return digest.hexdigest()  # 十六进制摘要。


def restore_local_time(text: str) -> datetime | None:  # 还原被抹年份的本地时间；无法解析返回 None。
    stripped = str(text).strip()  # 去空白。
    if not stripped:  # 空值。
        return None  # 交给调用方按 INVALID_TIMESTAMP 记账。
    try:  # 严格按来源格式解析。
        parsed = datetime.strptime(stripped, TIME_FORMAT)  # 朴素本地时间。
    except ValueError:  # 格式不符。
        return None  # 拒绝。
    if parsed.year < 1000:  # 被减 2000 的脱敏年份（0014/0015）。
        parsed = parsed.replace(year=parsed.year + YEAR_OFFSET)  # 还原为真实年份。
    return parsed  # 返回本地时间。


def to_utc_stamp(local_moment: datetime) -> str:  # 本地时间（SOURCE_TZ）→ 契约 UTC 文本。
    aware = local_moment.replace(tzinfo=ZoneInfo(SOURCE_TZ))  # 贴上来源时区。
    return aware.astimezone(timezone.utc).strftime(OUTPUT_STAMP)  # 转 UTC 并格式化。


def select_stations(rows: list[dict[str, str]]) -> tuple[set[str], dict[str, Any]]:  # 按可复现规则选站。
    days: dict[str, set[str]] = defaultdict(set)  # 站点 → 有数据日集合。
    counts: Counter[str] = Counter()  # 站点 → 会话条数。
    for row in rows:  # 逐行累计（用 created 的日期部分）。
        station = str(row.get("stationId", "")).strip()  # 站点号。
        if not station:  # 缺站点。
            continue  # 该行稍后按 MISSING_REQUIRED 拒绝。
        counts[station] += 1  # 计数。
        days[station].add(str(row.get("created", "")).strip().split(" ")[0])  # 记有数据日。
    selected = sorted(  # 入选站点：同时满足两个门槛，按有数据日降序、站点号升序稳定排序。
        (s for s in counts if len(days[s]) >= MIN_DISTINCT_DAYS and counts[s] >= MIN_RECORDS),
        key=lambda s: (-len(days[s]), s),
    )  # 结束：入选站点。
    excluded_rows = sum(counts[s] for s in counts if s not in set(selected))  # 未入选站点的行数（单独记账，不算拒绝）。
    stats = {  # 选站统计：规则与结果都要进证据，避免"挑数"质疑。
        "rule": f"distinct_days>={MIN_DISTINCT_DAYS} and records>={MIN_RECORDS}; keep all qualifying stations",
        "total_stations": len(counts),  # 来源站点总数。
        "selected_count": len(selected),  # 入选站点数。
        "selected_stations": selected,  # 入选站点列表。
        "excluded_station_count": len(counts) - len(selected),  # 未入选站点数。
        "excluded_row_count": excluded_rows,  # 未入选站点带走的行数（范围外，非数据缺陷）。
    }  # 结束：选站统计。
    return set(selected), stats  # 返回集合与统计。


def convert_row(row: dict[str, str]) -> tuple[dict[str, str] | None, str, list[str]]:  # 单行映射。
    record_id = str(row.get("sessionId", "")).strip()  # 会话主键。
    station = str(row.get("stationId", "")).strip()  # 站点号。
    if not record_id or not station:  # 必填来源列缺失。
        return None, "MISSING_REQUIRED", []  # 拒绝。
    started = restore_local_time(row.get("created", ""))  # 开始时间（还原年份）。
    ended = restore_local_time(row.get("ended", ""))  # 结束时间。
    if started is None or ended is None:  # 时间无法解析。
        return None, "INVALID_TIMESTAMP", []  # 拒绝。
    if ended <= started:  # 区间倒挂或零长。
        return None, "INVALID_INTERVAL", []  # 拒绝。
    energy_text = str(row.get("kwhTotal", "")).strip()  # 电量原文。
    try:  # 十进制解析，拒绝科学计数法与负号。
        energy = Decimal(energy_text)  # 精确十进制。
    except (InvalidOperation, ValueError):  # 非数值。
        return None, "ANOMALY", ["ENERGY_NOT_NUMERIC"]  # 物理量异常。
    if energy < 0:  # 负电量。
        return None, "ANOMALY", ["ENERGY_NEGATIVE"]  # 拒绝。
    duration_text = str(row.get("chargeTimeHrs", "")).strip()  # 时长原文。
    try:  # 时长用于物理合理性校验。
        duration = Decimal(duration_text)  # 精确十进制。
    except (InvalidOperation, ValueError):  # 非数值。
        return None, "ANOMALY", ["DURATION_NOT_NUMERIC"]  # 拒绝。
    if duration > MAX_DURATION_HOURS:  # 超长会话（实测 1 行，55.2h）。
        return None, "ANOMALY", ["DURATION_GT_24H"]  # 拒绝，不静默丢弃。
    if -energy.as_tuple().exponent > 6:  # 超过契约允许的 6 位小数。
        return None, "ANOMALY", ["ENERGY_PRECISION"]  # 拒绝，不悄悄截断。
    record = {  # 规范行（不含任何隐私/冗余列）。
        "source_record_id": record_id,  # 来源主键。
        "source_station_id": station,  # 来源站点号（命名空间在适配层构造）。
        "interval_start_utc": to_utc_stamp(started),  # UTC 开始。
        "interval_end_utc": to_utc_stamp(ended),  # UTC 结束。
        "energy": format(energy, "f"),  # 十进制文本，保留来源精度。
        "energy_unit": ENERGY_UNIT,  # 恒定单位（manifest 背书）。
        "observation_kind": OBSERVATION_KIND,  # 会话分摊估计。
        "source_timezone": SOURCE_TZ,  # 来源时区声明。
        "session_started": "1",  # 每行是一次会话起始，供小时级 session_start_count 计一次。
    }  # 结束：规范行。
    return record, "", []  # 通过，无拒绝原因。


def amount_crosscheck(rows: list[dict[str, str]]) -> dict[str, Any]:  # 金额本地核对（不发布、不含用户标识）。
    values: list[Decimal] = []  # 解析后的金额。
    failed = 0  # 非数值计数。
    for row in rows:  # 逐行解析 totalFees。
        text = str(row.get("totalFees", "")).strip()  # 原文（二进制浮点长尾）。
        try:  # 用 Decimal(str) 规避 float 二次误差。
            values.append(Decimal(text))  # 收集。
        except (InvalidOperation, ValueError):  # 非数值。
            failed += 1  # 计数。
    total = sum(values, Decimal("0")) if values else Decimal("0")  # 精确合计。
    return {  # 仅聚合统计，无逐行金额、无 userId。
        "rows_with_amount": len(values),  # 可解析行数。
        "rows_not_numeric": failed,  # 不可解析行数。
        "sum_raw_decimal": format(total, "f"),  # 原始精度合计（未舍入）。
        "sum_half_up_2dp": format(total.quantize(Decimal("0.01")), "f"),  # 两位 HALF_UP 合计。
        "min": format(min(values), "f") if values else None,  # 最小值。
        "max": format(max(values), "f") if values else None,  # 最大值。
        "unit": "UNVERIFIED",  # 单位未证实，故发布层置 null + NOT_COLLECTED。
        "note": "totalFees 为二进制浮点长尾且无单位列；本文件仅供本地核对，不进入发布产物、仓库或共享区。",
    }  # 结束：金额核对。


def convert(input_path: str | Path, output_csv: str | Path, rejected_jsonl: str | Path | None = None,
            stats_json: str | Path | None = None, amount_json: str | Path | None = None,
            station_filter: bool = True, output_dir: str | Path | None = None) -> dict[str, Any]:
    input_path = Path(input_path)  # 输入。
    output_csv = Path(output_csv)  # 规范 CSV。
    output_csv.parent.mkdir(parents=True, exist_ok=True)  # 建目录。
    with open(input_path, "r", encoding="utf-8-sig", newline="") as stream:  # 兼容 BOM。
        rows = list(csv.DictReader(stream))  # 全量读入（本数据集仅 3340 行）。
    selected: set[str] = set()  # 入选站点。
    selection_stats: dict[str, Any] = {"rule": "disabled", "excluded_row_count": 0,
                                       "selected_count": 0, "selected_stations": []}  # 默认不筛选。
    if station_filter:  # 默认按规则选站。
        selected, selection_stats = select_stations(rows)  # 计算入选集合。
    rejected: list[dict[str, Any]] = []  # 拒绝记账。
    accepted: list[dict[str, str]] = []  # 通过行。
    seen: set[str] = set()  # 批内主键。
    excluded_rows: list[dict[str, str]] = []  # 范围外行（单独记账）。
    for line_number, row in enumerate(rows, start=2):  # 数据从第 2 行起。
        record_id = str(row.get("sessionId", "")).strip()  # 主键。
        if record_id and record_id in seen:  # 批内重复（本数据集实测无）。
            rejected.append({"row_number": line_number, "record_id": record_id,
                             "primary_reason": "DUPLICATE", "diagnostic_tags": []})  # 记账。
            continue  # 不覆盖首条。
        if record_id:  # 登记主键。
            seen.add(record_id)  # 先登记再校验，与 normalize 口径一致。
        record, reason, tags = convert_row(row)  # 单行映射。
        if record is None:  # 数据缺陷优先记账：与站点是否入选无关，避免"过滤掉缺陷站"掩盖问题。
            rejected.append({"row_number": line_number, "record_id": record_id,
                             "primary_reason": reason, "diagnostic_tags": tags})  # 主原因唯一。
            continue  # 下一行。
        station = str(row.get("stationId", "")).strip()  # 站点号（通过行才有意义）。
        if station_filter and station and station not in selected:  # 不在入选站点范围内。
            excluded_rows.append(row)  # 单独收集，不计作数据缺陷。
            continue  # 跳过。
        accepted.append(record)  # 通过。
    with open(output_csv, "w", encoding="utf-8", newline="") as stream:  # 写规范 CSV。
        writer = csv.DictWriter(stream, fieldnames=list(CANONICAL_COLUMNS), lineterminator="\n")  # 固定列序。
        writer.writeheader()  # 表头：历史适配器按表头校验必填列。
        writer.writerows(accepted)  # 数据行。
    if rejected_jsonl:  # 可选：拒绝明细。
        rejected_path = Path(rejected_jsonl)  # 目标。
        rejected_path.parent.mkdir(parents=True, exist_ok=True)  # 建目录。
        with open(rejected_path, "w", encoding="utf-8") as stream:  # JSONL 便于逐行审计。
            for item in rejected:  # 逐条写。
                stream.write(json.dumps(item, ensure_ascii=False) + "\n")  # 一行一条。
    stamps = sorted(r["interval_start_utc"] for r in accepted)  # 通过记录的 UTC 时间。
    reason_counts = Counter(item["primary_reason"] for item in rejected)  # 主原因直方图。
    tag_counts = Counter(tag for item in rejected for tag in item["diagnostic_tags"])  # 诊断标签分布。
    summary = {  # 统计文件：输入=通过+拒绝（范围外单独记），另附规则与推定声明。
        "source_id": SOURCE_ID,  # 命名空间。
        "dataset_version": DATASET_VERSION,  # 版本。
        "input_file": str(input_path),  # 输入路径（本地受控区）。
        "input_md5": file_md5(input_path),  # 输入指纹。
        "input_count": len(rows),  # 输入行数。
        "accepted_count": len(accepted),  # 通过行数。
        "rejected_count": len(rejected),  # 拒绝行数。
        "excluded_count": len(excluded_rows),  # 范围外行数（站点筛选带走，非缺陷）。
        "rejection_reasons": dict(sorted(reason_counts.items())),  # 主原因直方图。
        "diagnostic_tags": dict(sorted(tag_counts.items())),  # 诊断标签（不改变拒绝计数）。
        "station_selection": selection_stats,  # 选站规则与结果。
        "assumptions": {  # 三项推定，必须由 manifest 背书。
            "year_offset": YEAR_OFFSET,  # 年份 +2000。
            "source_timezone": SOURCE_TZ,  # 时区推定。
            "energy_unit": ENERGY_UNIT,  # 单位推定。
            "observation_kind": OBSERVATION_KIND,  # 会话分摊估计。
            "basis": "weekday 字段与真实日历一致（0014-11-18=Tue）证明年份被减 2000；其余为列名/地域推定。",
        },  # 结束：推定声明。
        "coverage_start_utc": stamps[0] if stamps else None,  # 通过记录最早 UTC。
        "coverage_end_utc": stamps[-1] if stamps else None,  # 通过记录最晚 UTC。
        "contract_columns": list(CANONICAL_COLUMNS),  # 输出列（无任何隐私列）。
    }  # 结束：统计。
    if stats_json:  # 可选：统计落盘。
        stats_path = Path(stats_json)  # 目标。
        stats_path.parent.mkdir(parents=True, exist_ok=True)  # 建目录。
        stats_path.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")  # 写 JSON。
    if amount_json:  # 可选：金额核对（仅聚合）。
        amount_path = Path(amount_json)  # 目标。
        amount_path.parent.mkdir(parents=True, exist_ok=True)  # 建目录。
        amount_path.write_text(json.dumps(amount_crosscheck(rows), ensure_ascii=False, indent=2),
                               encoding="utf-8")  # 写 JSON。
    return summary  # 返回统计供调用方断言/打印。


def main(argv: list[str] | None = None) -> int:  # CLI 入口。
    parser = argparse.ArgumentParser(description="课程真实会话数据 → 历史区间规范 CSV（T03）")  # 用途。
    parser.add_argument("--input", required=True, help="原始 CSV（本地受控区）")  # 输入。
    parser.add_argument("--output", required=True, help="规范 CSV 输出路径")  # 输出。
    parser.add_argument("--rejected", default=None, help="拒绝明细 JSONL")  # 可选。
    parser.add_argument("--stats", default=None, help="统计 JSON")  # 可选。
    parser.add_argument("--amount-crosscheck", default=None, help="金额核对 JSON（仅聚合）")  # 可选。
    parser.add_argument("--no-station-filter", action="store_true", help="不做站点筛选（默认按规则筛选）")  # 开关。
    args = parser.parse_args(argv)  # 解析。
    summary = convert(args.input, args.output, args.rejected, args.stats, args.amount_crosscheck,
                      station_filter=not args.no_station_filter)  # 执行。
    print(f"ADAPTER_INPUT={summary['input_count']}")  # 输入。
    print(f"ADAPTER_ACCEPTED={summary['accepted_count']}")  # 通过。
    print(f"ADAPTER_REJECTED={summary['rejected_count']}")  # 拒绝。
    print(f"ADAPTER_EXCLUDED={summary['excluded_count']}")  # 范围外。
    print(f"ADAPTER_STATIONS={summary['station_selection']['selected_count']}")  # 入选站点数。
    print(f"ADAPTER_COVERAGE={summary['coverage_start_utc']}~{summary['coverage_end_utc']}")  # 覆盖区间。
    return 0  # 成功。拒绝行已记账，不改变退出码（批次成败由质量门判定）。


if __name__ == "__main__":  # 直接执行。
    sys.exit(main())  # 退出码透传。

# 【文件讲解·答辩用】
# 功能作用：把教授给的课程真实充电会话 CSV 变成治理链路认识的规范来源 CSV，并留下可审计账目。
#   隐私列（userId 等）在本地受控区直接不写出；年份还原与时区换算写进统计文件；异常会话（55h）按
#   ANOMALY 拒绝并计入"输入=通过+拒绝"；站点筛选规则与入选站点全部落盘，避免"挑数"质疑。
# 技术原理：列映射 + Decimal 精确解析 + ZoneInfo 时区归一；主原因唯一、诊断标签不参与计数；
#   范围外行单独记 excluded，不与数据缺陷混算。
# 与其他文件关系：输出交给 history_adapter.adapt_csv 与 run_governance；manifest 在
#   analytics/sources/course/manifest.json，准入规则在 source_registry.py；小时聚合由 T04 的 Java MR 复算。
# 答辩时可以怎么讲：我们不猜单位也不改精度——金额单位未证实就不发布（null + NOT_COLLECTED）；
#   年份是脱敏减 2000，用星期几一致性反推；会话电量按秒分摊到小时，属于 SESSION_ALLOCATED 估计，
#   图表必须注明，不冒充逐小时实测。
