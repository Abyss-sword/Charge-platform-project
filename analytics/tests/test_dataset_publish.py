# T03 发布器测试：候选 run、质量门、原子激活、幂等重跑、HDFS 上传路径与成功标记。
import json  # run_manifest 与 active_manifest 都是 JSON。
from decimal import Decimal  # staging TSV 电量列解析断言。
import shutil  # 伪造 hdfs 脚本目录复用。
from pathlib import Path  # tmp_path 运行缓存。

import pytest  # 断言拒绝路径。

from src.history_adapter import adapt_csv  # 造一份合法批次作为发布输入。
from src.publish_dataset import (  # 被测模块。
    load_active_manifest,
    prepare_run,
    publish_run,
    quality_gate,
)
from src.quality import build_quality_report  # 兼容 primary_reason 的回归点。

REPO_ROOT = Path(__file__).resolve().parents[2]  # analytics/tests → 仓库根。
GOV = REPO_ROOT / "tests" / "fixtures" / "stage2" / "governance"  # 治理夹具。

META = {  # 与治理夹具清单一致。
    "source_id": "sim",
    "dataset_version": "fixture-v1",
    "data_mode": "SIMULATED",
    "ingest_batch": "batch-fixture-001",
    "ingested_at": "2026-09-13T00:00:00Z",
}


def _good_result():  # 三条合法记录的标准批次。
    return adapt_csv(GOV / "intervals_valid.csv", META)


def _prepare(runtime: Path, run_id: str = "run-001"):  # 标准备运行：适配→质量→候选目录。
    result = _good_result()  # 复用合法夹具。
    quality = build_quality_report(3, result.accepted, result.rejected)  # 质量报告。
    return prepare_run(runtime, "fixture-v1", run_id, result, quality, manifest=META)  # 候选 run 目录。


# 候选目录内容齐全且状态为 CANDIDATE；激活后才出现 active_manifest。
def test_prepare_run_writes_candidate_artifacts(tmp_path):
    run_dir = _prepare(tmp_path)  # 准备。
    assert (run_dir / "intervals.jsonl").exists()  # 规范记录。
    assert (run_dir / "rejected.jsonl").exists()  # 拒绝记录。
    assert (run_dir / "staging" / "hourly_fragments.tsv").exists()  # MR 输入。
    assert (run_dir / "quality.json").exists()  # 质量报告。
    manifest = json.loads((run_dir / "run_manifest.json").read_text(encoding="utf-8"))  # run 清单。
    assert manifest["status"] == "CANDIDATE"  # 未激活。
    with pytest.raises(FileNotFoundError):  # 未发布时不得有 active manifest。
        load_active_manifest(tmp_path)  # 触发缺失。


# 发布后 active_manifest 原子生成并指向该 run；同版本再发布新 run 时旧目录仍可读。
def test_publish_activates_and_keeps_previous_version(tmp_path):
    run_dir_a = _prepare(tmp_path, "run-001")  # 第一批。
    publish_run(tmp_path, "fixture-v1", "run-001")  # 激活。
    active = load_active_manifest(tmp_path)  # 读激活清单。
    assert active["active"]["fixture-v1"]["runId"] == "run-001"  # 指向 run-001。
    run_dir_b = _prepare(tmp_path, "run-002")  # 第二批（同数据版本新 run）。
    publish_run(tmp_path, "fixture-v1", "run-002")  # 切换。
    active = load_active_manifest(tmp_path)  # 再读。
    assert active["active"]["fixture-v1"]["runId"] == "run-002"  # 已切换。
    assert (run_dir_a / "run_manifest.json").exists()  # 旧版本目录保留。
    old = json.loads((run_dir_a / "run_manifest.json").read_text(encoding="utf-8"))  # 旧清单。
    assert old["status"] == "PUBLISHED"  # 旧版本保持已发布可读状态。


# 低质量批次：候选可写、激活被拒、已发布版本不变；空/全拒绝批次连候选都不写（S2-T03/T05 发布侧保障）。
def test_failed_batch_does_not_change_active(tmp_path):
    run_dir_a = _prepare(tmp_path, "run-001")  # 先发布一份好的。
    publish_run(tmp_path, "fixture-v1", "run-001")  # 激活。
    before = load_active_manifest(tmp_path)  # 记录激活状态。
    result = _good_result()  # 构造低质量批次：1 条通过 + 4 条拒绝（有效率 0.2）。
    quality = build_quality_report(5, result.accepted[:1], [{"reason_code": "DUPLICATE"}] * 4)  # 对账平衡的坏质量。
    ok, reasons = quality_gate(quality)  # 门结论。
    assert ok is False and "VALID_RATE_BELOW_THRESHOLD" in reasons  # 低于 0.95 预警线。
    bad_dir = prepare_run(tmp_path, "fixture-v1", "run-bad", result, quality, manifest=META)  # 候选仍可写（记账可查）。
    assert (bad_dir / "quality.json").exists()  # 拒绝账目已固化。
    from src.publish_dataset import PublishBlocked  # 专用异常。
    with pytest.raises(PublishBlocked, match="QUALITY_GATE"):  # 激活必须被拒。
        publish_run(tmp_path, "fixture-v1", "run-bad")  # 不得自动发布。
    assert load_active_manifest(tmp_path) == before  # 已发布版本未被破坏。
    assert quality_gate(build_quality_report(0, [], []))[0] is False  # 空批次门拒绝。
    from src.history_adapter import adapt_csv as _adapt  # 全拒绝批次构造。
    all_rejected = _adapt(GOV / "intervals_negative_energy.csv", META)  # 0 通过。
    with pytest.raises(PublishBlocked, match="EMPTY_OR_ALL_REJECTED"):  # 空/全拒绝必须非成功。
        prepare_run(tmp_path, "fixture-v1", "run-empty", all_rejected, build_quality_report(1, [], all_rejected.rejected), manifest=META)  # 拒绝。


