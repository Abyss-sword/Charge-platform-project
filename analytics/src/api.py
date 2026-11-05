# 第一阶段兼容 FastAPI 入口：/dashboard 只读统一库；第二阶段正式入口已迁移到 src.flask_api。
# /health /forecast /recommend 供第二阶段分析接口；第一阶段大屏主路径是 GET /dashboard。
"""HTTP boundary for member 3 analytics; all business data access is read-only."""  # 模块说明：成员3 HTTP 边界，业务库一律只读。

from __future__ import annotations  # 延迟注解，兼容 3.10。

import csv  # 读取 curated hourly_station.csv。
import json  # 读取 metadata.json、evaluation.json、staging 下的 accepted/quality。
import os  # CHARGE_ANALYTICS_RUNTIME / CHARGE_MODEL_DIR 隔离测试目录。
from datetime import datetime, timedelta, timezone  # 健康检查时间戳与 forecast 整点序列。
from pathlib import Path  # 定位 analytics 根与 runtime。
from typing import Any  # 小时行与评测字典。

import joblib  # 读取 train.py 写出的时域估计器。
from fastapi import FastAPI, HTTPException, Query  # 路由、400/409、查询参数校验。
from pydantic import BaseModel, Field  # 场景切换 POST 体。

from .capacity import CAPACITY_NOTE, CAPACITY_SOURCE, STATION_CAPACITY, estimate_free_piles  # 空闲桩公式。
from .model import predict_load  # 已发布估计器打分。
from .recommend import StationCandidate, rank_stations  # /recommend 教学推荐。
from .simulator_control import catalog as simulator_catalog  # 场景目录，声明不写库。
from .simulator_control import snapshot as simulator_snapshot  # 忙闲状态。
from .simulator_control import start_scenario as start_simulator_scenario  # 只拉起子进程。
from .sqlite_dashboard import (  # 大屏投影：只读 SQLite。
    DashboardDatabaseError,  # 库不存在或结构不兼容。
    build_dashboard_payload,  # 组装大屏 JSON。
    configured_database_path,  # CHARGE_PLATFORM_DB 或默认 runtime 库路径。
)  # 只读大屏三件套导入结束。

ANALYTICS_DIR = Path(__file__).resolve().parents[1]  # analytics 包根，models/evaluation.json 在这里。
app = FastAPI(title="充电平台分析服务", version="2.1.0")  # ASGI 应用，uvicorn 加载 src.api:app。

WEIGHTS_TRAINED = {"distance": "0.30", "wait": "0.30", "price": "0.20", "predictedLoad": "0.20"}  # 有预测时的推荐权重。
WEIGHTS_BASELINE = {"distance": "0.40", "wait": "0.35", "price": "0.25"}  # 缺预测时的降级权重。


def _runtime_dir() -> Path:  # 离线流水线根；测试可指向临时目录，避免误读本机 public-v1。
    override = os.environ.get("CHARGE_ANALYTICS_RUNTIME")  # 可选覆盖。
    return Path(override) if override else ANALYTICS_DIR.parent / "runtime" / "analytics"  # 默认仓库 runtime。


def _model_dir() -> Path:  # evaluation.json 与 joblib；测试可指向空目录模拟模型缺失。
    override = os.environ.get("CHARGE_MODEL_DIR")  # 可选覆盖。
    return Path(override) if override else ANALYTICS_DIR / "models"  # 默认 analytics/models。


