from __future__ import annotations  # 3.10 兼容的延迟注解。

import csv  # 只读表头和统计非空行，不在这里做字段清洗。
import hashlib  # SHA-256 固化源文件，防止同批次被悄悄换内容。
import json  # 写出 source_manifest.json 与 batch_manifest.json。
import shutil  # copy2 保留时间戳，把源 CSV 冻到 runtime/raw/<batch>/。
from pathlib import Path  # 批次目录与目标文件路径。

from .source_registry import AdmissionError, PRIVACY_FIELDS, load_manifest  # 清单校验与隐私列黑名单。


def _sha256(path: Path) -> str:  # 1MB 分块算哈希，大 CSV 不整文件进内存。
    digest = hashlib.sha256()  # 新建 SHA-256 状态。
    with path.open("rb") as stream:  # 二进制读，避免换行符被文本模式改掉导致哈希变。
        for block in iter(lambda: stream.read(1024 * 1024), b""):  # 读到空字节结束。
            digest.update(block)  # 累加当前块。
    return digest.hexdigest()  # 小写十六进制，写入 batch_manifest。


def file_digest(path: str | Path) -> str:  # 给 D8 在清洗前后核对 raw 未被改写。
    return _sha256(Path(path))  # 与入库时同一套分块 SHA-256。


def _csv_metadata(path: Path) -> tuple[list[str], int]:  # 读表头，并统计至少有一个非空单元格的数据行。
    with path.open("r", encoding="utf-8-sig", newline="") as stream:  # utf-8-sig 去掉 Excel BOM。
        reader = csv.reader(stream)  # 按 CSV 规则拆列，不手动 split。
        try:  # 尝试：第一行当表头并去空白。
            headers = [value.strip() for value in next(reader)]  # 第一行当表头并去空白。
        except StopIteration as exc:  # 文件一个单元格都没有。
            raise AdmissionError("source file is empty") from exc  # 空文件不准入。
        count = sum(1 for row in reader if any(cell.strip() for cell in row))  # 全空行不计入 row_count。
    return headers, count  # 给隐私列检查和清单 row_count 用。


def ingest_source(manifest_path: str | Path, source_path: str | Path, runtime_root: str | Path) -> Path:  # 清单准入 → 隐私表头检查 → 哈希固化 CSV → 写出两份 JSON 清单。
    manifest = load_manifest(manifest_path)  # 先过 source_registry，失败则整批不落盘。
    source = Path(source_path)  # 来源 CSV 路径。
    if not source.is_file() or source.suffix.lower() != ".csv":  # 必须是已存在的 .csv。
        raise AdmissionError("source must be an existing CSV file")  # 拒绝目录或其它后缀。
    headers, row_count = _csv_metadata(source)  # 表头 + 非空行数。
    private = sorted({h.lower() for h in headers}.intersection(PRIVACY_FIELDS))  # 表头与隐私黑名单求交。
    if private:  # 发现手机号/车牌等列名。
        raise AdmissionError("privacy fields found: " + ", ".join(private))  # 点名哪些列违规。

    batch_dir = Path(runtime_root) / "raw" / manifest.batch  # runtime/raw/<ingest_batch>。
    batch_dir.mkdir(parents=True, exist_ok=True)  # 批次目录可重复运行，不先删旧文件。
    target = batch_dir / source.name  # 固化后的 CSV 仍用原文件名。
    source_hash = _sha256(source)  # 当前源文件指纹。
    if target.exists() and _sha256(target) != source_hash:  # 同名已存在但内容变了，禁止覆盖。
        raise AdmissionError("existing raw batch differs from source")  # 保护已固化 raw，避免静默换数。
    if not target.exists():  # 第一次摄入才复制。
        shutil.copy2(source, target)  # 连同 mtime 一起拷，便于对照原始文件。
    snapshot = batch_dir / "source_manifest.json"  # 来源清单快照，已存在则不改（保持首次准入记录）。
    if not snapshot.exists():  # 只在首次写入，防止后改清单污染历史批次。
        snapshot.write_text(json.dumps(manifest.values, ensure_ascii=False, indent=2), encoding="utf-8")  # 中文字段原样保留。
    batch_manifest = {  # 给下游清洗读的批次元数据。
        "contract": "charge_event_v2", "ingest_batch": manifest.batch,  # 事件契约名与批次号。
        "data_mode": manifest.mode, "dataset_name": manifest.values["dataset_name"],  # 模式与数据集名随行传播。
        "dataset_version": manifest.values["dataset_version"], "source_file": source.name,  # 版本与文件名。
        "sha256": source_hash, "row_count": row_count,  # 指纹与行数，质量对账用 input_count。
        "coverage_start": manifest.values["coverage_start"], "coverage_end": manifest.values["coverage_end"]  # 覆盖区间原样记下。
    }  # 结束：给下游清洗读的批次元数据。
    (batch_dir / "batch_manifest.json").write_text(  # 每次摄入都重写批次清单（哈希应一致）。
        json.dumps(batch_manifest, ensure_ascii=False, indent=2), encoding="utf-8"  # UTF-8 缩进，方便 diff。
    )  # 结束：每次摄入都重写批次清单（哈希应一致）。
    return batch_dir  # 返回批次目录，demo_pipeline 接着 normalize 这份 CSV。


