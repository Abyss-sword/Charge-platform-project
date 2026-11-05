from __future__ import annotations  # 延迟注解，兼容 3.10。

import argparse  # 命令行入口，非作者可按文档复跑。
import json  # 读 manifest 与输出状态。
import sys  # 退出码。
from pathlib import Path  # 路径处理。

from .history_adapter import adapt_csv  # 适配器（隐私阻断/拒绝记账/分摊）。
from .publish_dataset import (  # 发布链。
    hdfs_upload_run,
    load_active_manifest,
    prepare_run,
    publish_run,
)
from .quality import build_quality_report  # 质量对账。


def main(argv: list[str] | None = None) -> int:  # 返回退出码：0 成功，2 参数/阻断，1 其它失败。
    parser = argparse.ArgumentParser(description="historical_interval_v1 治理批次：适配→质量→候选→HDFS→发布")  # 用途说明。
    parser.add_argument("--manifest", required=True, help="来源清单 JSON（source_registry 校验）")  # 清单必填。
    parser.add_argument("--source-csv", required=True, help="来源区间 CSV")  # 来源必填。
    parser.add_argument("--runtime-root", default="runtime", help="运行缓存根目录")  # 默认 runtime/。
    parser.add_argument("--run-id", required=True, help="run 目录名（同内容重跑幂等）")  # run 必填。
    parser.add_argument("--existing-manifest", default=None, help="可选：先前 manifest，用于跨批去重")  # 跨批去重。
    args = parser.parse_args(argv)  # 解析。
    manifest = json.loads(Path(args.manifest).read_text(encoding="utf-8"))  # 读来源清单。
    from .source_registry import load_manifest  # 准入闸门（许可/隐私/用途）。

    try:  # 准入失败整批不落盘。
        load_manifest(args.manifest)  # 校验必填/模式/隐私声明。
    except Exception as error:  # AdmissionError。
        print(f"ADMISSION=REFUSED {error}")  # 明确输出。
        return 2  # 非成功。
    metadata = {  # 适配元数据来自清单。
        "source_id": manifest["source_id"],  # 命名空间。
        "dataset_version": manifest["dataset_version"],  # 版本。
        "data_mode": manifest["data_mode"],  # 模式。
        "ingest_batch": manifest["ingest_batch"],  # 批次。
        "ingested_at": manifest.get("acquired_at", ""),  # 摄入时间。
    }  # 结束：元数据。
    existing_keys: set[tuple[str, str]] = set()  # 跨批去重键。
    if args.existing_manifest:  # 给了先前清单。
        previous = json.loads(Path(args.existing_manifest).read_text(encoding="utf-8"))  # 读取。
        if str(previous.get("dataset_version")) == str(metadata["dataset_version"]) and str(previous.get("source_id")) == str(metadata["source_id"]):  # 同版本同命名空间才去重。
            runs_dir = Path(args.runtime_root) / "analytics" / "runs" / metadata["dataset_version"]  # 历史 run。
            for record_file in sorted(runs_dir.glob("*/intervals.jsonl")):  # 扫已固化的记录。
                for line in record_file.read_text(encoding="utf-8").splitlines():  # 逐行。
                    if line.strip():  # 跳过空行。
                        record = json.loads(line)  # 还原记录。
                        existing_keys.add((str(record.get("source_id")), str(record.get("record_id"))))  # 登记键。
    try:  # 批级阻断（隐私列/缺列）。
        result = adapt_csv(args.source_csv, metadata, existing_keys=existing_keys)  # 适配。
    except Exception as error:  # BatchBlocked。
        print(f"BATCH=BLOCKED {error}")  # 明确输出。
        return 2  # 非成功。
    quality = build_quality_report(len(result.accepted) + len(result.rejected), result.accepted, result.rejected)  # 质量对账。
    try:  # 质量门。
        run_dir = prepare_run(args.runtime_root, metadata["dataset_version"], args.run_id, result, quality, manifest=metadata)  # 候选目录。
    except Exception as error:  # PublishBlocked。
        print(f"PUBLISH=REFUSED {error}")  # 明确输出。
        return 1  # 非成功。
    print(f"QUALITY_INPUT={quality['input_count']}")  # 对账：输入。
    print(f"QUALITY_ACCEPTED={quality['accepted_count']}")  # 通过。
    print(f"QUALITY_REJECTED={quality['rejected_count']}")  # 拒绝。
    print(f"QUALITY_REASONS={json.dumps(quality['rejection_reasons'], ensure_ascii=False, sort_keys=True)}")  # 主原因分布。
    if quality["diagnostic_tags"]:  # 诊断标签仅观察。
        print(f"QUALITY_DIAGNOSTICS={json.dumps(quality['diagnostic_tags'], ensure_ascii=False, sort_keys=True)}")  # 标签分布。
    upload = hdfs_upload_run(args.runtime_root, metadata["dataset_version"], args.run_id)  # HDFS 上传（本地无 hdfs 则 SKIPPED）。
    print(f"HDFS={upload['hdfs']}")  # 状态。
    if upload["hdfs"] == "FAILED":  # 依赖不可用：HDFS 存在但上传失败（03 文档 S2-T18）。
        # 不允许把新批次标成成功：候选与质量账保留，旧已发布版本保持可读，返回非 0。
        print(f"HDFS_REASON={upload.get('reason', '')}")  # 如实报告失败原因。
        print(f"RUN_DIR={run_dir}")  # 候选目录，供人工修复后重跑。
        return 1  # 非成功：新批次不得伪造成功。
    try:  # 激活阶段同样受质量门约束。
        publish_run(args.runtime_root, metadata["dataset_version"], args.run_id)  # 本地激活（原子替换）。
    except Exception as error:  # PublishBlocked。
        print(f"PUBLISH=REFUSED {error}")  # 候选与质量账已保留，人工修复后重跑。
        print(f"RUN_DIR={run_dir}")  # 候选目录。
        return 1  # 非成功（不低于阈值不得自动发布）。
    active = load_active_manifest(args.runtime_root)["active"][metadata["dataset_version"]]  # 读回激活指针。
    print(f"PUBLISHED_RUN={active['runId']}")  # 激活结果。
    print(f"RUN_DIR={run_dir}")  # 候选目录。
    return 0  # 成功。


if __name__ == "__main__":  # 直接执行。
    sys.exit(main())  # 退出码透传。