def _metadata() -> dict[str, object]:  # 离线 curated 覆盖区间；优先公开聚合批次，没有再回落教学模拟。
    runtime = _runtime_dir()  # 当前 runtime。
    for version in ("public-v1", "sim-v2"):  # D8 正式 curated 在 public-v1；demo_pipeline 仍写 sim-v2。
        path = runtime / "curated" / version / "metadata.json"  # write_curated 产出。
        if not path.exists():  # 该版本尚未跑。
            continue  # 试下一个。
        meta = json.loads(path.read_text(encoding="utf-8"))  # 读覆盖与模式。
        return {  # 驼峰字段给前端。
            "dataMode": meta["data_mode"],  # PUBLIC_AGGREGATED 或 SIMULATED。
            "datasetName": meta["dataset_name"],  # 数据集名。
            "datasetVersion": meta["dataset_version"],  # public-v1 / sim-v2。
            "coverageStart": meta["coverage_start"],  # 最早小时。
            "coverageEnd": meta["coverage_end"],  # 最晚小时。
            "sampleCount": meta["row_count"],  # curated 行数。
        }  # 已有流水线元数据。
    return {  # 未跑流水线时的占位，避免 /health 500。
        "dataMode": "SIMULATED",  # 教学默认模拟。
        "datasetName": "member3-simulated-charge-events",  # 与示例清单一致。
        "datasetVersion": "sim-v2",  # 约定版本号。
        "coverageStart": None,  # 尚无覆盖。
        "coverageEnd": None,  # 尚无覆盖。
        "sampleCount": 0,  # 无样本。
    }  # 占位元数据结束。


def _evaluation_bundle() -> dict[str, Any]:  # 读评测 JSON；没有则空字典。
    path = _model_dir() / "evaluation.json"  # 只读 train.py 的正式评测；模拟链不得写这里。
    if not path.exists():  # 没跑过训练。
        return {}  # 空。
    return json.loads(path.read_text(encoding="utf-8"))  # 含 evaluations。


def _horizon_modes() -> dict[str, str]:  # 三个时域各自 trained/baseline，互不顶替。
    modes = {"1": "baseline", "6": "baseline", "24": "baseline"}  # 缺评测则全基线。
    for item in _evaluation_bundle().get("evaluations", []):  # 逐时域。
        horizon = str(item.get("horizon_hours"))  # 1/6/24。
        if horizon in modes:  # 只收允许时域。
            modes[horizon] = str(item.get("selected_mode") or "baseline")  # 缺字段当基线。
    return modes  # 给 /health。


def _active_model_mode() -> str:  # 任一评测时域打赢基线则 trained，否则 baseline。
    return "trained" if "trained" in set(_horizon_modes().values()) else "baseline"  # 总徽章。


def _horizon_record(horizon: int) -> dict[str, Any] | None:  # 取出某一个时域的评测行。
    for item in _evaluation_bundle().get("evaluations", []):  # 扫描。
        if int(item.get("horizon_hours") or 0) == horizon:  # 匹配。
            return item  # 找到。
    return None  # 没有该时域。


def _hourly_rows() -> list[dict[str, Any]]:  # 优先 public-v1，其次教学 sim-v2。
    runtime = _runtime_dir()  # 当前 runtime。
    for version in ("public-v1", "sim-v2"):  # 正式分析表优先。
        path = runtime / "curated" / version / "hourly_station.csv"  # 小时特征。
        if not path.exists():  # 没有该版本。
            continue  # 下一个。
        with path.open("r", encoding="utf-8", newline="") as stream:  # 文本 CSV。
            rows = []  # 数值化后的行。
            for row in csv.DictReader(stream):  # 按表头。
                item = dict(row)  # 拷贝。
                item["load_kwh"] = float(item["load_kwh"])  # 负荷。
                item["available_piles"] = int(float(item.get("available_piles") or 0))  # 教学桩数。
                item["hour"] = int(item["hour"])  # 小时。
                item["weekday"] = int(item["weekday"])  # 星期。
                item["is_holiday"] = int(item.get("is_holiday") or 0)  # 节假日。
                rows.append(item)  # 收行。
            return rows  # 已有小时表。
    return []  # 未跑流水线。


def _accepted_rows() -> list[dict[str, object]]:  # 取 staging 下最新一批 accepted.json。
    candidates = sorted((_runtime_dir() / "staging").glob("*/accepted.json"))  # 各批次通过记录。
    return (  # 没有批次则空列表。
        json.loads(candidates[-1].read_text(encoding="utf-8"))  # 最新一批。
        if candidates  # 至少一份。
        else []  # 未跑流水线。
    )  # 三元表达式结束。


