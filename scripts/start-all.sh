#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")/.." && pwd -P)"
stage="1"; db="$project_root/runtime/charge_platform.sqlite"; host="127.0.0.1"; port="48080"
while [ "$#" -gt 0 ]; do
 case "$1" in
  --stage) stage="$2"; shift 2 ;;
  --db) db="$2"; shift 2 ;;
  --host) host="$2"; shift 2 ;;
  --port) port="$2"; shift 2 ;;
  *) echo "用法: $0 [--stage 1|2] [--db PATH] [--host HOST] [--port PORT]" >&2; exit 2 ;;
 esac
done
pid_file="$project_root/runtime/all.pids"
port_busy() { timeout 2 bash -c "</dev/tcp/$1/$2" 2>/dev/null && return 0 || return 1; }
# 重复 start 保护：已有存活的本项目进程记录时拒绝重复启动，不覆盖 PID 文件。
if [ -f "$pid_file" ]; then
 while IFS=' ' read -r label pid; do
  [ -n "${pid:-}" ] || continue
  case "$pid" in ''|*[!0-9]*) continue ;; esac
  if kill -0 "$pid" 2>/dev/null && ps -p "$pid" -o args= 2>/dev/null | grep -q "$project_root"; then
   echo "已有运行中的启动器进程（$label pid=$pid），先执行 scripts/stop-all.sh 再启动" >&2
   exit 3
  fi
 done < "$pid_file"
fi
# 端口占用检查：目标端口被占用（含非本项目进程）时直接失败并报告。
if port_busy "$host" "$port"; then
 listener="$(ss -ltnp 2>/dev/null | grep ":$port " | head -1 || true)"
 echo "端口 $host:$port 已被占用: ${listener:-未知进程}" >&2
 exit 4
fi
started_pids=""  # 本次启动的进程，失败时逐个回收，不动其它进程。
cleanup_own() {
 for own in $started_pids; do
  kill "$own" 2>/dev/null || true
 done
}
trap cleanup_own EXIT
"$project_root/scripts/init-demo.sh" --stage "$stage" --db "$db" --port "$port"
mkdir -p "$project_root/runtime/logs"
server="$project_root/build/charge_platform_server"
[ -x "$server" ] || { echo "缺少$server，请先构建项目" >&2; exit 2; }
"$server" --host "$host" --port "$port" --db "$db" >"$project_root/runtime/logs/server.log" 2>&1 &
server_pid=$!
started_pids="$server_pid"
ready=0
for _ in $(seq 1 20); do  # 等待业务端口真正可连接，而不是固定 sleep。
 if port_busy "$host" "$port"; then ready=1; break; fi
 if ! kill -0 "$server_pid" 2>/dev/null; then break; fi
 sleep 0.5
done
if [ "$ready" != 1 ] || ! kill -0 "$server_pid" 2>/dev/null; then
 echo "SERVER=FAIL（未在10秒内就绪或进程退出，日志 runtime/logs/server.log）" >&2
 exit 1
fi
printf 'server %s\n' "$server_pid" >"$pid_file"
if [ "$stage" = "2" ]; then
 export CHARGE_PLATFORM_DB="$db"
 [ -x "$project_root/analytics/.venv/bin/python" ] || { echo "缺少analytics/.venv" >&2; exit 2; }
 if port_busy "127.0.0.1" "8000"; then echo "端口 127.0.0.1:8000 已被占用" >&2; exit 4; fi
 (cd "$project_root/analytics" && "$project_root/analytics/.venv/bin/python" -m uvicorn src.api:app --host 127.0.0.1 --port 8000 >"$project_root/runtime/logs/analytics.log" 2>&1) &
 api_pid=$!
 started_pids="$started_pids $api_pid"
 command -v npm >/dev/null || { echo "缺少npm，无法启动第二阶段大屏" >&2; exit 2; }
 if port_busy "127.0.0.1" "4173"; then echo "端口 127.0.0.1:4173 已被占用" >&2; exit 4; fi
 (cd "$project_root/dashboard" && npm run dev -- --host 127.0.0.1 --port 4173 >"$project_root/runtime/logs/dashboard.log" 2>&1) &
 dashboard_pid=$!
 started_pids="$started_pids $dashboard_pid"
 printf 'analytics %s\n' "$api_pid" >>"$pid_file"
 printf 'dashboard %s\n' "$dashboard_pid" >>"$pid_file"
fi
trap - EXIT
echo "START=OK"
echo "STAGE=$stage"
echo "DATABASE=$db"
echo "SERVER_PID=$server_pid"
[ "$stage" = "1" ] || echo "ANALYTICS_PORT=8000"
[ "$stage" = "1" ] || echo "DASHBOARD_PORT=4173"
