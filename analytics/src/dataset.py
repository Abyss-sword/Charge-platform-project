from __future__ import annotations  # 允许 list[dict] 等注解在 3.10 运行。

from datetime import datetime, timedelta, timezone  # 按小时时间戳做切分，以及“前一天同时刻”基线。
from typing import Any, Sequence  # Sequence 表示只读遍历，不要求一定是 list。


def time_split(rows: Sequence[dict[str, Any]], ratio: float = 0.8) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:  # 按 hour_start 时间顺序切训练/测试，禁止随机打乱，防止未来小时泄漏进训练集。
    if not 0 < ratio < 1:  # 比例必须落在开区间，0 或 1 会导致某一侧为空。
        raise ValueError("ratio must be between 0 and 1")  # 非法比例直接拒绝。
    ordered = sorted(rows, key=lambda row: (str(row["hour_start"]), str(row.get("station_id", ""))))  # 先按小时再按站点，保证切分稳定。
    timestamps = sorted({str(row["hour_start"]) for row in ordered})  # 去重后的时间轴，按站点重复的小时只算一次边界。
    if len(timestamps) < 2:  # 只有一个时刻无法分出“过去/未来”。
        raise ValueError("at least two distinct timestamps are required")  # 数据太短不能评测。
    pivot = max(1, min(len(timestamps) - 1, int(len(timestamps) * ratio)))  # 夹紧切点，保证两侧都至少有一个时间戳。
    boundary = timestamps[pivot]  # 该时刻起（含）划入测试集。
    train = [row for row in ordered if str(row["hour_start"]) < boundary]  # 边界之前全部训练。
    test = [row for row in ordered if str(row["hour_start"]) >= boundary]  # 边界及之后全部测试。
    if str(train[-1]["hour_start"]) >= str(test[0]["hour_start"]):  # 再断言训练集最晚时刻早于测试集最早时刻。
        raise ValueError("time split overlaps")  # 重叠就说明切分逻辑坏了。
    return train, test  # 返回两个列表，供 model.evaluate 拟合与打分。


def previous_day_baseline(rows: Sequence[dict[str, Any]]) -> list[float | None]:  # 朴素基线：同一站点 24 小时前的 load_kwh；没有昨天同时刻则记 None。
    lookup = {(str(row["station_id"]), datetime.fromisoformat(str(row["hour_start"])).astimezone(timezone.utc)): float(row["load_kwh"]) for row in rows}  # 统一到 UTC，夏令时切换前后仍按真实 24 小时回查。
    result: list[float | None] = []  # 与输入行一一对应的基线值。
    for row in rows:  # 按原顺序生成，方便和真实负荷对齐算误差。
        previous = datetime.fromisoformat(str(row["hour_start"])).astimezone(timezone.utc) - timedelta(hours=24)  # 按真实经过时间回退 24 小时。
        result.append(lookup.get((str(row["station_id"]), previous)))  # 查不到就是 None，评测时需跳过或单独处理。
    return result  # 给需要“昨日同时刻”对照的调用方；model.py 里监督样本用的是序列内 -24 步。


def attach_yesterday_baseline(rows: Sequence[dict[str, Any]]) -> list[dict[str, Any]]:  # 把基线写回每行，首日明确标不可用，不拿 0 或随机数填。
    baselines = previous_day_baseline(rows)  # 与输入等长的 None/float。
    attached: list[dict[str, Any]] = []  # 拷贝后追加两列。
    for row, baseline in zip(rows, baselines):  # 逐行对齐。
        item = dict(row)  # 不改调用方原 dict。
        item["baseline_available"] = baseline is not None  # 答辩可直接念这列。
        item["baseline_load_kwh"] = None if baseline is None else round(float(baseline), 2)  # 有值才给两位。
        attached.append(item)  # 收行。
    return attached  # curated 旁路或测试使用。

# 【文件讲解·答辩用】
# 功能作用：给负荷预测提供时间切分和昨日同时刻基线，避免随机划分造成信息泄漏。
# 技术原理：先抽出去重时间轴再按比例取切点；同一小时多站点一起进训练或一起进测试。
# 代码思路：time_split 用字符串比较 ISO 小时；previous_day_baseline 回查 24h 前；attach_yesterday_baseline 把 None 标成不可用。
# 与其他文件关系：model.evaluate 调用 time_split；夹具质量流水线调用 attach_yesterday_baseline。
# 答辩时可以怎么讲：充电负荷有明显日内周期，必须按时间切；第一天没有昨天，基线是不可用而不是填 0。
