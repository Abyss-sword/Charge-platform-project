# 成员3只读大屏投影：CHARGE_PLATFORM_DB 指向成员1同一 SQLite。
# 连接使用 mode=ro + PRAGMA query_only，不改订单/钱包/电桩/告警/工单。
# 用电按同桩相邻累计电表差分；营收只加 FINISHED；在线率/使用率分母见 metricDefinitions。
"""Read-only Stage-1 dashboard projections over the team's single SQLite database."""  # 模块说明：第一阶段大屏全部从统一库只读投影。

from __future__ import annotations  # 延迟注解，兼容 3.10。

import os  # 读 CHARGE_PLATFORM_DB。
import sqlite3  # URI 只读连接与 Row 工厂。
import time  # 库忙时短暂退避，不阻塞 C++ 写端。
from collections import defaultdict  # 按日营收、按小时电量归并。
from contextlib import closing  # 用完关闭只读连接。
from datetime import datetime, timedelta, timezone  # 东八区自然日与遥测时间。
from decimal import Decimal, InvalidOperation  # 金额电量不用 float 累加。
from pathlib import Path  # 库文件路径。
from typing import Any, Iterable  # SQL 参数与通用单元格。

PROJECT_DIR = Path(__file__).resolve().parents[2]  # 仓库根：analytics/src 上两级。
PROJECT_TIMEZONE = timezone(timedelta(hours=8), name="Asia/Shanghai")  # 营收“今日”按上海自然日。
REQUIRED_SCHEMA = {  # 缺表缺列就拒绝，绝不编造看起来合理的假数。
    "station_info": {"id", "name", "price_yuan_per_kwh"},  # 站点名与锁价。
    "pile_info": {"id", "station_id", "pile_sn", "status"},  # 桩归属与实时状态。
    "charge_order": {  # 订单：状态、电量、金额、起止时间。
        "id", "status", "energy_kwh", "amount_yuan", "started_at",  # 主键与计费字段。
        "ended_at", "created_at", "price_locked",  # 结束时间与锁价。
    },  # 结束：订单：状态、电量、金额、起止时间。
    "pile_telemetry": {  # 遥测：温压流功与累计电表。
        "id", "pile_id", "recorded_at", "temperature_c", "voltage_v",  # 时间与电气量。
        "current_a", "power_w", "energy_kwh", "source_mode",  # 功率、累计电量、来源模式。
    },  # 结束：遥测：温压流功与累计电表。
    "alarm_event": {  # 告警事件。
        "id", "pile_id", "alarm_type", "severity", "content", "status", "occurred_at",  # 类型严重度与发生时间。
    },  # 结束：告警事件。
    "maintenance_ticket": {  # 运维工单。
        "id", "alarm_id", "assignee", "status", "updated_at",  # 关联告警与处理人。
    },  # 结束：运维工单。
}  # 结束：缺表缺列就拒绝，绝不编造看起来合理的假数。


class DashboardDatabaseError(RuntimeError):  # 给大屏展示的中文错误，不是 sqlite 内部码。
    """A clear, user-displayable database availability or compatibility error."""  # 给大屏展示的中文错误，不是 sqlite 内部码。


def configured_database_path() -> Path:  # 只解析路径，不 create。
    """Return the shared database path; never create a second analytics database."""  # 只解析路径，不 create。
    configured = os.environ.get("CHARGE_PLATFORM_DB")  # 启动脚本注入的统一库。
    return (  # 有环境变量用它，否则仓库默认 runtime 文件。
        Path(configured).expanduser()  # 支持 ~。
        if configured  # 已配置。
        else PROJECT_DIR / "runtime" / "charge_platform.sqlite"  # 成员1默认库名。
    )  # 结束：有环境变量用它，否则仓库默认 runtime 文件。


def _connect_readonly(path: Path) -> sqlite3.Connection:  # 双保险只读。
    """Open SQLite in URI read-only/query-only mode with a short busy timeout."""  # 双保险只读。
    if not path.is_file():  # 文件都不在。
        raise DashboardDatabaseError(f"统一SQLite数据库不存在：{path}")  # 中文路径给前端。
    try:  # 尝试：URI 模式才能加 mode=ro。
        connection = sqlite3.connect(  # URI 模式才能加 mode=ro。
            f"{path.resolve().as_uri()}?mode=ro", uri=True, timeout=1.0  # 只读 + 1 秒 busy。
        )  # 结束：URI 模式才能加 mode=ro。
        connection.row_factory = sqlite3.Row  # 按列名取值。
        connection.execute("PRAGMA query_only = ON")  # 连接级禁止写。
        connection.execute("PRAGMA busy_timeout = 1000")  # 再声明 1000ms。
        return connection  # 交给 closing。
    except sqlite3.Error as error:  # 打开失败。
        raise DashboardDatabaseError(f"SQLite只读连接失败：{error}") from error  # 包装成业务错误。


