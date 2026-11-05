# -*- coding: utf-8 -*-
# T03 课程真实会话适配器测试：年份/时区还原、隐私列不外流、站点筛选、异常拒绝、金额不发布。
import csv  # 构造与读取临时 CSV。
import json  # 读统计/核对文件。
from pathlib import Path  # 路径处理。

import pytest  # 断言异常与工具函数。

from src.adapters.course_sessions import (  # 被测模块。
    CANONICAL_COLUMNS,
    convert,
    restore_local_time,
    select_stations,
    to_utc_stamp,
)

SOURCE_HEADER = (  # 来源表头（与原文件一致，含隐私列 userId）。
    "sessionId,kwhTotal,totalFees,created,ended,startTime,endTime,chargeTimeHrs,"
    "weekday,platform,userId,stationId,locationId,managerVehicle,facilityType,facilityTypeName\n"
)


def _write_source(tmp_path: Path, body: str) -> Path:
    path = tmp_path / "source.csv"  # 临时来源文件。
    path.write_text(SOURCE_HEADER + body, encoding="utf-8")  # 表头 + 数据行。
    return path  # 返回路径。


def _row(session: str, kwh: str, hours: str, start: str, end: str, user: str = "35897499",
         station: str = "369001") -> str:
    return (f"{session},{kwh},7.002000188827515,{start},{end},15,17,{hours},Tue,android,"
            f"{user},{station},461655,0,3,直流交流一体充电桩\n")  # 与原文件列序一致。


# 年份脱敏还原：0014 -> 2014；真实年份不动。
def test_restore_local_time_adds_missing_century():
    assert restore_local_time("0014-11-18 15:40:26").year == 2014  # 脱敏年份补 2000。
    assert restore_local_time("2015-01-16 08:00:00").year == 2015  # 完整年份保持不变。
    assert restore_local_time("") is None  # 空值不可解析。
    assert restore_local_time("not-a-time") is None  # 非法格式不可解析。


# 时区归一：上海本地 15:40:26 -> UTC 07:40:26（同日），契约格式带 Z。
def test_to_utc_stamp_converts_shanghai_to_utc():
    stamp = to_utc_stamp(restore_local_time("0014-11-18 15:40:26"))  # 本地时间转 UTC。
    assert stamp == "2014-11-18T07:40:26Z"  # +08:00 减 8 小时。


# 站点筛选规则：仅保留 distinct_days>=28 且 records>=50 的站点。
def test_select_stations_applies_documented_rule(tmp_path):
    rows = []  # 构造两站：A 满足 28 天 50 条，B 只有 1 天。
    for day in range(1, 29):  # 28 天。
        for _ in range(2):  # 每天 2 条 -> 56 条。
            rows.append({"stationId": "A", "created": f"0014-11-{day:02d} 10:00:00"})  # A 站。
    rows.append({"stationId": "B", "created": "0014-11-01 10:00:00"})  # B 站单条。
    selected, stats = select_stations(rows)  # 执行规则。
    assert selected == {"A"}  # 只有 A 入选。
    assert stats["selected_count"] == 1 and stats["excluded_row_count"] == 1  # 统计对账。
    assert "distinct_days>=28" in stats["rule"]  # 规则文本可追溯。


# 规范 CSV 不得包含任何隐私或冗余列（userId/managerVehicle/totalFees 等）。
def test_output_has_no_privacy_or_redundant_columns(tmp_path):
    source = _write_source(tmp_path, _row("s-1", "7.78", "1.5", "0014-11-18 15:40:26", "0014-11-18 17:11:04"))  # 单行来源。
    out = tmp_path / "canonical.csv"  # 输出。
    convert(source, out, station_filter=False)  # 不筛选，专注列检查。
    with out.open(encoding="utf-8") as stream:  # 读输出表头。
        header = stream.readline().strip().split(",")  # 列名。
    assert header == list(CANONICAL_COLUMNS)  # 列序与契约一致。
    for banned in ("userId", "managerVehicle", "totalFees", "platform", "weekday", "startTime", "endTime"):  # 禁止列。
        assert banned not in header  # 一个都不能出现。
    assert "user" not in ",".join(header).lower()  # 兜底：任何 user 字样都不允许。


# 通过行映射：常量注入正确，电量为十进制文本，session_started=1，金额列不存在。
def test_accepted_row_mapping_and_constants(tmp_path):
    source = _write_source(tmp_path, _row("s-2", "7.78", "1.5", "0014-11-18 15:40:26", "0014-11-18 17:11:04"))  # 单行。
    out = tmp_path / "canonical.csv"  # 输出。
    summary = convert(source, out, station_filter=False)  # 转换。
    with out.open(encoding="utf-8") as stream:  # 读数据行。
        reader = csv.DictReader(stream)  # 表头驱动。
        record = next(reader)  # 唯一数据行。
    assert record["source_record_id"] == "s-2"  # 主键。
    assert record["source_station_id"] == "369001"  # 站点号。
    assert record["interval_start_utc"] == "2014-11-18T07:40:26Z"  # 时间归一。
    assert record["energy"] == "7.78" and record["energy_unit"] == "kwh"  # 电量与单位。
    assert record["observation_kind"] == "SESSION_ALLOCATED"  # 会话分摊估计。
    assert record["source_timezone"] == "Asia/Shanghai"  # 时区声明。
    assert record["session_started"] == "1"  # 会话起始计一次。
    assert summary["accepted_count"] == 1 and summary["rejected_count"] == 0  # 账目。
    assert summary["assumptions"]["year_offset"] == 2000  # 推定声明。
    assert "amount" not in record  # 金额不发布。


