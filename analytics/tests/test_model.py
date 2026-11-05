from datetime import datetime, timedelta, timezone
from pathlib import Path

from src.dataset import time_split
from src.demo_pipeline import ANALYTICS_DIR, run_pipeline
from src.model import build_supervised


# 同一临时 runtime 跑两遍，结果字典必须完全相等，且评测覆盖 1/6/24 小时。
def test_pipeline_is_reproducible_and_evaluates_three_horizons(tmp_path):
    # 第一次只写临时 runtime 下的 input / raw / staging / curated/sim-v2，不得碰正式 models。
    first = run_pipeline(tmp_path / "runtime")
    # 第二次应吃到同一份生成规则和同一批清单，数值不能因随机种子或时间戳漂移。
    second = run_pipeline(tmp_path / "runtime")
    # 整份返回值（质量、行数、三份 Evaluation、元数据）逐字段相等，即教学流水线可复现。
    assert first == second
    # 对账通过才能把这套结果拿去答辩；失败会被 quality 模块在中途打断。
    assert first["quality"]["reconciled"] is True
    # evaluate() 只允许 horizon 为 1、6、24，这里确认流水线一个都没漏、也没多塞别的时域。
    assert {item["horizon_hours"] for item in first["evaluations"]} == {1, 6, 24}
    assert (tmp_path / "runtime" / "curated" / "sim-v2" / "evaluation.json").is_file()


def test_simulated_pipeline_does_not_overwrite_official_models(tmp_path):
    official = ANALYTICS_DIR / "models" / "evaluation.json"
    before = official.read_bytes() if official.exists() else None
    source = Path(__file__).resolve().parents[1] / "src" / "demo_pipeline.py"
    assert 'ANALYTICS_DIR / "models"' not in source.read_text(encoding="utf-8")
    run_pipeline(tmp_path / "runtime")
    after = official.read_bytes() if official.exists() else None
    assert before == after
    sim_eval = tmp_path / "runtime" / "curated" / "sim-v2" / "evaluation.json"
    assert sim_eval.is_file()
    assert b"SIMULATED" in sim_eval.read_bytes()
    assert not (tmp_path / "runtime" / "curated" / "public-v1" / "evaluation.json").exists()


def test_feature_hour_is_not_after_origin_and_train_precedes_test():
    start = datetime(2026, 1, 6, tzinfo=timezone(timedelta(hours=8)))
    rows = []
    for index in range(48):
        stamp = start + timedelta(hours=index)
        rows.append({
            "station_id": "1",
            "hour_start": stamp.isoformat(),
            "load_kwh": float(3 + index % 5),
            "available_piles": 4,
            "hour": stamp.hour,
            "weekday": stamp.weekday(),
            "is_holiday": 0,
        })
    samples, _codes = build_supervised(rows, 6)
    assert all(str(item["hour_start"]) < str(item["target_hour_start"]) for item in samples)
    train, test = time_split(samples)
    assert str(train[-1]["hour_start"]) < str(test[0]["hour_start"])

# 【文件讲解·答辩用】
# 功能作用：这是分析流水线的端到端冒烟测试。不测某一行清洗规则，而是证明：固定种子的
#   教学数据从生成到模型评测可以重复跑出同一份 JSON，并且三个预测时域都有评估记录。
# 技术原理：run_pipeline 依次 generate_demo_csv → ingest_source → normalize_csv →
#   build_quality_report → aggregate_hourly → evaluate(1/6/24)。演示 CSV 用固定起点
#   2026-08-30 东八区，电量和金额是公式算出来的，没有 datetime.now()，所以两次结果
#   能做字典全等。模拟评测只写 curated/sim-v2，禁止覆盖 analytics/models/evaluation.json。
# 代码思路：先跑、再跑、全等；然后抽质量对账和 horizon 集合。不在这里断言 MAE 大小，
#   因为树模型数值会随 sklearn 版本微调，教学验收认“可复现 + 时域齐全”即可。
# 与其他文件关系：入口 demo_pipeline.py；质量、归一化、数据集、模型模块都被间接覆盖。
#   benchmark_pipeline.py 同样调 run_pipeline，但输出的是耗时和行数，给性能备注用。
#   test_quality / test_normalize / test_dataset 是单步细测；本文件是把它们串起来的
#   回归网。D9 的 /forecast 在有 public-v1 时走站点负荷；无 curated 时仍回落大屏 3 点均值。
# 答辩讲法：一句话：“同一命令跑两遍，JSON 对得上，1 小时、6 小时、24 小时都评了。”
#   被问为什么不比精度，答教学重点是流程和基线对比，精度随库版本变化不作为门禁。
#   被问数据是不是真的，指向生成函数和 SIMULATED 清单，不要把演示 CSV 说成现场数据。
