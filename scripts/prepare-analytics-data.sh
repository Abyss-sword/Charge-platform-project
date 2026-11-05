#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")/.." && pwd -P)"
python_bin="$project_root/analytics/.venv/bin/python"
[ -x "$python_bin" ] || { echo "缺少analytics/.venv" >&2; exit 2; }
export CHARGE_PLATFORM_DB="${CHARGE_PLATFORM_DB:-$project_root/runtime/charge_platform.sqlite}"
if [ "$#" -eq 0 ]; then
  (cd "$project_root/analytics" && "$python_bin" -m src.demo_pipeline)
  exit $?
fi
# 第二阶段：manifest 参数必须被真正消费——historical_interval_v1 清单走治理发布链（T03/T08）。
manifest="$1"
[ -f "$manifest" ] || { echo "manifest不存在: $manifest" >&2; exit 2; }
case "$manifest" in *.json) ;; *) echo "manifest必须为JSON" >&2; exit 2 ;; esac
contract="$("$python_bin" - "$manifest" <<'PY'
import json, sys
print(json.load(open(sys.argv[1], encoding="utf-8")).get("contract", ""))
PY
)"
case "$contract" in
  historical_interval_v1)
    [ "$#" -ge 3 ] || { echo "用法: $0 <manifest.json> <source.csv> <run_id>（historical_interval_v1 需要来源CSV与run_id）" >&2; exit 2; }
    source_csv="$2"
    [ -f "$source_csv" ] || { echo "来源CSV不存在: $source_csv" >&2; exit 2; }
    case "$source_csv" in *.csv) ;; *) echo "来源必须是CSV" >&2; exit 2 ;; esac
    run_id="$3"
    case "$run_id" in ""|*[!A-Za-z0-9._-]*) echo "run_id 只允许字母数字._-" >&2; exit 2 ;; esac
    manifest="$(cd "$(dirname "$manifest")" && pwd)/$(basename "$manifest")"  # 先绝对化，治理模块在 analytics/ 下运行。
    source_csv="$(cd "$(dirname "$source_csv")" && pwd)/$(basename "$source_csv")"
    (cd "$project_root/analytics" && "$python_bin" -m src.run_governance \
      --manifest "$manifest" --source-csv "$source_csv" --run-id "$run_id" \
      --runtime-root "$project_root/runtime")
    ;;
  charge_event_v2)
    (cd "$project_root/analytics" && "$python_bin" -m src.demo_pipeline)  # 既有业务导出链路保持不变。
    ;;
  *) echo "未知契约: ${contract:-<缺失>}，manifest 必须声明 contract 字段" >&2; exit 2 ;;
esac