def _quality() -> dict[str, object]:  # 最新一批 quality.json；没有则全 0。
    candidates = sorted((_runtime_dir() / "staging").glob("*/quality.json"))  # 各批次质量文件。
    if not candidates:  # 离线链未跑。
        return {  # 占位，build_dashboard_payload 主要用库内遥测质量。
            "input_count": 0,  # 无输入。
            "accepted_count": 0,  # 无通过。
            "rejected_count": 0,  # 无拒绝。
            "valid_rate": 0.0,  # 有效率 0。
        }  # 空质量占位。
    return json.loads(candidates[-1].read_text(encoding="utf-8"))  # 最新对账结果。


def _shift_hour(value: str, hours: int) -> str:  # ISO 小时加减，保留原有偏移。
    return (datetime.fromisoformat(value) + timedelta(hours=hours)).isoformat()  # 目标窗口。


def _load_estimator(horizon: int) -> dict[str, Any] | None:  # 读该时域 joblib；损坏或缺失返回 None。
    path = _model_dir() / f"load_h{horizon}.joblib"  # train.py 仅在 selected_mode=trained 时写入。
    if not path.exists():  # 文件不在。
        return None  # 调用方降级基线。
    try:  # 反序列化。
        payload = joblib.load(path)  # 含 estimator 与 station_codes。
    except Exception:  # 损坏文件不得让接口 500。
        return None  # 当缺失处理。
    if not isinstance(payload, dict) or "estimator" not in payload or "station_codes" not in payload:  # 结构不对。
        return None  # 降级。
    return payload  # 可推理。


