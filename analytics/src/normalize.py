from __future__ import annotations  # 延迟注解，兼容 3.10。

import csv  # DictReader 按表头把一行变成 dict。
from datetime import datetime  # 解析 ISO 事件时间。
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP  # 电量金额用十进制，不用 float 累加。
from pathlib import Path  # 打开来源 CSV。
import re  # 拒绝科学计数法，只接受普通非负十进制文本。
from typing import Any  # 规范记录与元数据用 dict。
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError  # 按声明时区生成带偏移 ISO。

from .source_registry import PRIVACY_FIELDS  # 行级再扫一遍隐私列名。
from .station_mapping import map_station  # 来源站号 → 内部 station_id。

STATUS_MAP = {  # 来源状态 → charge_event_v2 契约状态。
    "RESERVED": "RESERVED",  # 已预约。
    "CHARGING": "CHARGING",  # 充电中。
    "PENDING_PAYMENT": "PENDING_PAYMENT",  # 待支付。
    "COMPLETED": "FINISHED",  # 来源“完成”统一成 FINISHED。
    "STOPPED": "FINISHED",  # 停止充电也视为已结束订单。
    "FINISHED": "FINISHED",  # 已是契约名则保持。
    "FAILED": "CANCELED",  # 失败记取消，不进营收。
    "CANCELED": "CANCELED",  # 取消。
    "EXPIRED": "EXPIRED",  # 预约过期。
}  # 结束：来源状态 → charge_event_v2 契约状态。
PLAIN_DECIMAL = re.compile(r"^[0-9]+(?:\.[0-9]+)?$")  # 只匹配 12 或 12.3，拒绝 1e3、负数、空串。
REQUIRED_SOURCE_FIELDS = {  # 缺任一字段就 MISSING_REQUIRED，不做默认值填补。
    "source_record_id", "source_station_id", "event_time", "timezone", "energy",  # 主键、站、时间、时区、电量。
    "energy_unit", "duration", "duration_unit", "amount", "amount_unit", "event_status"  # 单位、时长、金额、状态。
}  # 结束：缺任一字段就 MISSING_REQUIRED，不做默认值填补。


def _time(value: str, timezone: str) -> str:  # 用清单/行声明的 IANA 时区，把事件时间规范成带偏移的 ISO 字符串。
    try:  # 尝试：如 Asia/Shanghai。
        zone = ZoneInfo(timezone)  # 如 Asia/Shanghai。
    except ZoneInfoNotFoundError as exc:  # 时区名无效。
        raise ValueError("UNKNOWN_TIMEZONE") from exc  # 清洗拒绝码。
    try:  # 尝试：解析事件时间文本。
        parsed = datetime.fromisoformat(value)  # 解析事件时间文本。
    except ValueError as exc:  # 不是 ISO。
        raise ValueError("INVALID_TIME") from exc  # 不猜测格式。
    if parsed.tzinfo is None:  # 无时区则贴上声明时区，不当成 UTC。
        parsed = parsed.replace(tzinfo=zone)  # 朴素时间 + 声明 zone。
    return parsed.astimezone(zone).isoformat()  # 转换到声明时区再输出。


def _decimal_value(value: str, unit: str, factors: dict[str, Decimal], reason: str) -> str:  # 按单位乘系数，校验非负有限，再四舍五入到两位小数字符串。
    if unit not in factors:  # 单位不在允许集合（如 kwh/wh 或 yuan/cents）。
        raise ValueError(reason)  # 用调用方传入的拒绝码。
    text = str(value).strip()  # 去掉首尾空白再匹配。
    if not PLAIN_DECIMAL.fullmatch(text):  # 科学计数法或符号一律非法。
        raise ValueError(reason)  # 与单位错误共用同一 reason，避免泄漏内部细节。
    try:  # 尝试：例如 wh×0.001 得到 kWh。
        result = Decimal(text) * factors[unit]  # 例如 wh×0.001 得到 kWh。
    except InvalidOperation as exc:  # Decimal 仍可能对极端文本失败。
        raise ValueError(reason) from exc  # 转成清洗码。
    if not result.is_finite() or result < 0:  # 无穷或负数。
        raise ValueError(reason)  # 不修成 0。
    return format(result.quantize(Decimal("0.01"), rounding=ROUND_HALF_UP), ".2f")  # 固定两位，跨语言展示一致。


def _duration_seconds(value: str, unit: str) -> int:  # 秒或分钟 → 非负整数秒；带小数分钟若不整除则拒绝。
    if unit not in {"seconds", "minutes"}:  # 只认这两种时长单位。
        raise ValueError("INVALID_DURATION_UNIT")  # 未知单位。
    text = str(value).strip()  # 去空白。
    if not PLAIN_DECIMAL.fullmatch(text):  # 同样拒绝科学计数法。
        raise ValueError("INVALID_DURATION_UNIT")  # 文本不合法。
    try:  # 尝试：分钟乘 60。
        result = Decimal(text) * (Decimal("60") if unit == "minutes" else Decimal("1"))  # 分钟乘 60。
    except InvalidOperation as exc:  # Decimal 解析失败。
        raise ValueError("INVALID_DURATION_UNIT") from exc  # 统一拒绝码。
    if result != result.to_integral_value() or result < 0:  # 必须是整数秒且非负。
        raise ValueError("INVALID_DURATION_UNIT")  # 1.5 分钟这种不自动 round。
    return int(result)  # 转 Python int 写入规范记录。


