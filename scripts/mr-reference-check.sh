#!/usr/bin/env bash
# MR 结果与 Python Decimal 参考实现比对（T04）：最终两位/未舍入文本必须完全一致。
# 用法: mr-reference-check.sh <staging.tsv 本地文件> <mr输出.tsv 本地文件>
set -euo pipefail
project_root="$(cd "$(dirname "$0")/.." && pwd -P)"
python_bin="$project_root/analytics/.venv/bin/python"
[ -x "$python_bin" ] || python_bin="$(command -v python3)"
[ "$#" -eq 2 ] || { echo "用法: $0 <staging.tsv> <mr_output.tsv>" >&2; exit 2; }
[ -f "$1" ] || { echo "staging 文件不存在: $1" >&2; exit 2; }
[ -f "$2" ] || { echo "MR输出文件不存在: $2" >&2; exit 2; }
"$python_bin" - "$1" "$2" <<'PY'
import sys
from decimal import Decimal

KIND_RANK = {"METER_INTERVAL": 3, "SESSION_ALLOCATED": 2, "PUBLIC_HOURLY": 1}

def reference(staging_path):  # 与 HourlyAggregator 相同语义的 Decimal 参考实现。
    grouped = {}
    with open(staging_path, encoding="utf-8") as stream:
        for line in stream:
            line = line.rstrip("\n")
            if not line:
                continue
            cols = line.split("\t")
            assert len(cols) == 6, f"staging 列数错误: {line}"
            key = (cols[0], cols[1])
            bucket = grouped.setdefault(key, {"energy": Decimal("0"), "sessions": 0, "coverage": 0, "rank": -1, "kind": "PUBLIC_HOURLY"})
            bucket["energy"] += Decimal(cols[2])
            bucket["sessions"] += int(cols[3])
            bucket["coverage"] = max(bucket["coverage"], int(cols[4]))
            rank = KIND_RANK[cols[5]]
            if rank > bucket["rank"]:
                bucket["rank"], bucket["kind"] = rank, cols[5]
    rows = []
    for (station, hour) in sorted(grouped):  # 字典序 = MR Text 键序。
        bucket = grouped[(station, hour)]
        rows.append("\t".join([station, hour, format(bucket["energy"], "f"), str(bucket["sessions"]), str(bucket["coverage"]), bucket["kind"]]))
    return rows

expected = reference(sys.argv[1])
actual = [line for line in open(sys.argv[2], encoding="utf-8").read().splitlines() if line]
if expected == actual:
    print(f"MR_REFERENCE=OK ROWS={len(actual)}")
    sys.exit(0)
print("MR_REFERENCE=MISMATCH")
for index in range(max(len(expected), len(actual))):
    left = expected[index] if index < len(expected) else "<缺行>"
    right = actual[index] if index < len(actual) else "<缺行>"
    if left != right:
        print(f"EXPECTED[{index}]={left}")
        print(f"ACTUAL  [{index}]={right}")
sys.exit(1)
PY
