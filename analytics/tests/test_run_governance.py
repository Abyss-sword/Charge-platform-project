# -*- coding: utf-8 -*-
# T18 故障隔离回归：依赖不可用时批次必须如实失败，不得把新批次标成成功。
import json  # 读 active manifest 断言未被替换。
from pathlib import Path  # 定位治理夹具。

from src import run_governance as rg  # 被测入口（命令行 main）。

REPO_ROOT = Path(__file__).resolve().parents[2]  # analytics/tests → 仓库根。
GOV = REPO_ROOT / "tests" / "fixtures" / "stage2" / "governance"  # 治理夹具。
MANIFEST = GOV / "manifest_simulated_v1.json"  # 合法来源清单。
SOURCE_CSV = GOV / "intervals_valid.csv"  # 合法来源行。


def _run(tmp_path: Path, run_id: str, upload_status: dict, monkeypatch) -> int:
    monkeypatch.setattr(rg, "hdfs_upload_run", lambda *a, **k: upload_status)  # 固定 HDFS 结果。
    return rg.main([  # 执行治理链。
        "--manifest", str(MANIFEST),  # 清单。
        "--source-csv", str(SOURCE_CSV),  # 来源。
        "--run-id", run_id,  # 运行 ID。
        "--runtime-root", str(tmp_path),  # 隔离运行缓存。
    ])  # 结束：参数。


# HDFS 存在但上传失败（FAILED）：不得激活新版本，必须返回非 0（03 文档 S2-T18）。
def test_hdfs_failed_aborts_batch_and_keeps_active_unchanged(tmp_path, monkeypatch, capsys):
    first = _run(tmp_path, "run-ok", {"hdfs": "SKIPPED_NO_HDFS", "targets": []}, monkeypatch)  # 先有一次本地上传跳过（模拟单元环境）作为基线发布。
    assert first == 0  # 基线发布成功。
    active_before = json.loads((tmp_path / "analytics" / "active_manifest.json").read_text(encoding="utf-8"))  # 记录基线激活。

    code = _run(tmp_path, "run-hdfs-fail", {"hdfs": "FAILED", "reason": "put failed", "targets": []}, monkeypatch)  # HDFS 失败批次。
    assert code != 0  # 依赖不可用：批次失败，返回非 0。
    output = capsys.readouterr().out  # 捕获输出。
    assert "HDFS=FAILED" in output  # 如实报告失败状态，不伪装成功。
    assert "PUBLISHED_RUN=run-hdfs-fail" not in output  # 不得宣称新批次已发布。

    active_after = json.loads((tmp_path / "analytics" / "active_manifest.json").read_text(encoding="utf-8"))  # 失败后激活指针。
    assert active_after == active_before  # 旧版本保持可读且未被新批次覆盖。


# 本地无 hdfs 命令（SKIPPED_NO_HDFS）：允许本地发布（单元/离线环境的既有语义不变）。
def test_hdfs_skipped_still_publishes_locally(tmp_path, monkeypatch):
    code = _run(tmp_path, "run-local", {"hdfs": "SKIPPED_NO_HDFS", "targets": []}, monkeypatch)  # 无 HDFS 环境。
    assert code == 0  # 本地发布照常成功。
    active = json.loads((tmp_path / "analytics" / "active_manifest.json").read_text(encoding="utf-8"))  # 激活指针。
    assert active["active"]["fixture-v1"]["runId"] == "run-local"  # 已激活。
