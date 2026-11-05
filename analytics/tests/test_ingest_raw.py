# 本文件用 csv 只出现在被测数据准备里；入库函数自己会再读一遍表头。
import csv
# 读回 batch_manifest.json，核对行数和契约名是否写进血缘。
import json
# tmp_path 是 pytest 临时目录；Path 用来拼 CSV、清单和 runtime 根。
from pathlib import Path

# 用 pytest.raises 断言隐私表头在拷贝前就被 AdmissionError 拦住。
import pytest

# ingest_source 是 raw 层唯一入口：校验清单、扫表头、固化副本、写批次清单。
from src.ingest_raw import ingest_source
# 隐私冲突和清单失败共用 AdmissionError，与 source_registry 保持同一异常类型。
from src.source_registry import AdmissionError


# 往指定路径写一份声明 SIMULATED、无隐私字段的 JSON 清单，供入库函数读取。
def manifest(path: Path):
    # 把完整必填字段序列化成 UTF-8 JSON，模拟 manifests/source.example.json 的形状。
    path.write_text(json.dumps({
        # 名称、提供方、来源 URL 与 test_source_registry 底板一致，避免两套夹具分叉。
        "dataset_name": "demo", "provider": "member3", "source_url": "local://demo",
        # 用途仍限制在教学分析，禁止画像，防止测试数据被解释成可商用。
        "allowed_uses": ["teaching-analysis"], "prohibited_uses": ["profiling"],
        # 模拟模式 + v1 版本；acquired_at 用日期即可，校验不要求必须带时区偏移。
        "acquired_at": "2026-09-05", "data_mode": "SIMULATED", "dataset_version": "v1",
        # 覆盖两天、允许再分发，满足覆盖区间和 redistribution 声明。
        "coverage_start": "2026-09-01", "coverage_end": "2026-09-02", "redistribution_allowed": True,
        # 留存策略 course；fields 只声明 id，与下面那份无隐私 CSV 的列对齐。
        "retention_policy": "course", "privacy_boundary": "no pii", "ingest_batch": "batch-1", "fields": ["id"]
        # write_text 在这里结束 JSON 写入，清单文件此时已落在 tmp_path。
    }), encoding="utf-8")


# 同一份源文件入库两次，批次目录应相同，且源文件字节不能被入库过程改掉。
def test_raw_copy_is_traceable_and_idempotent(tmp_path):
    # 写一行业务 CSV：只有 id/value，没有隐私列，用来走通拷贝和清单。
    source = tmp_path / "events.csv"; source.write_text("id,value\n1,2\n", encoding="utf-8")
    # 清单与 CSV 放在同一临时目录，ingest_source 会先 load_manifest 再读表头。
    source_manifest = tmp_path / "source.json"; manifest(source_manifest)
    # 第一次入库：应在 runtime 下按 batch-1 建目录，拷贝 CSV 并写 batch_manifest.json。
    batch = ingest_source(source_manifest, source, tmp_path / "runtime")
    # 记住源文件字节，后面用来证明入库只拷贝、不回写源。
    before = source.read_bytes()
    # 再跑一次同一批：幂等要求返回同一批次目录，而不是新建 batch-1-2。
    batch2 = ingest_source(source_manifest, source, tmp_path / "runtime")
    # 两次返回路径相同，且源 CSV 内容未被改动。
    assert batch == batch2 and source.read_bytes() == before
    # 批次清单是血缘入口，后面归一化和质量报告都靠这里的 row_count / contract。
    batch_manifest = json.loads((batch / "batch_manifest.json").read_text())
    # 非空数据行只有 1 条（表头不算），必须原样记入清单。
    assert batch_manifest["row_count"] == 1
    # 契约名必须是 charge_event_v2，和 analytics/contracts 以及流水线打印的 CONTRACT 对齐。
    assert batch_manifest["contract"] == "charge_event_v2"


# CSV 表头一旦出现 phone_number，即使清单没写这个字段，也必须在拷贝前失败。
def test_private_header_is_rejected_before_raw_copy(tmp_path):
    # 故意写带手机号列的 CSV，模拟有人把原始采集表直接丢进分析目录。
    source = tmp_path / "events.csv"; source.write_text("id,phone_number\n1,13800000000\n", encoding="utf-8")
    # 清单仍声明无隐私、fields 只有 id，用来证明拦截发生在“读表头”而不是“信清单”。
    source_manifest = tmp_path / "source.json"; manifest(source_manifest)
    # ingest_source 内部用表头与 PRIVACY_FIELDS 求交，命中则抛 AdmissionError。
    with pytest.raises(AdmissionError, match="privacy"):
        # runtime 目录此时不应留下这份带手机号的 raw 副本。
        ingest_source(source_manifest, source, tmp_path / "runtime")


