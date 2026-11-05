from __future__ import annotations  # 延迟注解，兼容 3.10。

import hashlib  # 输入指纹：同 run_id 重跑必须比对内容。
import json  # run_manifest 与 active_manifest。
import os  # os.replace 原子替换。
import shutil  # which 探测 hdfs 命令。
import subprocess  # 调用 hdfs 命令行。
from datetime import datetime, timezone  # 发布时间戳（UTC）。
from pathlib import Path  # 运行缓存目录。
from typing import Any, Callable  # 类型标注。

from .history_adapter import AdaptResult, fragments_to_tsv_rows  # 片段 → TSV 行。

MIN_VALID_RATE = "0.95"  # 质量预警线（00 总规格 §5.4）：低于不得自动发布，需修复或重定范围并报告。


class PublishBlocked(ValueError):  # 质量门拒绝：批次不得进入发布。
    pass  # 专用异常，脚本可区分退出码。


def _utc_now() -> str:  # UTC 时间文本。
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")  # 与契约时间格式一致。


def quality_gate(quality: dict[str, Any]) -> tuple[bool, list[str]]:  # 质量预警线：低于阈值不得自动发布；候选目录仍可写（干净记录进候选集，拒绝记账可查）。
    reasons: list[str] = []  # 不通过原因。
    if int(quality.get("input_count", 0)) <= 0 or int(quality.get("accepted_count", 0)) <= 0:  # 全部拒绝或空批次。
        reasons.append("EMPTY_OR_ALL_REJECTED_BATCH")  # 必须非成功返回，禁止发布空产物。
    if not quality.get("reconciled", False):  # 对账失败（正常不会到这里）。
        reasons.append("QUALITY_NOT_RECONCILED")  # 记账不平。
    if int(quality.get("input_count", 0)) > 0 and float(quality.get("valid_rate", 0.0)) < float(MIN_VALID_RATE):  # 有效率低于预警线。
        reasons.append("VALID_RATE_BELOW_THRESHOLD")  # 记录原因。
    return (len(reasons) == 0, reasons)  # 通过与否与原因。


def _fingerprint(intervals_text: str, tsv_text: str) -> str:  # 内容指纹：幂等与 run_id 冲突检测。
    digest = hashlib.sha256()  # SHA-256。
    digest.update(intervals_text.encode("utf-8"))  # 规范记录。
    digest.update(tsv_text.encode("utf-8"))  # staging 片段。
    digest.update(json.dumps({"rejected": 0}, sort_keys=True).encode("utf-8"))  # 占位扩展点。
    return digest.hexdigest()  # 十六进制。