def _rows(  # busy/locked 时短退避重试，避免拖住 C++ 写端。
    database: sqlite3.Connection, sql: str, parameters: Iterable[Any] = ()  # 绑定参数默认空。
) -> list[sqlite3.Row]:  # 返回本查询全部 Row。
    """Retry short-lived busy/locked reads without delaying the C++ writer."""  # 读端退避，不抢写锁太久。
    for attempt in range(3):  # 最多三次。
        try:  # 尝试：一次取完本查询。
            return database.execute(sql, tuple(parameters)).fetchall()  # 一次取完本查询。
        except sqlite3.OperationalError as error:  # locked/busy。
            busy = "locked" in str(error).lower() or "busy" in str(error).lower()  # 是否可重试。
            if not busy or attempt == 2:  # 非忙错误或已是最后一次。
                raise  # 原样抛出。
            time.sleep(0.05 * (attempt + 1))  # 50ms、100ms 退避。
    return []  # 理论上走不到，满足类型检查。


def _snapshot_version(database: sqlite3.Connection) -> int:  # S1-A 联动契约：只读读取全局写版本。
    """Read linkage_meta.changeVersion so the dashboard can expose snapshotVersion."""  # 旧库回退 0。
    try:  # 迁移 010 之前的旧库没有该表，不能因此拒绝服务。
        row = database.execute(  # 单行只读查询。
            "SELECT value FROM linkage_meta WHERE key='changeVersion'"  # 固定键。
        ).fetchone()  # 取一行。
    except sqlite3.OperationalError:  # 表不存在。
        return 0  # 回退：旧库口径，Web 端退化为只看 updatedAt。
    if not row:  # 表存在但无记录（理论不发生）。
        return 0  # 回退。
    try:  # 值按文本数字存储。
        return int(row[0])  # 转整数返回。
    except (TypeError, ValueError):  # 非数字内容。
        return 0  # 回退。


def _validate_schema(database: sqlite3.Connection) -> None:  # 旧库宁可失败。
    """Reject an old or unrelated database instead of returning plausible fake data."""  # 旧库宁可失败。
    missing: list[str] = []  # 缺失说明列表。
    for table, required_columns in REQUIRED_SCHEMA.items():  # 逐表检查。
        columns = {  # 当前表列名集合。
            str(row["name"])  # PRAGMA 的 name 列。
            for row in _rows(database, f"PRAGMA table_info({table})")  # 表不存在则空集合。
        }  # 结束：当前表列名集合。
        absent = sorted(required_columns - columns)  # 缺的列。
        if absent:  # 有缺列。
            missing.append(f"{table}缺少{','.join(absent)}")  # 中文条目。
    if missing:  # 任一项不兼容。
        raise DashboardDatabaseError("数据库结构不兼容：" + "；".join(missing))  # 一次说清。


def _decimal(value: Any) -> Decimal:  # 金额电量走 Decimal。
    """Parse business decimals without binary floating-point accumulation."""  # 金额电量走 Decimal。
    try:  # 尝试：空当 0.00。
        return Decimal(str(value if value not in (None, "") else "0.00"))  # 空当 0.00。
    except InvalidOperation as error:  # 非法文本。
        raise DashboardDatabaseError(f"数据库包含非法十进制数值：{value}") from error  # 不静默当 0。


def _money(value: Decimal) -> str:  # 千分位两位，给营收展示。
    return f"{value.quantize(Decimal('0.01')):,.2f}"  # 千分位两位，给营收展示。


def _parse_time(value: Any) -> datetime | None:  # 把 SQLite 里的时间文本解析成 aware datetime；失败返回 None。
    if value in (None, ""):  # 空。
        return None  # 调用方当缺失。
    text = str(value).strip().replace(" ", "T")  # 空格改 T，贴近 ISO。
    if text.endswith("Z"):  # UTC 缩写。
        text = text[:-1] + "+00:00"  # fromisoformat 需要显式偏移。
    try:  # 尝试：解析。
        parsed = datetime.fromisoformat(text)  # 解析。
    except ValueError:  # 格式不对。
        return None  # 记 invalid_time，不猜。
    if parsed.tzinfo is None:  # 朴素时间。
        return parsed.replace(tzinfo=timezone.utc)  # 按 UTC 解释后再转上海。
    return parsed  # 已带时区。