# 公开聚合 CSV 准入后应留下可追溯批次，且源文件字节不变。
def test_public_aggregated_source_creates_raw_batch(tmp_path):
    from src.public_aggregated import transform_leeds_sessions

    upstream = tmp_path / "leeds.csv"
    upstream.write_text(
        "Charging event,Unique User ID,CP ID,Connector,Start Date,Start Time,End Date,End Time,Total kWh,Site,Model\n"
        "1,User A,10,1,01/01/2021,07:30,01/01/2021,08:30,5.00,Woodhouse Lane Car Park,APT\n"
        "2,User B,11,1,01/01/2021,09:05,01/01/2021,09:45,4.00,Wellington Place,APT\n",
        encoding="utf-8",
    )
    source = tmp_path / "hourly_charge_events.csv"
    result = transform_leeds_sessions(upstream, source)
    source_manifest = tmp_path / "source.json"
    source_manifest.write_text(json.dumps({
        "dataset_name": "member3-public-aggregated-hourly-load", "provider": "member3",
        "source_url": "local://test", "allowed_uses": ["teaching-analysis"],
        "prohibited_uses": ["profiling"], "acquired_at": "2026-09-10",
        "data_mode": "PUBLIC_AGGREGATED", "dataset_version": "public-v1",
        "coverage_start": "2026-01-06", "coverage_end": "2026-01-08",
        "redistribution_allowed": True, "retention_policy": "course",
        "privacy_boundary": "no pii", "ingest_batch": "public-20260910-001",
        "fields": ["source_record_id"],
    }), encoding="utf-8")
    before = source.read_bytes()
    runtime = tmp_path / "runtime"
    batch = ingest_source(source_manifest, source, runtime)
    assert source.read_bytes() == before
    batch_manifest = json.loads((batch / "batch_manifest.json").read_text(encoding="utf-8"))
    assert batch_manifest["data_mode"] == "PUBLIC_AGGREGATED"
    assert batch_manifest["row_count"] == result["output_rows"] == 9
    assert "Unique User ID" not in source.read_text(encoding="utf-8")
    assert (batch / source.name).is_file()


# 不合规来源失败后，不得留下可用 raw 批次目录或 CSV 副本。
def test_rejected_source_does_not_create_usable_raw_batch(tmp_path):
    source = tmp_path / "events.csv"
    source.write_text("id,value\n1,2\n", encoding="utf-8")
    source_manifest = tmp_path / "source.json"
    manifest(source_manifest)
    broken = json.loads(source_manifest.read_text(encoding="utf-8"))
    broken.pop("coverage_end")
    source_manifest.write_text(json.dumps(broken), encoding="utf-8")
    runtime = tmp_path / "runtime"
    with pytest.raises(AdmissionError, match="coverage_end"):
        ingest_source(source_manifest, source, runtime)
    batch_dir = runtime / "raw" / "batch-1"
    assert not batch_dir.exists() or not any(batch_dir.glob("*.csv"))


# 命令行入口打印 PUBLIC_AGGREGATED，非法清单以退出码 2 拒绝。
def test_ingest_cli_admits_public_source_and_rejects_privacy(tmp_path):
    from src.ingest_raw import main
    from src.public_aggregated import transform_leeds_sessions

    upstream = tmp_path / "leeds.csv"
    upstream.write_text(
        "Charging event,Unique User ID,CP ID,Connector,Start Date,Start Time,End Date,End Time,Total kWh,Site,Model\n"
        "1,User A,10,1,01/01/2021,07:30,01/01/2021,08:30,5.00,Woodhouse Lane Car Park,APT\n",
        encoding="utf-8",
    )
    source = tmp_path / "hourly_charge_events.csv"
    transform_leeds_sessions(upstream, source)
    source_manifest = tmp_path / "source.json"
    source_manifest.write_text(json.dumps({
        "dataset_name": "public", "provider": "member3", "source_url": "local://test",
        "allowed_uses": ["teaching-analysis"], "prohibited_uses": ["profiling"],
        "acquired_at": "2026-09-10", "data_mode": "PUBLIC_AGGREGATED",
        "dataset_version": "public-v1", "coverage_start": "2026-01-06",
        "coverage_end": "2026-01-08", "redistribution_allowed": True,
        "retention_policy": "course", "privacy_boundary": "no pii",
        "ingest_batch": "cli-public-1", "fields": ["source_record_id"],
    }), encoding="utf-8")
    runtime = tmp_path / "runtime"
    assert main(["--manifest", str(source_manifest), "--source", str(source), "--runtime", str(runtime)]) == 0
    private_csv = tmp_path / "private.csv"
    private_csv.write_text("id,phone_number\n1,13800000000\n", encoding="utf-8")
    assert main(["--manifest", str(source_manifest), "--source", str(private_csv), "--runtime", str(runtime)]) == 2
    assert not (runtime / "raw" / "cli-public-1" / "private.csv").exists()

# 【文件讲解·答辩用】
# 功能作用：验证 raw 入库两件事——可追溯且可重复跑；隐私列在拷贝前就被拒绝。
#   课程要求“原始文件要固化、不能改源文件、批次可复现”。本测试把这三句变成断言。
# 技术原理：ingest_source 先 load_manifest，再读 CSV 表头做隐私求交，再 SHA-256 拷贝到
#   runtime/<batch>/，并写 batch_manifest.json（行数、契约、校验和）。第二次用同一清单
#   和同一文件调用时，应回到同一批次目录，这就是幂等。pytest 的 tmp_path 保证每条用例
#   都在干净目录里，不会污染仓库 runtime。
# 代码思路：正向用例写无隐私 CSV，入库两次比路径、比源字节、再打开批次清单核对
#   row_count=1 和 contract=charge_event_v2。负向用例只改表头加 phone_number，清单故意
#   不声明该列，证明门禁看的是文件本身而不是声明。
# 与其他文件关系：实现是 analytics/src/ingest_raw.py；清单规则在 source_registry.py；
#   隐私字段集合 PRIVACY_FIELDS 两边共用。demo_pipeline.py 的 run_pipeline 会调
#   ingest_source。test_source_registry.py 测清单字段，本文件测“文件表头”。后面的
#   test_normalize.py 吃的是已经入库的 CSV，进不到带手机号的数据。
# 答辩讲法：先说“我们不直接分析原始采集表，先拷一份 raw 并写批次清单”。演示两次
#   入库路径相同、源文件没被改。再展示带 phone_number 的表头会在拷贝前失败——这是
#   隐私边界，不是事后清洗。被问契约名时，指向 charge_event_v2。