def prepare_run(  # 构建候选 run 目录（未激活）；空/全拒绝批次直接拒绝；低质量候选可写但不许自动发布。
    runtime_root: str | Path,  # 运行缓存根（runtime/）。
    dataset_version: str,  # 数据版本。
    run_id: str,  # 运行目录名。
    adapt_result: AdaptResult,  # 适配结果三账。
    quality: dict[str, Any],  # 质量报告（build_quality_report 产物）。
    manifest: dict[str, Any],  # 来源清单元数据（source_id/ingest_batch/data_mode）。
) -> Path:  # 返回候选 run 目录。
    gate_ok, gate_reasons = quality_gate(quality)  # 质量预警线。
    if "EMPTY_OR_ALL_REJECTED_BATCH" in gate_reasons:  # 空/全拒绝批次。
        raise PublishBlocked("EMPTY_OR_ALL_REJECTED_BATCH")  # 必须非成功，不写候选。
    run_dir = Path(runtime_root) / "analytics" / "runs" / dataset_version / run_id  # 候选目录。
    intervals_text = "".join(  # 规范记录 JSONL 文本（稳定排序键；剔除下划线开头的内部字段）。
        json.dumps({k: v for k, v in record.items() if not str(k).startswith("_")}, ensure_ascii=False, sort_keys=True) + "\n"  # 每行一条，保证过 historical_interval_v1 schema。
        for record in sorted(adapt_result.accepted, key=lambda r: (r["analysis_station_id"], r["record_id"]))  # 排序保证指纹稳定。
    )  # 结束：JSONL。
    tsv_text = "".join(row + "\n" for row in fragments_to_tsv_rows(adapt_result.fragments))  # staging TSV 文本。
    fingerprint = _fingerprint(intervals_text, tsv_text)  # 内容指纹。
    manifest_path = run_dir / "run_manifest.json"  # run 清单位置。
    if manifest_path.exists():  # 已有同 run_id。
        existing = json.loads(manifest_path.read_text(encoding="utf-8"))  # 读取。
        if existing.get("fingerprint") != fingerprint:  # 同 run_id 不同内容。
            raise PublishBlocked("RUN_ID_REUSE_WITH_DIFFERENT_INPUT")  # 必须换 run_id，不得覆盖旧输出。
        return run_dir  # 幂等：同内容重跑是 no-op，不改变已发布状态。
    (run_dir / "staging").mkdir(parents=True, exist_ok=True)  # 建目录树。
    (run_dir / "intervals.jsonl").write_text(intervals_text, encoding="utf-8")  # 规范记录。
    (run_dir / "rejected.jsonl").write_text(  # 拒绝记录：主原因唯一 + 诊断标签。
        "".join(json.dumps(item, ensure_ascii=False, sort_keys=True) + "\n" for item in adapt_result.rejected),  # 每行一条。
        encoding="utf-8",  # UTF-8。
    )  # 结束：拒绝记录。
    (run_dir / "staging" / "hourly_fragments.tsv").write_text(tsv_text, encoding="utf-8")  # MR 输入。
    (run_dir / "quality.json").write_text(json.dumps(quality, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")  # 质量报告。
    run_manifest = {  # run 元数据：状态 CANDIDATE，激活后才 PUBLISHED。
        "dataset_version": dataset_version,  # 版本。
        "runId": run_id,  # 运行ID。
        "sourceId": str(manifest.get("source_id", "")),  # 命名空间。
        "ingestBatch": str(manifest.get("ingest_batch", "")),  # 批次。
        "dataMode": str(manifest.get("data_mode", "")),  # 模式。
        "contract": "historical_interval_v1",  # 记录契约。
        "stagingContract": "station_hour_v1",  # 小时表契约（TSV 列对应）。
        "counts": {"accepted": len(adapt_result.accepted), "rejected": len(adapt_result.rejected), "hours": len(adapt_result.fragments)},  # 三账计数。
        "qualityGate": {"ok": gate_ok, "reasons": gate_reasons, "minValidRate": MIN_VALID_RATE},  # 质量门结论随 run 固化。
        "fingerprint": fingerprint,  # 内容指纹。
        "status": "CANDIDATE",  # 候选。
        "createdAt": _utc_now(),  # 创建时间。
    }  # 结束：run 清单。
    manifest_path.write_text(json.dumps(run_manifest, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")  # 写清单。
    return run_dir  # 返回。


def publish_run(runtime_root: str | Path, dataset_version: str, run_id: str) -> dict[str, Any]:  # 激活候选 run：原子替换 active manifest；旧版本保持可读。
    run_dir = Path(runtime_root) / "analytics" / "runs" / dataset_version / run_id  # 候选目录。
    manifest_path = run_dir / "run_manifest.json"  # 清单。
    if not manifest_path.exists():  # 未准备。
        raise FileNotFoundError(f"run 不存在: {run_dir}")  # 明确失败。
    run_manifest = json.loads(manifest_path.read_text(encoding="utf-8"))  # 读取。
    gate = run_manifest.get("qualityGate", {"ok": True, "reasons": []})  # 兼容旧候选。
    if not gate.get("ok", True):  # 低质量批次：禁止自动发布（需修复数据或重定范围并报告）。
        raise PublishBlocked("QUALITY_GATE:" + ";".join(gate.get("reasons", [])))  # 激活拒绝。
    if run_manifest.get("status") != "PUBLISHED":  # 幂等：已发布则不重复改时间。
        run_manifest["status"] = "PUBLISHED"  # 置为已发布。
        run_manifest["publishedAt"] = _utc_now()  # 记录发布时间。
        manifest_path.write_text(json.dumps(run_manifest, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")  # 回写。
    quality = json.loads((run_dir / "quality.json").read_text(encoding="utf-8"))  # 质量摘要进激活清单。
    return _activate(runtime_root, dataset_version, {  # 原子激活。
        "runId": run_id,  # 运行ID。
        "path": str(run_dir),  # 绝对路径，供 API 直接读取。
        "publishedAt": run_manifest.get("publishedAt", _utc_now()),  # 发布时间。
        "counts": run_manifest.get("counts", {}),  # 计数。
        "quality": {"validRate": quality.get("valid_rate"), "accepted": quality.get("accepted_count"), "rejected": quality.get("rejected_count")},  # 摘要。
        "dataMode": run_manifest.get("dataMode", ""),  # 模式。
        "sourceId": run_manifest.get("sourceId", ""),  # 命名空间。
    })  # 结束：激活。


def _activate(runtime_root: str | Path, dataset_version: str, entry: dict[str, Any]) -> dict[str, Any]:  # 同文件系统原子替换 active manifest（tmp + os.replace）。
    active_path = Path(runtime_root) / "analytics" / "active_manifest.json"  # 激活清单位置。
    active_path.parent.mkdir(parents=True, exist_ok=True)  # 确保目录。
    current: dict[str, Any] = {"active": {}}  # 默认空。
    if active_path.exists():  # 已有清单。
        current = json.loads(active_path.read_text(encoding="utf-8"))  # 读旧清单。
    current.setdefault("active", {})[dataset_version] = entry  # 覆盖该版本的指针（其它版本不动）。
    current["updatedAt"] = _utc_now()  # 更新时间。
    tmp_path = active_path.with_suffix(".json.tmp")  # 临时文件。
    tmp_path.write_text(json.dumps(current, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")  # 先写临时。
    os.replace(tmp_path, active_path)  # 原子替换，读者永远看到完整 JSON。
    return current  # 返回新清单。


def load_active_manifest(runtime_root: str | Path) -> dict[str, Any]:  # 读取激活清单；未发布过则 FileNotFoundError。
    active_path = Path(runtime_root) / "analytics" / "active_manifest.json"  # 位置。
    return json.loads(active_path.read_text(encoding="utf-8"))  # 读失败自然抛 FileNotFoundError。


def _run_hdfs(args: list[str], hdfs_bin: str = "hdfs") -> int:  # 执行 hdfs 命令，返回退出码（测试可替换）。
    completed = subprocess.run([hdfs_bin, *args], capture_output=True, text=True)  # 不继承 shell。
    return completed.returncode  # 只看退出码。


def hdfs_upload_run(  # 把候选 run 上传 HDFS（raw/rejected/staging），最后打 _SUCCESS；与本地发布是两个动作。
    runtime_root: str | Path,  # 运行缓存根。
    dataset_version: str,  # 版本。
    run_id: str,  # 运行ID。
    hdfs_base: str = "/charge_platform",  # HDFS 根目录。
    hdfs_bin: str = "hdfs",  # hdfs 可执行名。
) -> dict[str, Any]:  # 状态：UPLOADED / SKIPPED_NO_HDFS / FAILED。
    run_dir = Path(runtime_root) / "analytics" / "runs" / dataset_version / run_id  # 候选目录。
    if not run_dir.exists():  # 未准备。
        raise FileNotFoundError(f"run 不存在: {run_dir}")  # 明确失败。
    if shutil.which(hdfs_bin) is None:  # 无 hdfs 命令（本地单元环境）。
        return {"hdfs": "SKIPPED_NO_HDFS", "targets": []}  # 显式跳过，不假装成功。
    if _run_hdfs(["dfs", "-test", "-d", hdfs_base], hdfs_bin) != 0:  # 根目录不可达。
        return {"hdfs": "FAILED", "reason": f"{hdfs_base} 不可达", "targets": []}  # 如实失败。
    targets: list[str] = []  # 已上传目标。
    for sub, source in (  # （HDFS 目标子目录, 本地文件）清单。
        ("raw", "intervals.jsonl"),  # 规范记录 → raw。
        ("rejected", "rejected.jsonl"),  # 拒绝记录 → rejected。
        ("staging", "staging/hourly_fragments.tsv"),  # staging TSV。
    ):  # 结束：清单。
        local = run_dir / source  # 本地文件。
        target = f"{hdfs_base}/{sub}/{dataset_version}/{run_id}/"  # 版本化 HDFS 目录。
        if _run_hdfs(["dfs", "-mkdir", "-p", target], hdfs_bin) != 0:  # put 不会自动建目录。
            return {"hdfs": "FAILED", "reason": f"mkdir {target}", "targets": targets}  # 如实失败。
        if _run_hdfs(["dfs", "-put", "-f", str(local), target], hdfs_bin) != 0:  # put 失败。
            return {"hdfs": "FAILED", "reason": f"put {local} -> {target}", "targets": targets}  # 如实失败。
        targets.append(target + Path(source).name)  # 记录目标。
    for sub in ("raw", "staging"):  # 成功标记：put 全部完成后才 touchz。
        if _run_hdfs(["dfs", "-touchz", f"{hdfs_base}/{sub}/{dataset_version}/{run_id}/_SUCCESS"], hdfs_bin) != 0:  # 打标失败。
            return {"hdfs": "FAILED", "reason": "touchz _SUCCESS", "targets": targets}  # 如实失败。
    return {"hdfs": "UPLOADED", "targets": targets}  # 全部成功。


# 【文件讲解·答辩用】
# 功能作用：治理产物先写候选 run 目录，过质量门才允许激活；active manifest 用 os.replace 原子
#   替换；同 run_id 同内容重跑是 no-op（幂等），不同内容直接拒绝；HDFS 上传与本地发布分离，
#   _SUCCESS 只在 put 全部成功后出现。
# 技术原理：内容指纹（SHA-256）判定幂等；有效率 0.95 预警线拦截低质量批次；空批次/全拒绝批次
#   直接 PublishBlocked；失败批次不触碰 active manifest，旧版本永远可读。
# 与其他文件关系：history_adapter 提供 AdaptResult；T04 的 Java MR 消费 staging 路径；成员3 的
#   Hive/Spark/模型按 active manifest 读取同一 dataset_version/run_id。
# 答辩时可以怎么讲：发布是"先候选后激活"的两段式：失败重跑不会污染已发布版本，重复导入不会
#   重复累加，HDFS 的成功标记和本地激活是两个可分别核查的动作。