def _forecast_from_curated(horizon: int, rows: list[dict[str, Any]]) -> dict[str, object]:  # 站点负荷 + 容量空闲桩；时域模式互不顶替。
    record = _horizon_record(horizon)  # 该时域评测。
    published = str(record.get("selected_mode") or "baseline") if record else "baseline"  # 发布模式。
    bundle = _load_estimator(horizon) if published == "trained" else None  # 未发布为 trained 时不读模型。
    if published == "trained" and bundle is not None:  # 有评测且文件在。
        mode = "trained"  # 使用树模型。
        method = "HistGradientBoostingRegressor"  # 算法名。
        degrade_reason = None  # 不降级。
    elif published == "trained":  # 评测说 trained 但文件没了。
        mode = "baseline"  # 服务仍启动，改发基线。
        method = "昨日同窗口基线"  # 方法名。
        degrade_reason = "MODEL_FILE_MISSING"  # 原因码。
    else:  # 该时域本来就发布基线，或还没训练。
        mode = "baseline"  # 基线。
        method = "昨日同窗口基线"  # 方法名。
        degrade_reason = "MODEL_NOT_BETTER_THAN_BASELINE" if record else "MODEL_NOT_TRAINED"  # 有评测则未打赢，否则未训练。
    last_hour = max(str(row["hour_start"]) for row in rows)  # curated 最晚小时当作预测起点。
    lookup = {(str(row["station_id"]), str(row["hour_start"])): float(row["load_kwh"]) for row in rows}  # (站,小时)→负荷。
    station_ids = sorted({str(row["station_id"]) for row in rows})  # 稳定顺序。
    by_station = {station: sorted([row for row in rows if str(row["station_id"]) == station], key=lambda item: str(item["hour_start"])) for station in station_ids}  # 分站序列。

    def station_load(station_id: str, step: int) -> tuple[float, str]:  # 从起点往后 step 小时的负荷与模式。
        series = by_station[station_id]  # 该站小时行。
        origin = series[-1]  # 最新特征行，特征时刻不晚于起点。
        target = _shift_hour(str(origin["hour_start"]), step)  # 目标窗口。
        yesterday = lookup.get((station_id, _shift_hour(target, -24)))  # 昨日同窗口。
        moving = sum(float(item["load_kwh"]) for item in series[-3:]) / min(3, len(series))  # 没有昨天时用 3 点均值。
        load = yesterday if yesterday is not None else moving  # 基线值。
        point_mode = "baseline"  # 默认基线。
        if mode == "trained" and step == horizon and bundle is not None:  # 只在目标时域用树模型，前面各步仍用基线填图。
            load = predict_load(bundle["estimator"], origin, bundle["station_codes"])  # 已发布估计器。
            point_mode = "trained"  # 该点来自模型。
        return max(0.0, float(load)), point_mode  # 物理负荷不得为负；容量估算和返回值使用同一安全值。

    values = []  # 平台合计序列，长度 = horizon，兼容旧测试。
    for step in range(1, horizon + 1):  # 未来 1…H 小时。
        total = sum(station_load(station_id, step)[0] for station_id in station_ids)  # 三站相加。
        values.append({  # 一个合计点。
            "time": _shift_hour(last_hour, step),  # 目标小时。
            "loadKwh": f"{total:.2f}",  # 两位小数。
            "modelMode": "trained" if mode == "trained" and step == horizon else "baseline",  # 仅终点可标 trained。
        })  # 合计点结束。
    stations = []  # 逐站目标时域。
    for station_id in station_ids:  # 每个内部站。
        load, point_mode = station_load(station_id, horizon)  # 目标窗口负荷。
        spec = STATION_CAPACITY.get(station_id, {"name": f"站点{station_id}", "total_piles": int(by_station[station_id][-1]["available_piles"]), "nominal_kw_per_pile": 60.0})  # 教学表，未知站用小时表桩数。
        free = estimate_free_piles(load, int(spec["total_piles"]), float(spec["nominal_kw_per_pile"]))  # 容量公式。
        stations.append({  # 站级预测。
            "stationId": station_id,  # 内部站号。
            "name": spec["name"],  # 展示名。
            "loadKwh": f"{load:.2f}",  # 负荷。
            "estimatedFree": free["estimatedFree"],  # 可能为 None。
            "capacityReason": free["reason"],  # 非法参数时的中文。
            "modelMode": point_mode,  # 该站该时域。
        })  # 站级结束。
    return {  # 分析预测成功体。
        "status": "ok",  # 有值。
        "horizonHours": horizon,  # 时域。
        "modelMode": mode,  # 本时域发布模式。
        "method": method,  # 中文/算法名。
        "sampleCount": len(rows),  # curated 行数。
        "degraded": mode != "trained",  # 基线即降级。
        "degradeReason": degrade_reason,  # 原因或 None。
        "predictedRange": {"start": _shift_hour(last_hour, 1), "end": _shift_hour(last_hour, horizon)},  # 本请求覆盖的小时区间。
        "originHour": last_hour,  # 预测起点。
        "capacity": {  # 参数来源，禁止省略。
            "source": CAPACITY_SOURCE,  # 教学表说明。
            "note": CAPACITY_NOTE,  # 非排队承诺。
            "stations": {station_id: {"totalPiles": STATION_CAPACITY[station_id]["total_piles"], "nominalKwPerPile": STATION_CAPACITY[station_id]["nominal_kw_per_pile"]} for station_id in station_ids if station_id in STATION_CAPACITY},  # 逐站参数。
        },  # 容量块结束。
        "stations": stations,  # 逐站。
        "values": values,  # 合计序列。
        "horizonModes": _horizon_modes(),  # 三个时域各自模式，避免拿别的时域顶替。
        **_metadata(),  # 来源/版本/覆盖。
    }  # 分析预测结束。


@app.get("/health")  # 探活路由。
def health() -> dict[str, object]:  # 模型模式、库是否可读、只读声明、离线元数据。
    mode = _active_model_mode()  # 总徽章。
    horizon_modes = _horizon_modes()  # 逐时域。
    database_path = configured_database_path()  # 统一库路径，不新建第二份库。
    return {  # 探活 JSON。
        "status": "ok",  # 进程活着。
        "modelMode": mode,  # 给前端徽章。
        "horizonModes": horizon_modes,  # 1/6/24 各自 trained 或 baseline。
        "degraded": mode == "baseline",  # 三个时域都没打赢才算总降级。
        "degradeReason": "MODEL_NOT_BETTER_THAN_BASELINE" if mode == "baseline" else None,  # 总原因码。
        "databaseReadable": database_path.is_file(),  # 文件是否存在，不代表 schema 合法。
        "databasePath": str(database_path),  # 便于答辩核对 CHARGE_PLATFORM_DB。
        "databaseAccess": "read-only",  # 合同：本服务不写业务库。
        "updatedAt": datetime.now(timezone.utc).isoformat(),  # UTC 探活时间。
        **_metadata(),  # 拼上 dataMode 等。
    }  # /health 结束。


