from __future__ import annotations  # 与流水线脚本同一注解风格。

import json  # 打印两次评测是否一致。
import tempfile  # 隔离 runtime，不覆盖本机 models。
from pathlib import Path  # 临时目录。

from src.train import train_public_models  # D9 正式训练入口。


def _round(item: dict) -> dict:  # 只比门禁用字段，忽略浮点尾差。
    return {  # 时域、模式、样本、四位 MAE。
        "horizon_hours": item["horizon_hours"],
        "selected_mode": item["selected_mode"],
        "sample_count": item["sample_count"],
        "model_mae": round(float(item["model_mae"]), 4),
        "baseline_mae": round(float(item["baseline_mae"]), 4),
    }  # 舍入结束。


def main() -> None:  # python tests/run_reproducibility_check.py
    with tempfile.TemporaryDirectory() as directory:  # 用完即删。
        root = Path(directory)  # 临时根。
        first = train_public_models(root / "runtime", root / "models")  # 第一遍。
        second = train_public_models(root / "runtime", root / "models")  # 第二遍。
        same = [_round(item) for item in first["evaluations"]] == [_round(item) for item in second["evaluations"]]  # 是否可复现。
        print(json.dumps({  # 一行证据。
            "same": same,
            "data_mode": first["metadata"]["data_mode"],
            "dataset_version": first["metadata"]["dataset_version"],
            "curated_rows": first["curated_rows"],
            "evaluations": [_round(item) for item in first["evaluations"]],
        }, ensure_ascii=False))  # 保留中文模式名以外的 ASCII 键。
        if not same:  # 两次不一致。
            raise SystemExit(1)  # 非零退出给脚本调用方。


if __name__ == "__main__":  # 直接运行才检查。
    main()  # 入口。

# 【文件讲解·答辩用】
# 功能作用：用临时目录把 public-v1 训练跑两遍，打印是否可复现，避免改仓库 models。
# 技术原理：与 test_train.py 同一套 round 规则；失败退出码 1。
# 答辩讲法：指 same=true 和三个 HORIZON 的 selected_mode。
