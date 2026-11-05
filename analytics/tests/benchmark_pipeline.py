from __future__ import annotations  # 允许 Path 等现代类型标注。

import json  # 把机器、行数、耗时和内存输出成一行可归档 JSON。
import platform  # 记录操作系统和 Python 版本，避免脱离环境比较耗时。
import tempfile  # 每次基准使用隔离 runtime，不污染正式运行目录。
import time  # perf_counter 测量包含磁盘写入的墙钟时间。
import tracemalloc  # 记录 Python 管线自身的内存分配峰值。
from pathlib import Path  # 把临时目录构造成管线路径。

from src.curate_batch import curate_public_batch  # 基准对象是 D7/D8 Leeds 正式公开数据管线。


def _run_once(runtime: Path) -> tuple[dict, float, float]:  # 返回管线结果、秒数、峰值 MiB。
    tracemalloc.start()  # 从进入正式清洗前开始统计 Python 分配。
    started = time.perf_counter()  # 高精度起点。
    result = curate_public_batch(runtime)  # 准入、固化、规范化、质量、聚合和基线。
    elapsed = time.perf_counter() - started  # 完整墙钟耗时。
    _current, peak = tracemalloc.get_traced_memory()  # 只保留峰值。
    tracemalloc.stop()  # 防止影响第二遍测量。
    return result, elapsed, peak / (1024 * 1024)  # 字节换算 MiB。


def main() -> None:  # 连续跑两次，验证幂等并输出一份机器可读记录。
    with tempfile.TemporaryDirectory() as directory:  # 退出时自动清理隔离数据。
        runtime = Path(directory) / "runtime"  # 目录结构与生产 runtime/analytics 相同。
        first, first_elapsed, first_peak = _run_once(runtime)  # 第一次含 raw 复制。
        second, second_elapsed, second_peak = _run_once(runtime)  # 第二次验证 raw 幂等。
        if first["curated_rows"] != second["curated_rows"]:  # 同一输入聚合行数必须一致。
            raise SystemExit("PIPELINE_ROW_COUNT_CHANGED")  # 行数漂移属于 D9 阻断。
        if first["sha256"] != second["sha256"]:  # 两遍必须读取同一份不可变 raw。
            raise SystemExit("PIPELINE_RAW_HASH_CHANGED")  # 哈希变化属于 D9 阻断。
        curated_bytes = sum(  # 统计小时表、元数据、基线和回算证据总大小。
            path.stat().st_size
            for path in (runtime / "curated" / "public-v1").rglob("*")
            if path.is_file()
        )
    print(json.dumps({  # 一行 JSON 便于直接收进测试报告。
        "machine": platform.platform(),  # 操作系统版本。
        "python": platform.python_version(),  # Python 版本。
        "input_rows": first["quality"]["input_count"],  # 衍生 raw 行数。
        "accepted_rows": first["quality"]["accepted_count"],  # 清洗通过数。
        "rejected_rows": first["quality"]["rejected_count"],  # 清洗拒绝数。
        "curated_rows": first["curated_rows"],  # 站点小时行数。
        "curated_bytes": curated_bytes,  # curated 目录字节数。
        "first_elapsed_seconds": round(first_elapsed, 4),  # 首次含固化耗时。
        "second_elapsed_seconds": round(second_elapsed, 4),  # 幂等复跑耗时。
        "peak_memory_mb": round(max(first_peak, second_peak), 4),  # 两遍峰值取较大者。
        "data_mode": first["data_mode"],  # 必须为 PUBLIC_AGGREGATED。
        "dataset_version": first["dataset_version"],  # 必须为 public-v1。
        "raw_unchanged": first["sha256"] == second["sha256"],  # 直接展示门禁结论。
    }, ensure_ascii=False))  # 中文和布尔值保持可读。


if __name__ == "__main__":  # 直接执行脚本时进入基准，pytest 导入时不自动运行。
    main()  # 执行两遍正式管线。

# 【文件讲解·答辩用】
# 功能作用：在隔离目录连续两遍运行 Leeds 正式数据管线，记录行数、文件大小、耗时和峰值内存。
# 技术原理：TemporaryDirectory 防污染，perf_counter 测墙钟，tracemalloc 测 Python 峰值，SHA-256 证明 raw 未变。
# 代码思路：首遍含复制、第二遍测幂等；任何行数或哈希变化立即以非零退出码阻断。
# 与其他文件关系：调用 curate_batch.curate_public_batch，不再调用会生成 SIMULATED 数据的 demo_pipeline。
# 答辩时可以怎么讲：这项基准测的是正式 PUBLIC_AGGREGATED 管线，不是业务接口延迟，也不冒充真实设备压力测试。
