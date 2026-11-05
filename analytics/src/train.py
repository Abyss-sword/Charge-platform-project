from __future__ import annotations  # 3.10 延迟注解。

import argparse  # 选择旧公开聚合训练或第二阶段 Spark 特征训练。
import csv  # 读取 public-v1 hourly_station.csv。
import json  # 写出 evaluation.json。
import math  # 把无穷误差转换成JSON null。
from dataclasses import asdict  # Evaluation 转可序列化字典。
from datetime import datetime, timezone  # 模型发布时间使用UTC。
from pathlib import Path  # runtime 与 models 目录。
from typing import Any  # 训练结果 dict。

import joblib  # sklearn 依赖，用来保存打赢基线的时域估计器。

from .curate_batch import curate_public_batch  # 缺少 curated 时先跑 D8，不混 SIMULATED。
from .model import (  # 旧模型、正式 Spark 特征模型与高峰口径。
    STAGE2_FEATURE_NAMES,
    chronological_stage2_split,
    fit_horizon,
    fit_stage2_horizon,
    precision_at_peak,
    stage2_feature_matrix,
    training_peak_thresholds,
)

ANALYTICS_DIR = Path(__file__).resolve().parents[1]  # analytics 包根。
PROJECT_DIR = ANALYTICS_DIR.parent  # 仓库根。


def load_hourly_csv(path: Path) -> list[dict[str, Any]]:  # 把 CSV 字符串列转成训练用的数值。
    with path.open("r", encoding="utf-8", newline="") as stream:  # 文本 CSV。
        rows = []  # 小时特征。
        for row in csv.DictReader(stream):  # 按表头读。
            item = dict(row)  # 拷贝，避免改 DictReader 内部行。
            item["load_kwh"] = float(item["load_kwh"])  # 负荷。
            item["available_piles"] = int(float(item["available_piles"]))  # 教学可用桩。
            item["hour"] = int(item["hour"])  # 0–23。
            item["weekday"] = int(item["weekday"])  # 0=周一。
            item["is_holiday"] = int(item.get("is_holiday") or 0)  # 教学恒 0。
            rows.append(item)  # 收行。
    return rows  # 供 fit_horizon。


