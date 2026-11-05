#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd -P)"
stage="1"
database_path="$project_root/runtime/charge_platform.sqlite"
port="48080"
host="127.0.0.1"
reset=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --stage) [ "$#" -ge 2 ] || exit 2; stage="$2"; shift 2 ;;
    --db) [ "$#" -ge 2 ] || exit 2; database_path="$2"; shift 2 ;;
    --port) [ "$#" -ge 2 ] || exit 2; port="$2"; shift 2 ;;
    --reset-demo-data) reset=1; shift ;;
    *) echo "用法: $0 [--stage 1|2] [--db PATH] [--port PORT] [--reset-demo-data]" >&2; exit 2 ;;
  esac
done

[ "$stage" = "1" ] || [ "$stage" = "2" ] || { echo "stage必须为1或2" >&2; exit 2; }
case "$database_path" in /*) ;; *) echo "数据库路径必须是绝对路径" >&2; exit 2 ;; esac
# 路径防护（T08）：拒绝空/根/项目源码目录等不安全目标；演示库只允许放在项目 runtime/ 内或项目外明确路径。
case "$database_path" in
  "/"|"/home"|"/usr"|"/etc"|"/var"|"/tmp") echo "拒绝根级路径: $database_path" >&2; exit 2 ;;
esac
if [ -d "$database_path" ]; then echo "目标是目录而非数据库文件: $database_path" >&2; exit 2; fi
case "$database_path" in
  "$project_root") echo "拒绝指向项目源码根目录" >&2; exit 2 ;;
  "$project_root"/*)
    case "$database_path" in
      "$project_root"/runtime/*) ;;  # runtime 内是演示库的合法位置。
      *) echo "拒绝把数据库放进项目源码目录（仅允许 $project_root/runtime/ 下）: $database_path" >&2; exit 2 ;;
    esac ;;
esac
# 运行中保护：存在存活的本项目 server 进程时禁止重置业务库（S2-T20）。
pid_file="$project_root/runtime/all.pids"
if [ "$reset" -eq 1 ] && [ -f "$pid_file" ]; then
  while IFS=' ' read -r label pid; do
    [ -n "${pid:-}" ] || continue
    case "$pid" in ''|*[!0-9]*) continue ;; esac
    if kill -0 "$pid" 2>/dev/null && ps -p "$pid" -o args= 2>/dev/null | grep -q "$project_root"; then
      echo "存在运行中的项目进程（$label pid=$pid），禁止重置业务库；先 stop-all" >&2
      exit 3
    fi
  done < "$pid_file"
fi
mkdir -p "$project_root/runtime"

if [ "$reset" -eq 1 ]; then
  "$project_root/scripts/reset-demo-database.sh" "$database_path"
elif [ ! -f "$database_path" ]; then
  echo "数据库不存在；首次初始化请显式使用 --reset-demo-data" >&2
  exit 2
fi

command -v sqlite3 >/dev/null || { echo "缺少sqlite3" >&2; exit 2; }
[ "$(sqlite3 "$database_path" 'PRAGMA integrity_check;')" = "ok" ] || { echo "数据库完整性检查失败" >&2; exit 1; }

if [ "$stage" = "2" ]; then
  [ -x "$project_root/analytics/.venv/bin/python" ] || { echo "缺少analytics/.venv，无法启动第二阶段分析" >&2; exit 2; }
  [ -d "$project_root/dashboard/node_modules" ] || { echo "缺少dashboard/node_modules，无法启动第二阶段大屏" >&2; exit 2; }
fi

echo "INIT=OK"
echo "STAGE=$stage"
echo "DATABASE=$database_path"
echo "SERVER=$host:$port"