@app.get("/forecast")  # 站点负荷预测：有 curated 走 D9 分析链，否则回落 SQLite 3 点均值。
def forecast(horizon: int = Query(1, ge=1)) -> dict[str, object]:  # 只允许 1/6/24。
    if horizon not in {1, 6, 24}:  # 与离线评测时域对齐。
        return {  # 不抛 400，返回说明对象。
            "status": "unsupported_horizon",  # 不支持的时域。
            "supportedHorizons": [1, 6, 24],  # 允许值。
            **health(),  # 带上探活字段。
        }  # 不支持时域的返回体。
    curated = _hourly_rows()  # public-v1 优先。
    if curated:  # 有小时表则不拿业务库电表差分冒充分析预测。
        return _forecast_from_curated(horizon, curated)  # 站点负荷 + 空闲桩。
    try:  # 读库算 energyTrend，库坏则降级。
        dashboard_data = build_dashboard_payload(_quality(), _active_model_mode())  # 只读库算 energyTrend。
    except DashboardDatabaseError as error:  # 库缺失或结构不对。
        return {  # 降级空序列。
            "status": "unavailable",  # 不可用。
            "message": str(error),  # 中文原因。
            "values": [],  # 无预测点。
            **health(),  # 探活。
        }  # 库不可用返回体。
    actual = [float(item["actual"]) for item in dashboard_data["energyTrend"]]  # 小时实际用电。
    if len(actual) < 2:  # 点太少无法做移动平均。
        return {  # 数据不足时的空预测。
            "status": "insufficient_data",  # 数据不足。
            "horizonHours": horizon,  # 回显请求时域。
            "modelMode": "baseline",  # 强制基线。
            "method": "数据不足",  # 中文方法名。
            "sampleCount": len(actual),  # 实际点数。
            "degraded": True,  # 降级。
            "degradeReason": "INSUFFICIENT_DATA",  # 原因码。
            "values": [],  # 空。
            **_metadata(),  # 离线元数据。
        }  # 不足数据返回体。
    estimate = sum(actual[-3:]) / min(3, len(actual))  # 最近至多 3 点均值。
    now = datetime.now(timezone(timedelta(hours=8))).replace(  # 东八区当前整点。
        minute=0, second=0, microsecond=0  # 截分秒。
    )  # replace 结束。
    values = [  # 未来 horizon 个整点，负荷都填同一个均值。
        {  # 单个预测点。
            "time": (now + timedelta(hours=index + 1)).isoformat(),  # 下一小时起。
            "loadKwh": f"{estimate:.2f}",  # 两位小数。
            "modelMode": "baseline",  # 规则基线，不是树模型。
        }  # 预测点字段结束。
        for index in range(horizon)  # 1、6 或 24 个点。
    ]  # 预测序列结束。
    return {  # 规则基线预测成功体。
        "status": "ok",  # 有值。
        "horizonHours": horizon,  # 时域。
        "modelMode": "baseline",  # 在线接口固定规则估算。
        "method": "3点移动平均规则估算",  # 与大屏 forecastMeta 口径一致。
        "sampleCount": len(actual),  # 用于估算的历史点数。
        "degraded": True,  # 明确降级，避免假装在线模型。
        "degradeReason": "RULE_BASELINE",  # 原因码。
        "values": values,  # 预测序列。
        **_metadata(),  # 离线覆盖信息。
    }  # /forecast 成功体结束。