def train_public_models(runtime_root: Path | None = None, model_dir: Path | None = None) -> dict[str, Any]:  # D9 入口：只在 PUBLIC_AGGREGATED / public-v1 上训站点负荷。
    runtime = Path(runtime_root) if runtime_root else PROJECT_DIR / "runtime" / "analytics"  # 默认仓库 runtime。
    models = Path(model_dir) if model_dir else ANALYTICS_DIR / "models"  # 评测与 joblib；目录已被 gitignore。
    curated_csv = runtime / "curated" / "public-v1" / "hourly_station.csv"  # D8 正式小时表。
    if not curated_csv.exists():  # 本机还没跑过清洗。
        curate_public_batch(runtime)  # 只读公开聚合 raw，不造 SIMULATED 训练样本。
    rows = load_hourly_csv(curated_csv)  # 读小时特征。
    modes = {str(row["data_mode"]) for row in rows}  # 本表出现的模式。
    versions = {str(row["dataset_version"]) for row in rows}  # 本表版本。
    if len(modes) != 1 or "PUBLIC_AGGREGATED" not in modes:  # 禁止混模式或拿模拟顶替。
        raise ValueError("PUBLIC_TRAIN_REQUIRES_SINGLE_PUBLIC_AGGREGATED_MODE")  # 阻断。
    if len(versions) != 1 or "public-v1" not in versions:  # 必须是 D8 正式版本。
        raise ValueError("PUBLIC_TRAIN_REQUIRES_PUBLIC_V1")  # 阻断。
    models.mkdir(parents=True, exist_ok=True)  # 可重复跑。
    evaluations = []  # 三个时域。
    artifacts: dict[str, str | None] = {}  # 只记文件名，HTTP 不得回传本机路径。
    for horizon in (1, 6, 24):  # 同一算法、三套标签。
        evaluation, estimator, station_codes = fit_horizon(rows, horizon)  # 时间切分 + 昨日同窗口基线。
        payload = asdict(evaluation)  # 可写入 JSON。
        payload["mae_delta"] = evaluation.baseline_mae - evaluation.model_mae  # 正值表示模型 MAE 更低。
        payload["rmse_delta"] = evaluation.baseline_rmse - evaluation.model_rmse  # RMSE 差值。
        payload["station_codes"] = station_codes  # 推理时复用同一编码。
        evaluations.append(payload)  # 收时域。
        artifact = models / f"load_h{horizon}.joblib"  # 该时域模型文件。
        if evaluation.selected_mode == "trained":  # 只有打赢基线才发布模型文件。
            joblib.dump({"estimator": estimator, "station_codes": station_codes, "horizon_hours": horizon}, artifact)  # 估计器与编码一起存。
            artifacts[str(horizon)] = artifact.name  # 只留文件名。
        else:  # 该时域发布基线。
            if artifact.exists():  # 上次若误发布，这次拿掉，避免接口读到过期模型。
                artifact.unlink()  # 删除过期 joblib。
            artifacts[str(horizon)] = None  # 无模型文件。
    metadata = json.loads((runtime / "curated" / "public-v1" / "metadata.json").read_text(encoding="utf-8"))  # 覆盖区间。
    results = {  # 给 /health 和复现脚本。
        "curated_rows": len(rows),  # Leeds 正式小时行，当前 6450。
        "evaluations": evaluations,  # 三时域。
        "metadata": metadata,  # data_mode / 版本 / 覆盖。
        "target": "station_load_kwh",  # 唯一目标变量。
        "algorithm": "HistGradientBoostingRegressor",  # 一种树模型。
        "hyperparameters": {"max_depth": 6, "learning_rate": 0.08, "random_state": 42},  # 与计划锁定一致。
        "split": "time_split_0.8",  # 训练集时刻严格早于测试集。
        "artifacts": artifacts,  # 文件名或 None。
    }  # 结果字典结束。
    text = json.dumps(results, ensure_ascii=False, indent=2)  # 同一份 JSON 写两处。
    (models / "evaluation.json").write_text(text, encoding="utf-8")  # API 默认读取处。
    (runtime / "curated" / "public-v1" / "evaluation.json").write_text(text, encoding="utf-8")  # 与小时表放一起，便于血缘。
    return results  # 测试比较两次运行。


def _json_safe(value: Any) -> Any:
    if isinstance(value, float) and not math.isfinite(value):
        return None
    if isinstance(value, dict):
        return {key: _json_safe(item) for key, item in value.items()}
    if isinstance(value, list):
        return [_json_safe(item) for item in value]
    return value


def load_stage2_training(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, start=1):
            if not line.strip():
                continue
            item = json.loads(line)
            required = {"station_id", "feature_available_at", "target_end", "horizon_hours", "target_energy_kwh"}
            missing = sorted(required - set(item))
            if missing:
                raise ValueError(f"TRAINING_ROW_MISSING line={line_number} fields={','.join(missing)}")
            rows.append(item)
    if not rows:
        raise ValueError("DATA_INSUFFICIENT: empty Spark training export")
    return rows