def _display_time(value: Any) -> str:  # 先解析。
    parsed = _parse_time(value)  # 先解析。
    return parsed.astimezone(PROJECT_TIMEZONE).strftime("%Y-%m-%d %H:%M") if parsed else "暂无"  # 展示到分钟。


def _display_timestamp(value: Any) -> str:  # 遥测面板用秒。
    """将实时遥测时间显示到秒，便于观察大屏的三秒刷新。"""  # 遥测面板用秒。
    parsed = _parse_time(value)  # 解析。
    return (  # 上海时区秒级。
        parsed.astimezone(PROJECT_TIMEZONE).strftime("%Y-%m-%d %H:%M:%S")  # 到秒。
        if parsed  # 解析成功。
        else "暂无"  # 失败。
    )  # 结束：上海时区秒级。


def _as_float(value: Any) -> float:  # 空电气量当 0，仅用于展示。
    return float(value) if value is not None and value != "" else 0.0  # 空电气量当 0，仅用于展示。


def _as_int(value: Any) -> int:  # 功率瓦特转 int。
    return int(value) if value is not None and value != "" else 0  # 功率瓦特转 int。


def _alarm_severity(value: str) -> str:  # 其它级别显示提示。
    return {"CRITICAL": "严重", "WARNING": "警告"}.get(value, "提示")  # 其它级别显示提示。


def _status_counts(rows: list[sqlite3.Row]) -> dict[str, int]:  # 五态计数，缺态为 0。
    result = {name: 0 for name in ("IDLE", "RESERVED", "CHARGING", "FAULT", "OFFLINE")}  # 五态计数，缺态为 0。
    for row in rows:  # 每台桩一行。
        status = str(row["status"])  # 业务状态枚举。
        if status in result:  # 未知状态不计入，避免撑破分母口径。
            result[status] += 1  # 累加。
    return result  # 给在线率/使用率。


def _order_summary(  # 今日服务次数、FINISHED 营收、近 7 日趋势。
    order_rows: list[sqlite3.Row], today_start: datetime  # 上海今日 00:00。
) -> tuple[int, Decimal, list[dict[str, str]]]:  # (今日服务次数, 今日营收, 趋势)。
    """Compute completed revenue with Decimal and explicit Asia/Shanghai boundaries."""  # 营收只加 FINISHED。
    today_services = 0  # 今日开工次数：started_at 落在今日。
    today_revenue = Decimal("0.00")  # 今日 FINISHED 金额。
    revenue_by_day: dict[str, Decimal] = defaultdict(lambda: Decimal("0.00"))  # 按 mm-dd 归并。
    for row in order_rows:  # 全量订单（库规模教学级）。
        started = _parse_time(row["started_at"])  # 开始时间。
        ended = _parse_time(row["ended_at"])  # 结束时间。
        if started and started.astimezone(PROJECT_TIMEZONE) >= today_start:  # 今日已开始。
            today_services += 1  # 服务次数含未完成。
        if str(row["status"]) != "FINISHED":  # 未完成不进营收。
            continue  # 跳过金额。
        amount = _decimal(row["amount_yuan"])  # 订单金额。
        if ended:  # 有结束时间才归日。
            local_ended = ended.astimezone(PROJECT_TIMEZONE)  # 上海日历。
            revenue_by_day[local_ended.strftime("%m-%d")] += amount  # 按结束日落袋。
            if local_ended >= today_start:  # 今日结束。
                today_revenue += amount  # 今日营收。
    trend = [  # 最近 7 个有营收的日期。
        {"period": day, "revenueYuan": _money(amount)}  # 展示字符串。
        for day, amount in sorted(revenue_by_day.items())[-7:]  # 按日期排序截尾。
    ]  # 结束：最近 7 个有营收的日期。
    return today_services, today_revenue, trend  # 指标 + 趋势。