# 异常拒绝：超过 24 小时的会话按 ANOMALY 记账，不静默丢弃。
def test_anomalous_duration_rejected_with_reason(tmp_path):
    source = _write_source(tmp_path, _row("s-3", "7.78", "55.238", "0014-11-18 15:40:26", "0014-11-18 17:11:04"))  # 55h 会话。
    out = tmp_path / "canonical.csv"  # 输出。
    rejected = tmp_path / "rejected.jsonl"  # 拒绝明细。
    summary = convert(source, out, rejected, station_filter=False)  # 转换。
    assert summary["accepted_count"] == 0 and summary["rejected_count"] == 1  # 一行拒绝。
    assert summary["rejection_reasons"] == {"ANOMALY": 1}  # 主原因唯一。
    assert summary["diagnostic_tags"] == {"DURATION_GT_24H": 1}  # 标签说明细节。
    item = json.loads(rejected.read_text(encoding="utf-8").strip())  # 读拒绝行。
    assert item["record_id"] == "s-3" and item["primary_reason"] == "ANOMALY"  # 可追溯。


# 区间倒挂与非数值电量分别拒绝，主原因互不混淆。
def test_invalid_interval_and_energy_rejected(tmp_path):
    body = (_row("s-4", "7.78", "1.0", "0014-11-18 17:00:00", "0014-11-18 15:00:00")  # 结束早于开始。
            + _row("s-5", "abc", "1.0", "0014-11-18 15:00:00", "0014-11-18 16:00:00"))  # 非数值电量。
    source = _write_source(tmp_path, body)  # 来源。
    out = tmp_path / "canonical.csv"  # 输出。
    summary = convert(source, out, station_filter=False)  # 转换。
    assert summary["rejection_reasons"] == {"INVALID_INTERVAL": 1, "ANOMALY": 1}  # 两种主原因。
    assert summary["accepted_count"] == 0  # 全部拒绝。


# 金额核对只产出聚合统计（无逐行金额、无用户标识），且不接受未证实单位。
def test_amount_crosscheck_is_aggregate_only(tmp_path):
    source = _write_source(tmp_path, _row("s-6", "7.78", "1.5", "0014-11-18 15:40:26", "0014-11-18 17:11:04"))  # 单行。
    out = tmp_path / "canonical.csv"  # 输出。
    amount = tmp_path / "amount.json"  # 核对文件。
    convert(source, out, amount_json=amount, station_filter=False)  # 转换。
    data = json.loads(amount.read_text(encoding="utf-8"))  # 读核对结果。
    assert data["unit"] == "UNVERIFIED"  # 单位未证实。
    assert data["rows_with_amount"] == 1  # 聚合计数。
    assert "sum_half_up_2dp" in data and "userId" not in json.dumps(data)  # 只有聚合，无用户标识。


# 隐私列存在时，规范输出仍然不含该列（适配器主动裁剪，而非依赖下游拦截）。
def test_user_id_is_dropped_not_forwarded(tmp_path):
    source = _write_source(tmp_path, _row("s-7", "1.00", "1.0", "0014-11-18 15:00:00", "0014-11-18 16:00:00",
                                          user="98765432"))  # 指定 userId。
    out = tmp_path / "canonical.csv"  # 输出。
    convert(source, out, station_filter=False)  # 转换。
    text = out.read_text(encoding="utf-8")  # 全文。
    assert "98765432" not in text  # 用户标识未外流。


# acn 适配器是预留骨架：调用必须显式失败，避免被误当可用实现。
def test_acn_adapter_is_reserved_placeholder():
    from src.adapters import acn  # 预留模块。
    with pytest.raises(NotImplementedError):  # 明确失败。
        acn.convert("in.csv", "out.csv")  # 任何调用都不应成功。


# 数据缺陷先于站点范围判定：未入选站点里的异常行也必须计为拒绝，不能被过滤掩盖。
def test_out_of_scope_defects_still_rejected(tmp_path):
    rows = []  # 构造两站：A 满足入选规则，B 未入选但含一条 55h 异常会话。
    for day in range(1, 29):  # 28 天。
        for _ in range(2):  # 每天 2 条。
            rows.append(_row(f"ok-{day}-{_}", "1.00", "1.0",
                             f"0014-11-{day:02d} 10:00:00", f"0014-11-{day:02d} 11:00:00", station="A"))
    rows.append(_row("bad-out-of-scope", "5.00", "55.238", "0014-11-18 10:00:00",
                     "0014-11-18 11:00:00", station="B"))  # B 站异常行。
    source = _write_source(tmp_path, "".join(rows))  # 写入来源。
    out = tmp_path / "canonical.csv"  # 输出。
    summary = convert(source, out, station_filter=True)  # 按规则筛选。
    assert summary["station_selection"]["selected_stations"] == ["A"]  # 只有 A 入选。
    assert summary["rejected_count"] == 1  # 范围外缺陷仍计拒绝。
    assert summary["rejection_reasons"] == {"ANOMALY": 1}  # 主原因 ANOMALY。
    assert summary["excluded_count"] == 0  # 该行是缺陷不是范围外。