def _collect_peak_curve(
    all_rows: list[dict[str, Any]],
    horizon: int,
    estimator: Any,
    station_codes: dict[str, int],
    curves: dict[tuple[str, str], list[dict[str, Any]]],
) -> None:
    """Re-derive this horizon's test split and record its predictions per origin.

    Peak precision needs a whole forecast curve, but each horizon is trained separately, so the
    curve has to be reassembled across horizons keyed by origin. The split is recomputed here
    instead of being passed out of fit_stage2_horizon so that scoring can only ever see test hours.
    """
    horizon_rows = [row for row in all_rows if int(row.get("horizon_hours", 0)) == horizon]
    try:
        _train, _validation, test = chronological_stage2_split(horizon_rows)
    except ValueError:
        return
    known = [
        row
        for row in test
        if str(row["station_id"]) in station_codes and row.get("target_energy_kwh") not in (None, "")
    ]
    if not known:
        return
    predicted = estimator.predict(stage2_feature_matrix(known, station_codes))
    for row, value in zip(known, predicted):
        key = (str(row["station_id"]), str(row["feature_available_at"]))
        curves.setdefault(key, []).append({
            "horizonHours": horizon,
            "actual": float(row["target_energy_kwh"]),
            "predicted": max(0.0, float(value)),
        })


def train_stage2_models(feature_dir: Path, model_dir: Path) -> dict[str, Any]:
    """Train horizons 1..24 from one Spark publication and publish an atomic metadata bundle."""
    manifest_path = feature_dir / "feature-manifest.json"
    training_path = feature_dir / "training.jsonl"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("contractVersion") != "member3_features_v1":
        raise ValueError("FEATURE_CONTRACT_UNSUPPORTED")
    rows = load_stage2_training(training_path)
    model_dir.mkdir(parents=True, exist_ok=True)
    evaluations: list[dict[str, Any]] = []
    artifacts: dict[str, str] = {}
    peak_curves: dict[tuple[str, str], list[dict[str, Any]]] = {}
    for horizon in range(1, 25):
        evaluation, estimator, station_codes = fit_stage2_horizon(rows, horizon)
        _collect_peak_curve(rows, horizon, estimator, station_codes, peak_curves)
        artifact_name = f"course_load_h{horizon:02d}.joblib"
        joblib.dump(
            {
                "trustedProjectArtifact": True,
                "datasetVersion": manifest["datasetVersion"],
                "runId": manifest["runId"],
                "horizonHours": horizon,
                "featureNames": list(STAGE2_FEATURE_NAMES),
                "stationCodes": station_codes,
                "estimator": estimator,
            },
            model_dir / artifact_name,
        )
        artifacts[str(horizon)] = artifact_name
        evaluations.append(evaluation)
    bundle = _json_safe({
        "contractVersion": "member3_model_v1",
        "datasetVersion": manifest["datasetVersion"],
        "runId": manifest["runId"],
        "dataMode": manifest["dataMode"],
        "observationKind": manifest["observationKind"],
        "generatedAt": datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "algorithm": "HistGradientBoostingRegressor",
        "selectionRule": (
            "lowest validation MAE against yesterday_same_hour and recent_3h_mean; test set never selects; "
            "a horizon whose validation baseline has too few comparable samples is marked "
            "selectionCaveat=NO_COMPARABLE_BASELINE instead of claiming a win over the baseline"
        ),
        "split": "chronological 70/15/15 with target-end purge at train and validation boundaries",
        "featureNames": list(STAGE2_FEATURE_NAMES),
        "trainingRows": len(rows),
        "evaluations": evaluations,
        "artifacts": artifacts,
        "capacityForecast": {"status": "BLOCKED", "reason": "NO_CAPACITY_DATA"},
        "peakRule": (
            "top 3 forecast hours ordered by load descending then target time ascending; "
            "high-load windows are the forecast hours strictly above the training-split P90 line; "
            "capacity-based power thresholds stay unavailable while the dataset carries no capacity column"
        ),
        "peakThresholds": training_peak_thresholds(rows),
        "peakEvaluation": precision_at_peak(peak_curves),
    })
    temporary = model_dir / ".stage2-evaluation.json.tmp"
    temporary.write_text(json.dumps(bundle, ensure_ascii=False, indent=2, allow_nan=False), encoding="utf-8")
    temporary.replace(model_dir / "stage2-evaluation.json")
    return bundle