def _telemetry_summary(  # 累计电表相邻差分，按上海整点归并用电。
    telemetry_rows: list[sqlite3.Row],  # 已按 id 升序。
) -> tuple[list[dict[str, Any]], dict[str, int]]:  # (小时用电趋势, 质量分项)。
    """Convert lifetime meters to valid adjacent deltas, grouped by local hour."""  # 累计电表差分才是用电。
    previous: dict[int, tuple[datetime, Decimal]] = {}  # 每桩上一笔合法样本。
    energy_by_hour: dict[str, Decimal] = defaultdict(lambda: Decimal("0.00"))  # 上海整点用电。
    quality = {  # 分项质量计数，给大屏 checks。
        "missing": 0,  # 电量解析失败。
        "invalid_time": 0,  # 时间解析失败或无偏移。
        "time_backward": 0,  # 时间不前进。
        "energy_backward": 0,  # 电表倒退。
        "duplicate": 0,  # 同桩同时同电量重复。
        "unknown_relation": 0,  # 桩不存在。
        "simulated": 0,  # source_mode=SIMULATED 条数。
    }  # 结束：分项质量计数，给大屏 checks。
    seen: set[tuple[int, str, str]] = set()  # 去重键。
    for row in telemetry_rows:  # 顺序扫，才能做相邻差分。
        pile_id = int(row["pile_id"])  # 桩主键。
        recorded_at = _parse_time(row["recorded_at"])  # 采样时间。
        try:  # 尝试：累计电表。
            energy = _decimal(row["energy_kwh"])  # 累计电表。
        except DashboardDatabaseError:  # 非法十进制。
            quality["missing"] += 1  # 记缺失。
            continue  # 不参与差分。
        if recorded_at is None:  # 时间废了。
            quality["invalid_time"] += 1  # 记非法时间。
            continue  # 跳过。
        raw_time = str(row["recorded_at"])  # 原始文本，检查是否带时区。
        if not (raw_time.endswith("Z") or (len(raw_time) >= 6 and raw_time[-6] in "+-")):  # 无 Z 也无 ±HH:MM。
            quality["invalid_time"] += 1  # 仍参与后续（已 parse 成功），但计一次质量问题。
        if row["pile_exists"] == 0:  # LEFT JOIN 不到桩。
            quality["unknown_relation"] += 1  # 脏关联。
        if str(row["source_mode"]) == "SIMULATED":  # 教学模拟帧。
            quality["simulated"] += 1  # 计数，不因此丢弃。
        key = (pile_id, raw_time, str(row["energy_kwh"]))  # 重复上报键。
        if key in seen:  # 已见。
            quality["duplicate"] += 1  # 幂等丢弃。
            continue  # 不更新 previous，避免 0 差分刷小时。
        seen.add(key)  # 登记。

        prior = previous.get(pile_id)  # 该桩上一笔。
        if prior:  # 有历史才能差分。
            prior_time, prior_energy = prior  # 拆开。
            if recorded_at <= prior_time:  # 时间倒退或相等。
                quality["time_backward"] += 1  # 不计入用电。
            elif energy < prior_energy:  # 电表倒退。
                quality["energy_backward"] += 1  # 不计入用电。
            elif recorded_at - prior_time <= timedelta(hours=24):  # 间隔不超过 24h 才当作连续会话。
                hour = recorded_at.astimezone(PROJECT_TIMEZONE).strftime("%m-%d %H:00")  # 落在哪个整点。
                energy_by_hour[hour] += energy - prior_energy  # 有效增量。
        previous[pile_id] = (recorded_at, energy)  # 无论是否计入，推进指针。

    actual = [  # 最近 24 个有电量的小时。
        {"hour": hour, "actual": float(value.quantize(Decimal("0.01")))}  # 展示用 float。
        for hour, value in sorted(energy_by_hour.items())[-24:]  # 按小时排序截尾。
    ]  # 结束：最近 24 个有电量的小时。
    for index, item in enumerate(actual):  # 给每个点补规则预测。
        # 只用更早的点做 3 点均值，不用当前点，避免“用自己预测自己”。
        history = actual[max(0, index - 3):index]  # 前最多 3 点。
        item["forecast"] = (  # 点数不足则 None。
            round(sum(point["actual"] for point in history) / len(history), 2)  # 均值两位。
            if len(history) >= 2  # 至少 2 点才估。
            else None  # 开头几个点无预测。
        )  # 结束：点数不足则 None。
    return actual, quality  # 趋势 + 质量分项。