# 幂等：同一批次重复导入不重复累加——同 run_id 重建后计数不变。
def test_idempotent_reimport_same_batch(tmp_path):
    _prepare(tmp_path, "run-001")  # 第一次。
    publish_run(tmp_path, "fixture-v1", "run-001")  # 激活。
    first = load_active_manifest(tmp_path)  # 快照。
    _prepare(tmp_path, "run-001")  # 重复导入同 run。
    second = load_active_manifest(tmp_path)  # 再读。
    assert second == first  # 计数与发布时间不变，无累加。


# staging TSV 固定 6 列、站点字段无制表/换行、电量保留未舍入文本。
def test_staging_tsv_format(tmp_path):
    run_dir = _prepare(tmp_path)  # 准备候选。
    tsv = (run_dir / "staging" / "hourly_fragments.tsv").read_text(encoding="utf-8")  # 读 TSV。
    rows = [line for line in tsv.splitlines() if line]  # 非空行。
    assert rows, "TSV 必须有数据行"  # 有内容。
    for row in rows:  # 逐行检查。
        cols = row.split("\t")  # 制表符拆列。
        assert len(cols) == 6, f"必须固定6列: {row}"  # 站点/小时/电量/会话数/覆盖秒/类型。
        assert "\t" not in cols[0] and "\n" not in cols[0]  # 站点无制表/换行。
        assert cols[2] and Decimal(cols[2]) >= 0  # 未舍入电量可解析且非负。
        assert cols[5] in {"METER_INTERVAL", "SESSION_ALLOCATED", "PUBLIC_HOURLY"}  # 观测类型合法。


# HDFS 上传：目标路径带 dataset_version/run_id，_SUCCESS 在 put 完成后才出现（与本地发布不是同一动作）。
def test_hdfs_upload_paths_and_success_marker(tmp_path, monkeypatch):
    run_dir = _prepare(tmp_path)  # 候选目录。
    calls: list[list[str]] = []  # 记录 hdfs 命令。

    def fake_hdfs(args, hdfs_bin="hdfs"):  # 替代真实 hdfs 命令。
        calls.append(args)  # 记录。
        if args[:2] == ["dfs", "-put"]:  # put 命令模拟成功。
            return 0  # 成功。
        return 0  # test/mkdir/touchz 都成功。

    from src import publish_dataset as pd  # 取模块打补丁。
    monkeypatch.setattr(pd.shutil, "which", lambda name: "/usr/bin/hdfs" if name == "hdfs" else None)  # 让探测认为 hdfs 存在。
    monkeypatch.setattr(pd, "_run_hdfs", fake_hdfs)  # 替换内部执行器。
    status = pd.hdfs_upload_run(tmp_path, "fixture-v1", "run-001")  # 执行上传。
    assert status["hdfs"] == "UPLOADED"  # 标记成功。
    targets = [" ".join(c) for c in calls if c[:2] == ["dfs", "-put"]]  # put 调用。
    assert any("/charge_platform/staging/fixture-v1/run-001" in t for t in targets)  # staging 路径。
    assert any("/charge_platform/raw/fixture-v1/run-001" in t for t in targets)  # raw 路径。
    assert any(c[:2] == ["dfs", "-touchz"] and "/charge_platform/staging/fixture-v1/run-001/_SUCCESS" in " ".join(c) for c in calls)  # 成功标记。


# 无 HDFS 命令时显式跳过（本地单元环境），不得假装已上传。
def test_hdfs_upload_skipped_without_binary(tmp_path, monkeypatch):
    _prepare(tmp_path)  # 候选目录。
    from src import publish_dataset as pd  # 模块。
    monkeypatch.setattr(pd.shutil, "which", lambda name: None if name == "hdfs" else "/usr/bin/" + name)  # 模拟无 hdfs。
    status = pd.hdfs_upload_run(tmp_path, "fixture-v1", "run-001")  # 执行。
    assert status["hdfs"] == "SKIPPED_NO_HDFS"  # 显式跳过。
