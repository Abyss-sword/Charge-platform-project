from __future__ import annotations  # 延迟注解，兼容 3.10。

import csv  # 读取历史区间来源 CSV。
from dataclasses import dataclass, field  # AdaptResult 聚合返回。
from datetime import datetime, timedelta  # 区间切小时的时间计算。
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP  # 电量金额精确十进制。
import re  # 时间与十进制文本的严格校验。
from typing import Any  # 记录与元数据为松散 dict。
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError  # 校验来源时区名。

from .source_registry import PRIVACY_FIELDS  # 行级隐私列复用第一階段黑名单。
from .station_mapping import build_analysis_station_id  # 命名空间站点ID构造复用 station_mapping。

UTC_STAMP = re.compile(r"^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(Z|\+00:00)$")  # 只接受 UTC（Z 或 +00:00）。
PLAIN_DECIMAL = re.compile(r"^[0-9]+(?:\.[0-9]+)?$")  # 与 normalize.py 同口径：拒绝科学计数法与符号。
OBSERVATION_KINDS = {"METER_INTERVAL", "SESSION_ALLOCATED", "PUBLIC_HOURLY"}  # 允许的观测类型。
KIND_PRECEDENCE = {"METER_INTERVAL": 3, "SESSION_ALLOCATED": 2, "PUBLIC_HOURLY": 1}  # 同小时混合时的取值优先级：实测>估计>公开。
ENERGY_FACTORS = {"kwh": Decimal("1"), "wh": Decimal("0.001")}  # 电量单位换算，禁止猜测其它单位。
AMOUNT_FACTORS = {"yuan": Decimal("1"), "cents": Decimal("0.01")}  # 金额单位换算。
MAX_ENERGY_SCALE = 6  # 区间电量最多保留 6 位小数（保留来源精度，不提前舍入到两位）。
REQUIRED_SOURCE_FIELDS = (  # 必填来源列，缺一拒绝。
    "source_record_id", "source_station_id", "interval_start_utc", "interval_end_utc",
    "energy", "energy_unit", "observation_kind", "source_timezone",
)  # 结束：必填来源列。


class BatchBlocked(ValueError):  # 批级阻断：隐私列或来源未授权时整批停止，不允许部分通过。
    pass  # 批级阻断异常。


@dataclass  # 一批适配结果：规范记录、拒绝记账、小时片段。
class AdaptResult:  # 三类产物一起返回，避免调用方分多次跑出不同账。
    accepted: list[dict[str, Any]]  # historical_interval_v1 规范记录。
    rejected: list[dict[str, Any]]  # {"row_number","record_id","primary_reason","diagnostic_tags"}。
    fragments: dict[tuple[str, str], dict[str, Any]]  # (站点, 小时) → 聚合片段。


def _plain(text: Decimal) -> str:  # Decimal → 无指数十进制文本（format f 不做尾零剥离）。
    return format(text, "f")  # 例如 Decimal("7.30") → "7.30"。


def _parse_utc(value: str) -> datetime:  # 严格 UTC 解析：拒绝朴素时间与其它时区偏移。
    match = UTC_STAMP.fullmatch(str(value).strip())  # 先按格式截形。
    if not match:  # 缺 Z、非整秒、其它偏移一律拒绝。
        raise ValueError("INVALID_TIMESTAMP")  # 不猜测时区。
    try:  # 再验证真实日历时间（如 25:00 必须失败）。
        parsed = datetime.strptime(match.group(1) + match.group(2) + match.group(3) + match.group(4) + match.group(5) + match.group(6), "%Y%m%d%H%M%S")  # strptime 不接受任意偏移，先拆字段。
    except ValueError as exc:  # 非法日期时间。
        raise ValueError("INVALID_TIMESTAMP") from exc  # 统一拒绝码。
    return parsed  # 返回朴素 UTC 时间（只用于切小时，输出时再补 Z）。


def _hour_floor(moment: datetime) -> datetime:  # 任意时刻 → 所在整点。
    return moment.replace(minute=0, second=0, microsecond=0)  # 秒级以下归零。