def main() -> None:  # python -m src.train
    parser = argparse.ArgumentParser(description="Train legacy public models or formal stage2 models")
    parser.add_argument("--stage2", action="store_true", help="read Spark feature publication and train horizons 1..24")
    parser.add_argument("--feature-dir", type=Path, default=PROJECT_DIR / "runtime" / "analytics" / "stage2" / "course-v1" / "course-001")
    parser.add_argument("--model-dir", type=Path, default=ANALYTICS_DIR / "models" / "stage2")
    args = parser.parse_args()
    if args.stage2:
        bundle = train_stage2_models(args.feature_dir, args.model_dir)
        print(f"DATASET_VERSION={bundle['datasetVersion']}")
        print(f"RUN_ID={bundle['runId']}")
        print(f"TRAINING_ROWS={bundle['trainingRows']}")
        for horizon in (1, 6, 24):
            item = bundle["evaluations"][horizon - 1]
            print(f"HORIZON_{horizon}H={item['selectedMode']}")
            print(f"HORIZON_{horizon}H_TEST_MODEL_MAE={item['test']['modelMae']:.6f}")
            baseline_mae = item["test"]["baselineMae"]
            print(f"HORIZON_{horizon}H_TEST_BASELINE_MAE={baseline_mae if baseline_mae is not None else 'NA'}")
        print("STAGE2_TRAIN=PASS")
        return
    first = train_public_models()  # 第一遍。
    second = train_public_models()  # 第二遍应得到同一套 selected_mode 与四位小数误差。
    def _round(item: dict[str, Any]) -> dict[str, Any]:  # 忽略浮点尾差。
        return {  # 只比门禁用的字段。
            "horizon_hours": item["horizon_hours"],
            "selected_mode": item["selected_mode"],
            "sample_count": item["sample_count"],
            "model_mae": round(float(item["model_mae"]), 4),
            "baseline_mae": round(float(item["baseline_mae"]), 4),
        }  # 舍入结束。
    if [_round(item) for item in first["evaluations"]] != [_round(item) for item in second["evaluations"]]:  # 不可复现。
        raise SystemExit("PUBLIC_TRAIN_RUNS_DIFFER")  # 失败。
    print("CONTRACT=charge_event_v2")  # 契约未改。
    print(f"DATA_MODE={first['metadata']['data_mode']}")  # PUBLIC_AGGREGATED。
    print(f"DATASET_VERSION={first['metadata']['dataset_version']}")  # public-v1。
    print(f"CURATED_ROWS={first['curated_rows']}")  # 正式 curated 行数。
    print(f"TARGET={first['target']}")  # station_load_kwh。
    print(f"ALGORITHM={first['algorithm']}")  # HGBR。
    for item in first["evaluations"]:  # 逐时域。
        print(f"HORIZON_{item['horizon_hours']}H={item['selected_mode'].upper()}")  # TRAINED 或 BASELINE。
        print(f"HORIZON_{item['horizon_hours']}H_MODEL_MAE={item['model_mae']:.4f}")  # 模型 MAE。
        print(f"HORIZON_{item['horizon_hours']}H_BASELINE_MAE={item['baseline_mae']:.4f}")  # 基线 MAE。


if __name__ == "__main__":  # 仅脚本启动时训练。
    main()  # 跑两遍并打印。

# 【文件讲解·答辩用】
# 功能作用：在 D8 的 public-v1 小时表上按时间切分训练树模型，并与昨日同窗口基线比较。
# 技术原理：三个同配置 HGBR 对应 1/6/24 小时标签；某时域 MAE 不能稳定更低就不写该时域 joblib。
# 代码思路：缺 curated 先 curate_public_batch → 校验单一 PUBLIC_AGGREGATED → 三时域 fit_horizon → 写 evaluation.json。
# 与其他文件关系：不再调用 demo_pipeline（那条是 SIMULATED 96 小时教学链）。api.py 读 models/evaluation.json。
# 答辩时可以怎么讲：训练数据是公开聚合，不是把模拟数据改名；某个时域打不过昨天，接口就标 baseline。