def main(argv: list[str] | None = None) -> int:  # 命令行准入入口：只固化 raw，不清洗、不训练。
    import argparse  # 局部导入，避免测试 import ingest_raw 时多一次 argparse 成本。
    parser = argparse.ArgumentParser(description="Admit a source CSV and freeze an immutable raw batch.")  # 说明本命令不改源文件。
    parser.add_argument("--manifest", required=True, help="UTF-8 JSON source manifest")  # 来源清单路径。
    parser.add_argument("--source", required=True, help="Source CSV; copied, never rewritten")  # 源 CSV。
    parser.add_argument(  # runtime 根，默认仓库 runtime/analytics。
        "--runtime",
        default=str(Path(__file__).resolve().parents[2] / "runtime" / "analytics"),
        help="Analytics runtime root containing raw/",
    )  # 结束 runtime 参数。
    args = parser.parse_args(argv)  # 解析命令行。
    try:  # 准入失败用退出码 2，方便脚本判断“没有可用批次”。
        batch_dir = ingest_source(args.manifest, args.source, args.runtime)  # 校验、复制、写清单。
    except AdmissionError as error:  # 缺字段、隐私表头、哈希冲突等。
        print(f"ADMISSION_REJECTED={error}")  # 中文或英文原因都打印。
        return 2  # 非 0，且 ingest_source 在抛错前不会留下该批次 CSV。
    batch_manifest = json.loads((batch_dir / "batch_manifest.json").read_text(encoding="utf-8"))  # 读刚写的批次清单。
    print(f"RAW_BATCH={batch_dir}")  # 批次目录。
    print(f"DATA_MODE={batch_manifest['data_mode']}")  # 模式，答辩核对不是把 SIMULATED 改名。
    print(f"INGEST_BATCH={batch_manifest['ingest_batch']}")  # 批次号。
    print(f"ROW_COUNT={batch_manifest['row_count']}")  # 行数。
    print(f"SHA256={batch_manifest['sha256']}")  # 源指纹。
    return 0  # 成功。


if __name__ == "__main__":  # python -m src.ingest_raw --manifest ... --source ...
    raise SystemExit(main())  # 把退出码交给 shell。

# 【文件讲解·答辩用】
# 功能作用：把通过准入的 CSV 原样冻到 runtime/raw，并留下清单和哈希，清洗只读这份拷贝。
# 技术原理：SHA-256 检测同批次被替换；隐私列在表头阶段拦截；copy2 不做字段变换。
# 代码思路：校验清单 → 检查 CSV → 冲突则失败 → 无文件才复制 → 写 source/batch 两份 JSON。
# 与其他文件关系：依赖 source_registry；被 demo_pipeline.ingest_source 调用；normalize 读 batch_dir 下的 CSV。
# 答辩时可以怎么讲：raw 层只固化不修改，后面清洗失败可以对照哈希证明“我们分析的就是当初那份文件”。
