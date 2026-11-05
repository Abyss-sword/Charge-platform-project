# D9：public-v1 上两次训练指标一致；特征时刻早于目标窗口；时域模式各自独立。
from src.dataset import time_split
from src.model import build_supervised
from src.train import load_hourly_csv, train_public_models


def _round_eval(item):
    return {
        "horizon_hours": item["horizon_hours"],
        "selected_mode": item["selected_mode"],
        "sample_count": item["sample_count"],
        "model_mae": round(float(item["model_mae"]), 4),
        "baseline_mae": round(float(item["baseline_mae"]), 4),
    }


def test_public_training_is_reproducible_and_does_not_leak_future(tmp_path):
    runtime = tmp_path / "runtime"
    models = tmp_path / "models"
    first = train_public_models(runtime, models)
    second = train_public_models(runtime, models)
    assert first["metadata"]["data_mode"] == "PUBLIC_AGGREGATED"
    assert first["metadata"]["dataset_version"] == "public-v1"
    assert first["target"] == "station_load_kwh"
    assert [_round_eval(item) for item in first["evaluations"]] == [_round_eval(item) for item in second["evaluations"]]
    assert {item["horizon_hours"] for item in first["evaluations"]} == {1, 6, 24}
    rows = load_hourly_csv(runtime / "curated" / "public-v1" / "hourly_station.csv")
    modes = {str(row["data_mode"]) for row in rows}
    assert modes == {"PUBLIC_AGGREGATED"}
    for horizon in (1, 6, 24):
        samples, _codes = build_supervised(rows, horizon)
        assert all(str(item["hour_start"]) < str(item["target_hour_start"]) for item in samples)
        train, test = time_split(samples)
        assert str(train[-1]["hour_start"]) < str(test[0]["hour_start"])

# 【文件讲解·答辩用】
# 功能作用：证明 D9 只在公开聚合 public-v1 上训站点负荷，两次结果一致，且没有把未来小时泄漏进训练。
# 技术原理：train_public_models 调 curate_public_batch 再 fit_horizon；比较四位小数 MAE 与 selected_mode。
# 代码思路：临时 runtime/models，避免改仓库 analytics/models。夹具 6 行质量样本不在这里训练。
# 答辩讲法：同一命令跑两遍，1/6/24 的选用模式对得上；特征小时一定早于目标小时。