@app.get("/recommend")  # 教学推荐路由，不返回本机路径或个人标识。
def recommend(model_available: bool = True) -> dict[str, object]:  # 三站固定距离等待价格，调用 rank_stations。
    load_source = "none"  # 缺省无预测。
    if not model_available:  # 查询参数强制降级。
        loads: list[float | None] = [None, None, None]  # 三站都没有负荷比。
    else:  # 尽量用 curated 最近一小时负荷换算 0~1 比。
        rows = _hourly_rows()  # 可能为空。
        if rows:  # 有分析表。
            loads = []  # 三站比。
            for station_id in ("1", "2", "3"):  # 与候选顺序一致。
                series = sorted([row for row in rows if str(row["station_id"]) == station_id], key=lambda item: str(item["hour_start"]))  # 该站。
                spec = STATION_CAPACITY[station_id]  # 容量。
                last = float(series[-1]["load_kwh"]) if series else 0.0  # 无行则 0。
                denom = spec["total_piles"] * spec["nominal_kw_per_pile"]  # 满载功率。
                loads.append(min(1.0, max(0.0, last / denom)))  # 夹到 [0,1]。
            load_source = "curated-last-hour"  # 来源摘要。
        else:  # 没有小时表时仍给可演示的教学负荷比，并在摘要里标明。
            loads = [.42, .68, .36]  # 固定夹具，不是库里的人。
            load_source = "teaching-fixture"  # 摘要。
    stations = [  # 演示候选，不含手机号或文件路径。
        StationCandidate("1", "东软北门站", 1.8, 8, "1.36", loads[0], 4),  # 站1。
        StationCandidate("2", "软件园站", 0.9, 14, "1.28", loads[1], 3),  # 站2。
        StationCandidate("3", "浑南广场站", 2.6, 3, "1.18", loads[2], 2),  # 站3。
    ]  # 三站候选结束。
    ranked = rank_stations(stations)  # 加权排序 Top3。
    mode = "trained" if model_available else "baseline"  # 查询参数控制演示模式。
    summary = {  # 输入摘要，给答辩核对权重，不带回磁盘路径。
        "stationCount": 3,  # 候选数。
        "weightMode": mode,  # 与 modelMode 一致。
        "weights": WEIGHTS_TRAINED if model_available else WEIGHTS_BASELINE,  # 数字用字符串避免浮点争议。
        "tieBreak": ["score", "distance_km", "station_id"],  # 同分次序。
        "loadSource": load_source,  # 负荷比从哪来。
        "note": "教学候选站，不含个人标识",  # 声明。
    }  # 摘要结束。
    return {  # 推荐响应。
        "status": "ok",  # 成功。
        "modelMode": mode,  # 回显。
        "degraded": not model_available,  # 无模型即降级。
        "degradeReason": None if model_available else "MODEL_UNAVAILABLE",  # 原因。
        "recommendations": ranked,  # 带 score 与 reasons。
        "inputSummary": summary,  # 权重与来源。
        **_metadata(),  # 离线元数据。
    }  # /recommend 结束。


