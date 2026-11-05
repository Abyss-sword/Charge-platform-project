# 构造带 UTC 时区的 hour_start，验证按时间切分和“昨天同一小时”基线。
from datetime import datetime, timedelta, timezone

# MIXING 模式混用时必须抛 ValueError，用 pytest.raises 接住。
import pytest

# aggregate_hourly 把事件行滚成站点×小时的 curated 特征。
from src.build_curated import aggregate_hourly
# time_split 防未来泄漏；previous_day_baseline 是“昨天同时段负荷”朴素预测。
from src.dataset import attach_yesterday_baseline, previous_day_baseline, time_split


# 同一站点同一小时两条订单，应聚成 1 行：单数 2、电量 3.00、批次仍为 b1。
def test_hourly_aggregation_and_lineage():
    # 10:15 和 10:45 都落在 10 点桶；电量用字符串避免 float 对账。血缘字段随行携带。
    rows = [{"station_id": 1, "occurred_at": "2026-09-01T10:15:00+08:00", "energy_kwh": "1.00", "duration_seconds": 600, "data_mode": "SIMULATED", "dataset_name": "demo", "dataset_version": "v2", "ingest_batch": "b1"}, {"station_id": 1, "occurred_at": "2026-09-01T10:45:00+08:00", "energy_kwh": "2.00", "duration_seconds": 1200, "data_mode": "SIMULATED", "dataset_name": "demo", "dataset_version": "v2", "ingest_batch": "b1"}]
    # available_piles 映射键是站点码字符串；本断言只取聚合结果第一行。
    result = aggregate_hourly(rows, {"S1": 3})[0]
    # 1.00+2.00 用 Decimal 得到 "3.00"；ingest_batches 保留 b1，证明小时表能回溯到批次。
    assert result["order_count"] == 2 and result["load_kwh"] == "3.00" and result["ingest_batches"] == "b1"


# 同一批聚合里同时出现 SIMULATED 和 PUBLIC_AGGREGATED，必须拒绝，不能默默混训。
def test_mode_mixing_requires_explicit_selection():
    # 抽出公共字段，两条记录只在 data_mode 上不同，用来单独触发混模式检查。
    base = {"station_id": 1, "occurred_at": "2026-09-01T10:00:00+08:00", "energy_kwh": "1.00", "duration_seconds": 1, "dataset_name": "d", "dataset_version": "v2", "ingest_batch": "b"}
    # 源码异常文本含 MIXING，对应 DATA_MODE_MIXING_REQUIRES_EXPLICIT_SELECTION。
    with pytest.raises(ValueError, match="MIXING"):
        # 不传 available_piles 也可以，混模式在分组前就会失败。
        aggregate_hourly([{**base, "data_mode": "SIMULATED"}, {**base, "data_mode": "PUBLIC_AGGREGATED"}])


# 按时间 80/20 切分后训练集最后时刻必须早于测试集最先时刻；昨天基线在第 25 个点应命中 0。
def test_time_split_and_yesterday_baseline():
    # 用 UTC 整点做起点，避免本地时区把 hour_start 字符串比较搞乱。
    start = datetime(2026, 9, 1, tzinfo=timezone.utc)
    # 30 个连续小时，第 i 点负荷写成 "i.00"，这样昨天同时段的数值就是 i-24。
    rows = [{"station_id": 1, "hour_start": (start + timedelta(hours=i)).isoformat(), "load_kwh": f"{i:.2f}"} for i in range(30)]
    # 默认 ratio=0.8：按不重复时间戳切，训练全在边界之前，测试从边界起。
    train, test = time_split(rows)
    # 字符串比较 ISO 时间即可，因为都是同一偏移的零分零秒。
    assert train[-1]["hour_start"] < test[0]["hour_start"]
    # 下标 24 对应 2026-09-02T00:00:00+00:00，昨天同时段是下标 0，负荷为 0.00 → 浮点 0。
    assert previous_day_baseline(rows)[24] == 0


def test_first_day_baseline_is_unavailable_not_imputed():
    start = datetime(2026, 1, 7, tzinfo=timezone.utc)
    rows = [{"station_id": 1, "hour_start": (start + timedelta(hours=i)).isoformat(), "load_kwh": "2.00"} for i in range(3)]
    attached = attach_yesterday_baseline(rows)
    assert all(item["baseline_available"] is False and item["baseline_load_kwh"] is None for item in attached)

# 【文件讲解·答辩用】
# 功能作用：把“事件表怎么变成小时特征、怎么切训练/测试、基线怎么取昨天”三条规则测死。
#   大屏能量趋势和模型训练都吃 curated 小时表；切分做错就会把未来泄漏进训练集。
# 技术原理：aggregate_hourly 按 (station_id, 整点 hour_start) 分组，电量用 Decimal 相加再
#   量化成两位小数字符串，批次用分号去重拼接。发现多于一种 data_mode 立即抛错，因为
#   模拟数据和公开聚合数据的量纲、缺失机制不同，不能当同质样本。time_split 先按
#   hour_start 排序，再按时间戳个数的 80% 取边界，训练严格 < 边界、测试 >= 边界。
#   previous_day_baseline 用 (站点, 当前小时-24h) 去查表，查不到就是 None。
# 代码思路：第一条用同一小时两条订单看聚合和血缘；第二条只改 data_mode 看混用拦截；
#   第三条造 30 个整点，既看切分不重叠，又用下标 24 验证“昨天负荷 = 0”。
# 与其他文件关系：聚合在 build_curated.py，切分和基线在 dataset.py。demo_pipeline 先
#   normalize 再 aggregate_hourly，再 evaluate（内部再次 time_split）。test_normalize.py
#   产出的 energy_kwh 字符串正是这里的输入格式。test_model.py 不直接测切分，但依赖
#   本文件保证的时间顺序。
# 答辩讲法：先画 10:15 和 10:45 两个点滚进同一个 10 点桶，电量 1+2=3.00。再说“模拟和
#   公开数据不能混着训”。最后强调我们按时间切而不是随机切，并指出昨天同时段基线是
#   模型对比的朴素对手，不是深度学习。被问 available_piles 键 S1 时：聚合结果断言没
#   碰这个字段，映射在真正写 curated 时由流水线传入站点 id。