def normalize_row(row: dict[str, str], mapping: dict[str, int], metadata: dict[str, Any]) -> dict[str, Any]:  # 单行 → charge_event_v2；失败抛 ValueError，由 normalize_csv 记 rejected。
    private = sorted(set(k.lower() for k in row).intersection(PRIVACY_FIELDS))  # 行里若带隐私列名则拒绝整行。
    if private:  # 发现隐私字段。
        raise ValueError("PRIVACY_FIELD")  # 不删除列后放行。
    missing = sorted(k for k in REQUIRED_SOURCE_FIELDS if not str(row.get(k, "")).strip())  # 必填空值。
    if missing:  # 缺字段。
        raise ValueError("MISSING_REQUIRED")  # 不填默认值。
    raw_status = row["event_status"].upper()  # 状态大小写不敏感。
    if raw_status not in STATUS_MAP:  # 未知状态。
        raise ValueError("INVALID_STATUS")  # 不映射成 FINISHED 蒙混。
    source_event_count_text = str(row.get("source_event_count", "1")).strip() or "1"  # 聚合源声明该小时原始会话数；普通逐事件源默认 1。
    if not source_event_count_text.isdigit():  # 会话数必须是非负整数。
        raise ValueError("INVALID_EVENT_COUNT")  # 不猜测或四舍五入。
    source_event_count = int(source_event_count_text)  # 给小时聚合恢复真实会话计数。
    return {  # 规范记录，供聚合与质量对账。
        "source_record_id": row["source_record_id"],  # 来源主键，重复检测用。
        "source_station_id": row["source_station_id"],  # 保留来源站号便于追溯。
        "station_id": map_station(row["source_station_id"], mapping),  # 内部站号；未知站抛 UNMAPPED_STATION。
        "occurred_at": _time(row["event_time"], row["timezone"]),  # 带时区的事件时间。
        "ingested_at": metadata["ingested_at"],  # 批次摄入时间，流水线写入。
        "timezone": row["timezone"],  # 原声明时区。
        "energy_kwh": _decimal_value(row["energy"], row["energy_unit"].lower(), {"wh": Decimal("0.001"), "kwh": Decimal("1")}, "INVALID_ENERGY_UNIT"),  # 统一 kWh 两位小数。
        "duration_seconds": _duration_seconds(row["duration"], row["duration_unit"].lower()),  # 统一秒。
        "amount_yuan": _decimal_value(row["amount"], row["amount_unit"].lower(), {"cents": Decimal("0.01"), "yuan": Decimal("1")}, "INVALID_AMOUNT_UNIT"),  # 统一元。
        "status": STATUS_MAP[raw_status],  # 契约状态。
        "data_mode": metadata["data_mode"],  # 随行带上来源模式，禁止后面和真实数据混聚。
        "dataset_name": metadata["dataset_name"],  # 数据集名。
        "dataset_version": metadata["dataset_version"],  # 版本。
        "ingest_batch": metadata["ingest_batch"],  # 批次号，curated 可追溯。
        "source_event_count": source_event_count,  # 不包含任何个人标识，只保留统计计数。
    }  # 结束：规范记录，供聚合与质量对账。


def normalize_csv(source: str | Path, mapping: dict[str, int], metadata: dict[str, Any]) -> tuple[list[dict[str, Any]], list[dict[str, str]]]:  # 逐行清洗：成功进 accepted，失败进 rejected；重复 source_record_id 记 DUPLICATE。
    accepted: list[dict[str, Any]] = []  # 规范记录。
    rejected: list[dict[str, str]] = []  # 带行号和 reason_code。
    seen: set[str] = set()  # 本文件内已出现的来源主键。
    with Path(source).open("r", encoding="utf-8-sig", newline="") as stream:  # 读固化后的 raw CSV。
        for row_number, row in enumerate(csv.DictReader(stream), start=2):  # 数据从第 2 行起，便于对照 Excel。
            record_id = str(row.get("source_record_id", ""))  # 可能为空，空则后面 MISSING_REQUIRED。
            if record_id and record_id in seen:  # 非空且重复。
                rejected.append({"row_number": str(row_number), "source_record_id": record_id, "reason_code": "DUPLICATE", "reason": "duplicate source record id"})  # 第二条起拒绝。
                continue  # 不覆盖第一条。
            if record_id:  # 非空主键登记，避免空 id 互相当成重复。
                seen.add(record_id)  # 记入已见集合。
            try:  # 尝试：单行转换。
                accepted.append(normalize_row(row, mapping, metadata))  # 单行转换。
            except ValueError as error:  # 清洗码或 UNMAPPED_STATION。
                rejected.append({"row_number": str(row_number), "source_record_id": record_id, "reason_code": str(error), "reason": "record rejected without implicit repair"})  # 明确：不隐式修复。
    return accepted, rejected  # 交给 quality 对账和 staging 落盘。

# 【文件讲解·答辩用】
# 功能作用：把来源 CSV 变成 charge_event_v2 规范事件，单位、时区、状态统一；坏行进 rejected 不修补。
# 技术原理：Decimal 换算电量金额；ZoneInfo 处理时区；状态表映射；隐私列与必填在行级再拦一次。
# 代码思路：normalize_row 抛 ValueError；normalize_csv 捕获并记账；重复主键只留第一次。
# 与其他文件关系：依赖 station_mapping、source_registry.PRIVACY_FIELDS；被 demo_pipeline 调用；输出给 quality 和 build_curated。
# 答辩时可以怎么讲：清洗原则是“拒绝并说明原因”，不把 1e3 或未知站悄悄改成合法值，否则质量报告对不上原始文件。