def build_dashboard_payload(quality: dict[str, Any], model_mode: str) -> dict[str, Any]:  # 一块连接读完所有段。
    """Build every dashboard section from the same read-only business database."""  # 一块连接读完所有段。
    path = configured_database_path()  # 统一库。
    try:  # 尝试：只读连接，离开 with 即关。
        with closing(_connect_readonly(path)) as database:  # 只读连接，离开 with 即关。
            _validate_schema(database)  # 先验表结构。
            snapshot_version = _snapshot_version(database)  # S1-A：联动写版本（旧库回退 0）。
            station_rows = _rows(  # 站左连桩，后面按站聚合状态。
                database,  # 当前连接。
                """
                SELECT station.id,station.name,station.price_yuan_per_kwh,
                       pile.id AS pile_id,pile.status
                FROM station_info AS station
                LEFT JOIN pile_info AS pile ON pile.station_id=station.id
                ORDER BY station.id,pile.id
                """,  # 无桩站点仍出现，pile_id 为 NULL。
            )  # 结束：站左连桩，后面按站聚合状态。
            pile_rows = _rows(database, "SELECT id,status FROM pile_info ORDER BY id")  # 全桩状态。
            order_rows = _rows(  # 订单全量，教学数据量可接受。
                database,  # 连接。
                """
                SELECT id,status,energy_kwh,amount_yuan,started_at,ended_at,created_at
                FROM charge_order ORDER BY created_at,id
                """,  # 按创建时间稳定排序。
            )  # 结束：订单全量，教学数据量可接受。
            telemetry_rows = _rows(  # 遥测左连桩和站，带 pile_exists。
                database,  # 连接。
                """
                SELECT telemetry.id,telemetry.pile_id,telemetry.recorded_at,
                       telemetry.temperature_c,telemetry.voltage_v,
                       telemetry.current_a,telemetry.power_w,
                       telemetry.energy_kwh,telemetry.source_mode,
                       pile.pile_sn,pile.status AS pile_status,
                       station.name AS station_name,
                       CASE WHEN pile.id IS NULL THEN 0 ELSE 1 END AS pile_exists
                FROM pile_telemetry AS telemetry
                LEFT JOIN pile_info AS pile ON pile.id=telemetry.pile_id
                LEFT JOIN station_info AS station ON station.id=pile.station_id
                ORDER BY telemetry.id
                """,  # 必须按 id 升序做相邻差分。
            )  # 结束：遥测左连桩和站，带 pile_exists。
            alarm_rows = _rows(  # 最近 10 条告警。
                database,  # 连接。
                """
                SELECT alarm.id,alarm.alarm_type,alarm.severity,alarm.content,
                       alarm.occurred_at,alarm.status,pile.pile_sn
                FROM alarm_event AS alarm
                JOIN pile_info AS pile ON pile.id=alarm.pile_id
                ORDER BY alarm.occurred_at DESC,alarm.id DESC LIMIT 10
                """,  # 内连桩，无主数据的告警不展示。
            )  # 结束：最近 10 条告警。
            ticket_rows = _rows(  # 最近 10 张工单。
                database,  # 连接。
                """
                SELECT ticket.id,ticket.status,ticket.assignee,ticket.updated_at,
                       alarm.id AS alarm_id,alarm.alarm_type,pile.pile_sn
                FROM maintenance_ticket AS ticket
                JOIN alarm_event AS alarm ON alarm.id=ticket.alarm_id
                JOIN pile_info AS pile ON pile.id=alarm.pile_id
                ORDER BY ticket.updated_at DESC,ticket.id DESC LIMIT 10
                """,  # 工单→告警→桩。
            )  # 结束：最近 10 张工单。

        now = datetime.now(PROJECT_TIMEZONE)  # 关闭连接后再算，减少锁持有。
        today_start = now.replace(hour=0, minute=0, second=0, microsecond=0)  # 今日零点。
        status = _status_counts(pile_rows)  # 五态。
        total_piles = len(pile_rows)  # 分母：电桩总数。
        online_piles = total_piles - status["OFFLINE"]  # 非离线。
        serviceable = status["IDLE"] + status["RESERVED"] + status["CHARGING"]  # 使用率分母不含故障离线。
        online_rate = online_piles / total_piles * 100 if total_piles else 0.0  # 无桩则 0。
        use_rate = status["CHARGING"] / serviceable * 100 if serviceable else 0.0  # 无可服务桩则 0。
        today_services, today_revenue, revenue_trend = _order_summary(  # 今日服务与营收。
            order_rows, today_start  # 全量订单 + 今日边界。
        )  # 结束：今日服务与营收。
        energy_trend, telemetry_quality = _telemetry_summary(telemetry_rows)  # 小时用电与质量。

        # telemetry_rows 按自增 id 排序；同一电桩后出现的记录就是最新成功入库样本。
        latest_by_pile: dict[int, sqlite3.Row] = {}  # 桩 → 最后一行。
        for row in telemetry_rows:  # 后写覆盖先写。
            latest_by_pile[int(row["pile_id"])] = row  # 最新样本。
        latest_telemetry = []  # 大屏 TelemetryPanel。
        for pile_id, row in sorted(latest_by_pile.items()):  # 按桩号稳定。
            recorded_at = _parse_time(row["recorded_at"])  # 最新时间。
            age_seconds = (  # 距现在秒数。
                max(0, int((now - recorded_at.astimezone(PROJECT_TIMEZONE)).total_seconds()))  # 负值夹 0。
                if recorded_at  # 时间合法。
                else None  # 无法算新鲜度。
            )  # 结束：距现在秒数。
            power_w = _as_int(row["power_w"])  # 瓦。
            latest_telemetry.append(  # 一行实时遥测。
                {
                    "pileId": str(pile_id),  # 字符串 id。
                    "pileSn": row["pile_sn"] or f"PILE-{pile_id}",  # 无 SN 则拼。
                    "stationName": row["station_name"] or "未知站点",  # 未知站。
                    "status": row["pile_status"] or "UNKNOWN",  # 桩状态。
                    "temperatureC": _as_float(row["temperature_c"]),  # 温度。
                    "voltageV": _as_float(row["voltage_v"]),  # 电压。
                    "currentA": _as_float(row["current_a"]),  # 电流。
                    "powerW": power_w,  # 瓦。
                    "powerKw": round(power_w / 1000, 2),  # 千瓦展示。
                    "energyKwh": str(row["energy_kwh"] or "0.00"),  # 累计电表原文字符串。
                    "recordedAt": _display_timestamp(row["recorded_at"]),  # 到秒。
                    "sourceMode": str(row["source_mode"] or "SIMULATED"),  # 来源。
                    "fresh": age_seconds is not None and age_seconds <= 15,  # 15 秒内算新鲜（贴近离线阈值）。
                    "ageSeconds": age_seconds,  # 供前端着色。
                }  # 结束：一行实时遥测。
            )  # 结束：结束：一行实时遥测。

        stations: dict[int, dict[str, Any]] = {}  # 站 id → 聚合。
        for row in station_rows:  # 站×桩展开行。
            station_id = int(row["id"])  # 站主键。
            current = stations.setdefault(  # 第一次见到该站则建骨架。
                station_id,  # 键。
                {
                    "id": str(station_id),  # 字符串 id。
                    "name": row["name"],  # 站名。
                    "price": row["price_yuan_per_kwh"],  # 单价。
                    "counts": defaultdict(int),  # 各状态桩数。
                },  # 结束：第一次见到该站则建骨架。
            )  # 结束：结束：第一次见到该站则建骨架。
            if row["pile_id"] is not None:  # 该行绑定了一台桩。
                current["counts"][str(row["status"])] += 1  # 按桩状态计数。
        ranking = []  # 使用率排行。
        recommendations = []  # 按可用桩的简单推荐（与 /recommend 教学接口不同）。
        for station in stations.values():  # 每个站一条。
            counts = station["counts"]  # 五态。
            available = counts["IDLE"]  # 空闲桩。
            station_serviceable = (  # 本站可服务桩。
                counts["IDLE"] + counts["RESERVED"] + counts["CHARGING"]  # 同全局口径。
            )  # 结束：本站可服务桩。
            utilization = (  # 本站使用率。
                round(counts["CHARGING"] / station_serviceable * 100)  # 百分整数。
                if station_serviceable  # 有可服务桩。
                else 0  # 否则 0。
            )  # 结束：本站使用率。
            ranking.append(  # 排行项。
                {
                    "id": station["id"],  # 站 id。
                    "name": station["name"],  # 名。
                    "utilization": utilization,  # 使用率。
                    "available": available,  # 空闲数。
                }  # 结束：排行项。
            )  # 结束：结束：排行项。
            recommendations.append(  # 大屏推荐卡片。
                {
                    "name": station["name"],  # 站名。
                    "wait": counts["CHARGING"] * 3,  # 教学：充电中桩数×3 分钟。
                    "distance": 0.0,  # 实时路径无定位，距离填 0。
                    "available": available,  # 空闲。
                    "reason": (  # 一句中文理由。
                        f"实时可用{available}桩，当前使用率{utilization}%，"  # 可用与使用率。
                        f"价格{station['price']}元/kWh"  # 单价。
                    ),  # 结束：一句中文理由。
                }  # 结束：大屏推荐卡片。
            )  # 结束：结束：大屏推荐卡片。
        ranking.sort(key=lambda item: (-item["utilization"], item["id"]))  # 使用率降序，id 升序。
        recommendations.sort(key=lambda item: (-item["available"], item["wait"]))  # 空闲多、等待少优先。

        anomalies = sum(  # 质量异常合计（不含 simulated 计数）。
            telemetry_quality[name]  # 分项。
            for name in (  # 计入拒绝的检查项。
                "missing", "invalid_time", "time_backward", "energy_backward",  # 值与时间问题。
                "duplicate", "unknown_relation",  # 重复与脏关联。
            )  # 结束：计入拒绝的检查项。
        )  # 结束：质量异常合计（不含 simulated 计数）。
        raw = len(telemetry_rows)  # 遥测原始条数。
        valid = max(0, raw - anomalies)  # 有效条数下限 0。
        rate = valid / raw * 100 if raw else 100.0  # 无遥测时有效率显示 100。
        open_alarms = sum(  # 未关闭告警。
            1 for row in alarm_rows if row["status"] in ("OPEN", "ACKNOWLEDGED")  # 打开或已确认。
        )  # 结束：未关闭告警。
        charging_orders = sum(1 for row in order_rows if row["status"] == "CHARGING")  # 充电中订单。
        timestamps = [  # 覆盖区间候选。
            _parse_time(row["recorded_at"]) for row in telemetry_rows  # 遥测时间。
        ] + [_parse_time(row["started_at"]) for row in order_rows]  # 订单开始时间。
        timestamps = [value for value in timestamps if value is not None]  # 去掉解析失败。
        coverage_start = min(timestamps).isoformat() if timestamps else None  # 最早。
        coverage_end = max(timestamps).isoformat() if timestamps else None  # 最晚。
        forecasts = [  # 有规则预测的点。
            item["forecast"] for item in energy_trend if item["forecast"] is not None  # 非空 forecast。
        ]  # 结束：有规则预测的点。
        mae = (  # 规则预测 MAE，Decimal 累加。
            sum(  # 绝对误差和。
                abs(Decimal(str(item["actual"])) - Decimal(str(item["forecast"])))  # |实际-预测|。
                for item in energy_trend  # 每小时。
                if item["forecast"] is not None  # 有预测才比。
            ) / Decimal(len(forecasts))  # 平均。
            if forecasts  # 有样本。
            else None  # 无 MAE。
        )  # 结束：规则预测 MAE，Decimal 累加。
        forecast_mode = "baseline" if forecasts else "insufficient"  # 有点则基线模式。

        return {  # 大屏一次拉取的完整载荷。
            "state": "normal" if (station_rows or pile_rows or order_rows) else "empty",  # 完全没主数据则 empty。
            "region": "教学模拟运营区",  # 固定区域名。
            "dataMode": "SIMULATED",  # 第一阶段教学模拟。
            "datasetName": path.name,  # 库文件名。
            "datasetVersion": "sqlite-stage1-v2",  # 契约版本。
            "snapshotVersion": snapshot_version,  # S1-A 联动契约：写版本，Web 据此丢弃旧响应。
            "snapshotAtUtc": now.isoformat(timespec="seconds"),  # 快照读取时刻（UTC）。
            "coverage": f"{_display_time(coverage_start)} — {_display_time(coverage_end)}",  # 覆盖文案。
            "updatedAt": now.isoformat(timespec="seconds"),  # 本次投影时间。
            "dataTimestamp": _display_time(coverage_end),  # 最新业务时间。
            "stale": False,  # 读成功即不标过期。
            "sourceStatus": "connected",  # 已连上统一库。
            "refreshIntervalSeconds": 3,  # 前端轮询间隔。
            "metricDefinitions": {  # 答辩用口径，写进 JSON 避免口误。
                "onlineRate": "非OFFLINE电桩数/电桩总数",  # 在线率。
                "utilization": "CHARGING/(IDLE+RESERVED+CHARGING)",  # 使用率。
                "revenue": "Asia/Shanghai自然日内FINISHED订单金额",  # 营收。
                "energy": "同桩相邻生命周期累计电表读数的有效增量",  # 用电。
            },  # 结束：答辩用口径，写进 JSON 避免口误。
            "metrics": [  # 顶部 KPI。
                {"label": "站点总数", "value": str(len(stations)), "unit": "座", "tone": "green"},  # 站。
                {"label": "电桩总数", "value": str(total_piles), "unit": "台", "tone": "blue"},  # 桩。
                {"label": "设备在线率", "value": f"{online_rate:.1f}", "unit": "%", "tone": "cyan"},  # 在线率。
                {"label": "充电中订单", "value": str(charging_orders), "unit": "单", "tone": "purple"},  # 进行中订单。
                {"label": "今日服务量", "value": str(today_services), "unit": "次", "tone": "green"},  # 今日开工。
                {"label": "今日营收", "value": _money(today_revenue), "unit": "元", "tone": "purple"},  # 今日金额。
                {"label": "即时使用率", "value": f"{use_rate:.1f}", "unit": "%", "tone": "blue"},  # 使用率。
                {"label": "未处理告警", "value": str(open_alarms), "unit": "条", "tone": "red"},  # 打开/已确认。
            ],  # 结束：顶部 KPI。
            "pileStatus": [  # 五态条形。
                {"status": name, "count": status[name]}  # 状态名与台数。
                for name in ("IDLE", "RESERVED", "CHARGING", "FAULT", "OFFLINE")  # 固定顺序。
            ],  # 结束：五态条形。
            "telemetry": latest_telemetry,  # 每桩最新电气量。
            "load": energy_trend,  # 与 energyTrend 同一数组，兼容旧前端字段。
            "energyTrend": energy_trend,  # 小时实际+规则预测。
            "revenueTrend": revenue_trend,  # 近 7 日营收。
            "forecastMeta": {  # 曲线旁说明。
                "method": "3点移动平均规则估算" if forecasts else "数据不足",  # 方法名。
                "mode": forecast_mode,  # baseline/insufficient。
                "sampleCount": len(energy_trend),  # 小时点数。
                "updatedAt": now.isoformat(timespec="seconds"),  # 计算时刻。
                "maeKwh": _money(mae) if mae is not None else None,  # MAE 展示。
            },  # 结束：曲线旁说明。
            "ranking": ranking,  # 站使用率榜。
            "alarms": [  # 告警列表。
                {
                    "id": f"ALARM-{row['id']}",  # 展示 id。
                    "type": row["alarm_type"],  # 类型。
                    "severity": _alarm_severity(str(row["severity"])),  # 中文严重度。
                    "status": row["status"],  # OPEN 等。
                    "message": f"{row['pile_sn']} {row['content']}（{row['status']}）",  # 一句描述。
                    "time": _display_time(row["occurred_at"]),  # 发生时间。
                }  # 结束：告警列表。
                for row in alarm_rows  # 最多 10 条。
            ],  # 结束：大屏一次拉取的完整载荷。
            "tickets": [  # 工单列表。
                {
                    "id": f"TICKET-{row['id']}",  # 展示 id。
                    "alarmId": f"ALARM-{row['alarm_id']}",  # 关联告警。
                    "pileSn": row["pile_sn"],  # 桩号。
                    "type": row["alarm_type"],  # 告警类型。
                    "assignee": row["assignee"] or "未分配",  # 处理人。
                    "status": row["status"],  # 工单状态。
                    "updatedAt": _display_time(row["updated_at"]),  # 更新时间。
                }  # 结束：工单列表。
                for row in ticket_rows  # 最多 10 条。
            ],  # 结束：最多 10 条。
            "recommendations": recommendations,  # 按可用桩排序。
            "quality": {  # 遥测质量面板；参数 quality 来自离线链，大屏主用库内 checks。
                "raw": raw,  # 原始遥测条数。
                "valid": valid,  # 粗算有效。
                "rejected": anomalies,  # 异常合计。
                "rate": round(rate, 2),  # 有效率百分。
                "batch": "sqlite-live-read",  # 标明是活库只读，不是 CSV 批次。
                "license": "统一SQLite业务库；遥测来源按source_mode核验",  # 口径说明。
                "checks": telemetry_quality,  # 分项。
            },  # 结束：遥测质量面板；参数 quality 来自离线链，大屏主用库内 checks。
            "modelMode": model_mode if forecasts else "baseline",  # 无曲线时不宣称 trained。
        }  # 大屏完整 JSON 载荷结束。
    except DashboardDatabaseError:  # 已是面向用户的错误。
        raise  # 原样给 api._dashboard_error。
    except sqlite3.Error as error:  # 查询期 sqlite 异常。
        raise DashboardDatabaseError(f"SQLite只读查询失败：{error}") from error  # 包装。

# 【文件讲解·答辩用】
# 功能作用：把成员1统一 SQLite 投影成大屏 JSON：KPI、桩状态、实时遥测、小时用电、营收、告警工单、站推荐。
# 技术原理：file: URI mode=ro + PRAGMA query_only；累计电表相邻差分得用电；营收只加 FINISHED；今日边界用 Asia/Shanghai。
# 代码思路：验 schema → 一次读六类表 → 关连接后纯 Python 聚合 → 质量分项与规则 3 点预测 → 返回固定字段。
# 与其他文件关系：api.dashboard 唯一调用入口；configured_database_path 同时给 simulator_control 校准电表；不写任何业务 UPDATE。
# 答辩时可以怎么讲：大屏数字都能指回表和口径；用电不是把 energy_kwh 直接加总（那是累计表），而是同桩相邻读数的有效增量。