def split_interval_hours(start_utc: str, end_utc: str) -> list[str]:  # 区间覆盖到的整点小时列表（起点小时起，终点不含）。
    start = _parse_utc(start_utc)  # 开始时刻。
    end = _parse_utc(end_utc)  # 结束时刻。
    if end <= start:  # 结束必须晚于开始。
        raise ValueError("INVALID_INTERVAL")  # 拒绝空区间或倒挂区间。
    hours: list[str] = []  # 结果。
    cursor = _hour_floor(start)  # 从开始所在整点起。
    while cursor < end:  # 覆盖到结束前。
        hours.append(cursor.strftime("%Y-%m-%dT%H:00:00Z"))  # 统一整点文本。
        cursor += timedelta(hours=1)  # 逐小时推进。
    return hours  # 例如 09:00-11:00 → ["...T09...Z", "...T10...Z"]。


def allocate_energy_cents(total_text: str, overlaps: list[tuple[str, Decimal]]) -> list[tuple[str, str]]:  # 把会话总量按覆盖秒数分摊到小时，分币守恒（最大余数法，并列取最早小时）。
    total = Decimal(str(total_text))  # 会话总电量（未舍入）。
    if total < 0:  # 负数在行级已拒绝，这里再挡一道。
        raise ValueError("INVALID_ENERGY")  # 保持调用链一致。
    total_cents = int((total * 100).quantize(Decimal("1"), rounding=ROUND_HALF_UP))  # 总量先定分币基准（10.27→1027；10.275→1028）。
    total_seconds = sum((secs for _, secs in overlaps), Decimal("0"))  # 覆盖秒总数。
    if total_seconds <= 0 or total_cents == 0:  # 零覆盖或零电量。
        return [(hour, "0.00") for hour, _ in overlaps]  # 全 0 且守恒。
    raw_cents = [Decimal(total_cents) * secs / total_seconds for _, secs in overlaps]  # 每小时未舍入分币份额。
    floors = [int(value // 1) for value in raw_cents]  # 向下取整部分。
    leftover = total_cents - sum(floors)  # 待分配的尾差分币数（0..len，证明见 docs/stage2/contracts.md）。
    order = sorted(  # 余数大者优先；并列取最早小时（列表已按时间序，稳定排序即可）。
        range(len(overlaps)),  # 片段下标。
        key=lambda i: (-(raw_cents[i] - floors[i]), i),  # 先按小数部分降序，再按下标（即时间）升序。
    )  # 结束：分配顺序。
    adjusted = list(floors)  # 拷贝后逐分币分配。
    for index in order[:leftover]:  # 只给前 leftover 个片段各加 1 分。
        adjusted[index] += 1  # 尾差分配。
    return [  # 输出两位字符串，Σ == total 的两位 HALF_UP。
        (hour, format(Decimal(cents).scaleb(-2).quantize(Decimal("0.01")), ".2f"))  # 分币 → 元文本。
        for (hour, _), cents in zip(overlaps, adjusted)  # 与输入小时一一对应。
    ]  # 结束：输出。


def _coverage_union(spans: list[tuple[int, int]]) -> int:  # 区间并集总秒数：同站多桩不重复计时。
    if not spans:  # 无区间。
        return 0  # 覆盖为 0。
    ordered = sorted(spans)  # 按起点排序。
    merged = [list(ordered[0])]  # 合并游标。
    for start, end in ordered[1:]:  # 逐段合并。
        if start <= merged[-1][1]:  # 与当前段重叠或相接。
            merged[-1][1] = max(merged[-1][1], end)  # 扩展右端。
        else:  # 不相交。
            merged.append([start, end])  # 新开一段。
    return sum(end - start for start, end in merged)  # 并集长度求和。


def parse_interval_row(row: dict[str, str], metadata: dict[str, Any]) -> dict[str, Any]:  # 单行 → historical_interval_v1 记录；失败抛 ValueError(拒绝码)。
    private = sorted(set(str(k).lower() for k in row).intersection(PRIVACY_FIELDS))  # 行级隐私列。
    if private:  # 命中黑名单。
        raise ValueError("PRIVACY_FIELD")  # 拒绝该行（批级由 adapt_csv 阻断）。
    missing = sorted(k for k in REQUIRED_SOURCE_FIELDS if not str(row.get(k, "")).strip())  # 必填列空值。
    if missing:  # 缺字段。
        raise ValueError("MISSING_REQUIRED")  # 不填默认值。
    record_id = str(row["source_record_id"]).strip()  # 来源主键。
    if "\t" in record_id or "\n" in record_id:  # 主键禁止制表/换行（会破坏 TSV/JSONL 行结构）。
        raise ValueError("INVALID_RECORD_ID")  # 结构性拒绝。
    try:  # 校验来源时区名（仅用于展示换算，存储一律 UTC）。
        ZoneInfo(row["source_timezone"].strip())  # IANA 名称必须可解析。
    except (ZoneInfoNotFoundError, ValueError) as exc:  # 未知时区。
        raise ValueError("UNKNOWN_TIMEZONE") from exc  # 拒绝码沿用 normalize.py。
    kind = row["observation_kind"].strip().upper()  # 观测类型。
    if kind not in OBSERVATION_KINDS:  # 未知类型。
        raise ValueError("UNKNOWN_OBSERVATION_KIND")  # 不猜测。
    energy_text = str(row["energy"]).strip()  # 电量原文。
    if not PLAIN_DECIMAL.fullmatch(energy_text):  # 科学计数法/负号拒绝。
        raise ValueError("INVALID_ENERGY")  # 不修成 0。
    unit = row["energy_unit"].strip().lower()  # 电量单位。
    if unit not in ENERGY_FACTORS:  # 未知单位。
        raise ValueError("INVALID_ENERGY")  # 与数值错误同码。
    try:  # 换算 kWh。
        energy = Decimal(energy_text) * ENERGY_FACTORS[unit]  # 保留来源精度。
    except InvalidOperation as exc:  # 极端文本。
        raise ValueError("INVALID_ENERGY") from exc  # 统一拒绝码。
    if energy < 0:  # 负电量。
        raise ValueError("INVALID_ENERGY")  # 拒绝。
    if -energy.as_tuple().exponent > MAX_ENERGY_SCALE:  # 小数位超 6 位。
        raise ValueError("INVALID_ENERGY")  # 超精度拒绝，不悄悄截断。
    start_dt = _parse_utc(row["interval_start_utc"])  # 开始（严格 UTC）。
    end_dt = _parse_utc(row["interval_end_utc"])  # 结束（严格 UTC）。
    if end_dt <= start_dt:  # 倒挂区间。
        raise ValueError("INVALID_INTERVAL")  # 拒绝。
    amount_text = str(row.get("amount", "")).strip()  # 金额可空。
    null_reasons: dict[str, str] = {}  # 空值原因表。
    if amount_text:  # 有金额才换算。
        amount_unit = str(row.get("amount_unit", "")).strip().lower()  # 金额单位。
        if not PLAIN_DECIMAL.fullmatch(amount_text) or amount_unit not in AMOUNT_FACTORS:  # 文本或单位非法。
            raise ValueError("INVALID_AMOUNT")  # 拒绝。
        amount = Decimal(amount_text) * AMOUNT_FACTORS[amount_unit]  # 换算为元。
        if amount < 0:  # 负金额。
            raise ValueError("INVALID_AMOUNT")  # 拒绝。
        amount_yuan = format(amount.quantize(Decimal("0.01"), rounding=ROUND_HALF_UP), ".2f")  # 两位 HALF_UP（10.275→10.28）。
    else:  # 未知金额。
        amount_yuan = None  # null。
        null_reasons["amount_yuan"] = "NOT_COLLECTED"  # 说明未采集。
    capacity_text = str(row.get("capacity_piles", "")).strip()  # 容量可空。
    if capacity_text:  # 有容量。
        if not capacity_text.isdigit() or int(capacity_text) < 1:  # 必须是正整数。
            raise ValueError("INVALID_CAPACITY")  # 拒绝。
        capacity: int | None = int(capacity_text)  # 转整数。
    else:  # 未知容量。
        capacity = None  # null。
        null_reasons["capacity_piles"] = "NOT_COLLECTED"  # 说明。
    pile_id = str(row.get("pile_id", "")).strip() or None  # 桩号可空。
    if pile_id is None:  # 空桩号给原因。
        null_reasons["pile_id"] = "NOT_APPLICABLE" if kind == "PUBLIC_HOURLY" else "NOT_COLLECTED"  # 公开表无桩概念。
    for nullable in ("available_piles", "occupied_piles", "temperature_c", "weather_source"):  # 本适配器尚无来源列。
        null_reasons[nullable] = "NOT_COLLECTED"  # 统一登记未采集，禁止补 0/假值。
    return {  # 规范记录（与 historical_interval_v1.schema.json 对应）。
        "source_id": str(metadata["source_id"]),  # 来源命名空间。
        "dataset_version": str(metadata["dataset_version"]),  # 数据版本。
        "record_id": record_id,  # 来源主键。
        "analysis_station_id": build_analysis_station_id(str(metadata["source_id"]), str(row["source_station_id"]).strip()),  # 带命名空间站点。
        "interval_start_utc": start_dt.strftime("%Y-%m-%dT%H:%M:%SZ"),  # 规范 UTC 文本。
        "interval_end_utc": end_dt.strftime("%Y-%m-%dT%H:%M:%SZ"),  # 规范 UTC 文本。
        "energy_kwh": _plain(energy),  # 未舍入电量文本。
        "observation_kind": kind,  # 观测类型。
        "data_mode": str(metadata["data_mode"]),  # 数据模式随行传播。
        "source_timezone": row["source_timezone"].strip(),  # 来源时区。
        "pile_id": pile_id,  # 桩号。
        "amount_yuan": amount_yuan,  # 金额两位或 null。
        "available_piles": None,  # 无来源列。
        "capacity_piles": capacity,  # 容量或 null。
        "occupied_piles": None,  # 无完整状态来源，不做会话占用以外的声明。
        "temperature_c": None,  # 无授权天气来源。
        "weather_source": None,  # 无授权天气来源。
        "null_reasons": null_reasons,  # 空值原因。
        "_session_started": _session_flag(row),  # 会话起始标记（内部用，不进 schema）。
    }  # 结束：记录。


def _session_flag(row: dict[str, str]) -> int:  # session_started 列只允许 0/1/空。
    text = str(row.get("session_started", "")).strip()  # 原文。
    if not text:  # 空。
        return 0  # 默认非会话起始。
    if text in {"0", "1"}:  # 合法布尔。
        return int(text)  # 返回。
    raise ValueError("INVALID_SESSION_FLAG")  # 其它值拒绝。


def adapt_csv(source_csv: str | Path, metadata: dict[str, Any], existing_keys: set[tuple[str, str]] | None = None) -> AdaptResult:  # 批级适配入口：隐私阻断 → 逐行转换 → 去重 → 小时片段聚合。
    with open(source_csv, "r", encoding="utf-8-sig", newline="") as stream:  # utf-8-sig 兼容 Excel BOM。
        reader = csv.DictReader(stream)  # 表头驱动。
        headers = [str(h).strip().lower() for h in (reader.fieldnames or [])]  # 表头小写集合。
        blocked = sorted(set(headers).intersection(PRIVACY_FIELDS))  # 批级隐私检查。
        if blocked:  # 命中隐私列。
            raise BatchBlocked("PRIVACY_FIELDS_IN_BATCH: " + ", ".join(blocked))  # 整批阻断，禁止部分放行。
        missing_columns = sorted(set(REQUIRED_SOURCE_FIELDS) - set(headers))  # 必填列缺一不可。
        if missing_columns:  # 表头不完整。
            raise BatchBlocked("MISSING_REQUIRED_COLUMNS: " + ", ".join(missing_columns))  # 批级拒绝。
        rows = list(reader)  # 读入全部数据行。
    source_id = str(metadata["source_id"])  # 命名空间。
    seen: set[str] = set()  # 批内已见主键。
    existing = {key for key in (existing_keys or set()) if key[0] == source_id}  # 只对同命名空间做跨批去重。
    accepted: list[dict[str, Any]] = []  # 通过记录。
    rejected: list[dict[str, Any]] = []  # 拒绝记账。
    fragment_acc: dict[tuple[str, str], dict[str, Any]] = {}  # 片段累计。
    for row_number, row in enumerate(rows, start=2):  # 数据从第 2 行起。
        record_id = str(row.get("source_record_id", "")).strip()  # 主键。
        tags: list[str] = []  # 诊断标签。
        if record_id and (source_id, record_id) in existing:  # 跨批重复。
            rejected.append({"row_number": str(row_number), "record_id": record_id, "primary_reason": "DUPLICATE", "diagnostic_tags": ["CROSS_BATCH"]})  # 主原因不变，标签区分来源。
            continue  # 不重复累加。
        if record_id and record_id in seen:  # 批内重复。
            rejected.append({"row_number": str(row_number), "record_id": record_id, "primary_reason": "DUPLICATE", "diagnostic_tags": []})  # 第二条起拒绝。
            continue  # 不覆盖第一条。
        if record_id:  # 登记主键。
            seen.add(record_id)  # 先登记再校验，重复语义与 normalize.py 一致。
        try:  # 单行转换。
            record = parse_interval_row(row, metadata)  # 抛 ValueError(码)。
        except ValueError as error:  # 记账。
            rejected.append({"row_number": str(row_number), "record_id": record_id, "primary_reason": str(error), "diagnostic_tags": tags})  # 主原因唯一。
            continue  # 下一行。
        accepted.append(record)  # 通过。
        _accumulate_fragments(fragment_acc, record)  # 累计小时片段。
    for fragment in fragment_acc.values():  # 收口：覆盖并集。
        fragment["coverage_seconds"] = _coverage_union(fragment.pop("_spans"))  # 并集秒数替换内部区间表。
    return AdaptResult(accepted=accepted, rejected=rejected, fragments=fragment_acc)  # 三账齐返。


def _accumulate_fragments(acc: dict[tuple[str, str], dict[str, Any]], record: dict[str, Any]) -> None:  # 一条记录的小时级累计：未舍入能量、会话起始计数、覆盖区间、观测类型。
    start = _parse_utc(record["interval_start_utc"])  # 开始。
    end = _parse_utc(record["interval_end_utc"])  # 结束。
    duration = Decimal(int((end - start).total_seconds()))  # 时长秒。
    station = record["analysis_station_id"]  # 站点。
    kind = record["observation_kind"]  # 观测类型。
    energy = Decimal(record["energy_kwh"])  # 未舍入电量。
    cursor = _hour_floor(start)  # 逐小时推进。
    while cursor < end:  # 与 split_interval_hours 同口径。
        hour_text = cursor.strftime("%Y-%m-%dT%H:00:00Z")  # 整点。
        overlap_start = max(start, cursor)  # 片段起点。
        overlap_end = min(end, cursor + timedelta(hours=1))  # 片段终点。
        seconds = Decimal(int((overlap_end - overlap_start).total_seconds()))  # 本小时覆盖秒。
        key = (station, hour_text)  # 片段键。
        bucket = acc.setdefault(key, {"energy": Decimal("0"), "session_starts": 0, "_spans": [], "_kinds": set()})  # 初始化。
        bucket["energy"] += energy * seconds / duration if duration > 0 else Decimal("0")  # 按秒比例的未舍入份额。
        bucket["_spans"].append((int((overlap_start - cursor).total_seconds()), int((overlap_end - cursor).total_seconds())))  # 相对整点的区间。
        bucket["_kinds"].add(kind)  # 记录类型。
        if record["_session_started"] == 1 and start >= cursor and start < cursor + timedelta(hours=1):  # 会话起始只落在开始小时。
            bucket["session_starts"] += 1  # 计一次。
        cursor += timedelta(hours=1)  # 下一小时。


def fragments_to_tsv_rows(fragments: dict[tuple[str, str], dict[str, Any]]) -> list[str]:  # 片段 → MR staging TSV 行（6 列，无表头）。
    rows: list[str] = []  # 输出行。
    for (station, hour) in sorted(fragments):  # 稳定排序：站点、时间。
        fragment = fragments[(station, hour)]  # 片段。
        kind = max(fragment["_kinds"], key=lambda k: KIND_PRECEDENCE[k])  # 混合类型取实测优先。
        station_text = str(station)  # 站点列。
        if "\t" in station_text or "\n" in station_text:  # 结构守卫（构造时已挡，双保险）。
            raise ValueError("INVALID_STATION_ID")  # 拒绝。
        energy_text = _plain(fragment["energy"])  # 未舍入电量文本。
        rows.append("\t".join([station_text, hour, energy_text, str(fragment["session_starts"]), str(fragment["coverage_seconds"]), kind]))  # 固定 6 列。
    return rows  # 供 publish_dataset 写文件与 MR 读取。


# 【文件讲解·答辩用】
# 功能作用：把来源区间 CSV 变成 historical_interval_v1 记录与站点×小时片段：非法时间、负电量、
#   重复、隐私列都被记账拒绝；10.27 跨两小时分摊成 5.14+5.13 分币守恒；覆盖取并集不是求和。
# 技术原理：Decimal 精确十进制与最大余数法分币（尾差给余数最大、并列取最早小时）；UTC 严格
#   校验；批级隐私列阻断（BatchBlocked）；跨批按（source_id, record_id) 去重只加标签不计数。
# 与其他文件关系：复用 source_registry.PRIVACY_FIELDS；publish_dataset 消费 AdaptResult 写
#   staging TSV；T04 的 Java MR 以该 TSV 为输入并用 BigDecimal 复算同一份账。
# 答辩时可以怎么讲：我们不猜单位、不补空值、不提前舍入；所有拒绝都有唯一主原因，诊断标签
#   不影响拒绝计数，因此"输入=通过+拒绝"永远能对上账。