def _dashboard_error(error: DashboardDatabaseError) -> dict[str, object]:  # 库不可用时返回大屏同结构空数据。
    now = datetime.now(timezone.utc).isoformat()  # 错误时刻。
    return {  # 与正常 /dashboard 字段对齐的错误体。
        "state": "api-error",  # 前端识别错误态。
        "message": str(error),  # 中文：文件不存在或 schema 不兼容。
        "dataMode": "SIMULATED",  # 占位。
        "datasetName": configured_database_path().name,  # 期望的文件名。
        "datasetVersion": "sqlite-stage1-v2",  # 大屏契约版本。
        "coverage": "暂无",  # 无覆盖。
        "updatedAt": now,  # 刷新时间。
        "dataTimestamp": "暂无",  # 无业务时间。
        "stale": True,  # 过期。
        "sourceStatus": "failed",  # 源失败。
        "refreshIntervalSeconds": 3,  # 前端仍 3 秒重试。
        "metrics": [],  # 空指标。
        "load": [],  # 空负荷。
        "energyTrend": [],  # 空电量。
        "revenueTrend": [],  # 空营收。
        "pileStatus": [],  # 空桩状态。
        "telemetry": [],  # 空实时遥测。
        "alarms": [],  # 空告警。
        "tickets": [],  # 空工单。
        "recommendations": [],  # 空推荐。
        "forecastMeta": {  # 预测元信息占位。
            "method": "数据不足",  # 方法。
            "mode": "insufficient",  # 模式。
            "sampleCount": 0,  # 样本。
            "updatedAt": now,  # 时间。
            "maeKwh": None,  # 无 MAE。
        },  # forecastMeta 占位结束。
        "quality": {  # 质量占位。
            "raw": 0,  # 原始 0。
            "valid": 0,  # 有效 0。
            "rejected": 0,  # 拒绝 0。
            "rate": 0,  # 有效率 0。
            "batch": "unavailable",  # 批次不可用。
            "license": "统一SQLite业务库不可用",  # 中文说明。
            "checks": {},  # 无分项。
        },  # quality 占位结束。
        "modelMode": _active_model_mode(),  # 离线评测模式仍可展示。
        "region": "教学模拟运营区",  # 与正常载荷一致。
    }  # 错误载荷结束。


@app.get("/dashboard")  # 第一阶段主接口，只读统一库。
def dashboard() -> dict[str, object]:  # 投影成功返回数据，失败返回同结构错误体。
    try:  # 读库投影。
        return build_dashboard_payload(_quality(), _active_model_mode())  # sqlite_dashboard 内 mode=ro。
    except DashboardDatabaseError as error:  # 不把 sqlite 堆栈抛成 500。
        return _dashboard_error(error)  # 前端可展示 message。


class ScenarioSwitchRequest(BaseModel):  # POST /simulator/scenario 请求体。
    scenario: str  # 场景 id 或 cycle。
    pileId: int = Field(default=101, ge=1)  # 默认教学桩 101。


@app.get("/simulator/scenarios")  # 场景目录，含 writesBusinessDatabase=False。
def simulator_scenarios() -> dict[str, object]:  # 转调 simulator_control.catalog。
    return simulator_catalog()  # 九场景 + writesBusinessDatabase=False。


@app.get("/simulator/status")  # 忙闲与最近退出码。
def simulator_status() -> dict[str, object]:  # 转调 snapshot。
    return simulator_snapshot()  # busy/message/lastExitCode。


@app.post("/simulator/scenario")  # 只启动 pile_simulator，不 SQL 写业务表。
def switch_simulator_scenario(request: ScenarioSwitchRequest) -> dict[str, object]:  # 未知场景 400，忙则 409。
    try:  # 校验名称并起后台线程。
        status = start_simulator_scenario(request.scenario, request.pileId)  # 置忙并起线程。
    except ValueError as error:  # 未知场景名。
        raise HTTPException(status_code=400, detail=str(error)) from error  # 参数错误。
    except RuntimeError as error:  # 已有场景在跑。
        raise HTTPException(status_code=409, detail=str(error)) from error  # 冲突。
    return {"ok": True, "writesBusinessDatabase": False, "status": status}  # 再次声明不写库。

# 【文件讲解·答辩用】
# 功能作用：成员3对外 HTTP：大屏 GET /dashboard 只读 SQLite；场景 POST 只启动 pile_simulator；/health /forecast /recommend 是分析接口。
# 技术原理：FastAPI 路由；业务查询全部进 sqlite_dashboard 的 URI mode=ro；模拟器控制零 SQL 写订单/告警。
# 代码思路：/dashboard 仍投影统一库；/forecast 有 public-v1 则走站点负荷与容量公式，否则回落 3 点均值并标 RULE_BASELINE。
# 与其他文件关系：sqlite_dashboard 出大屏 JSON；train.py 写 evaluation.json；capacity 换算空闲桩；recommend 做可解释排序。
# 答辩时可以怎么讲：大屏 KPI 还是成员1库；预测推荐读的是公开聚合小时表。某个时域打不过昨天，只把该时域标 baseline。
