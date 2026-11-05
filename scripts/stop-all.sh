#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")/.." && pwd -P)"
pid_file="$project_root/runtime/all.pids"
if [ ! -f "$pid_file" ]; then echo "没有统一启动器进程记录"; echo "STOP=OK"; exit 0; fi
while IFS=' ' read -r label pid; do
 [ -n "${pid:-}" ] || continue
 case "$pid" in ''|*[!0-9]*) continue ;; esac
 if kill -0 "$pid" 2>/dev/null; then
   cmd="$(ps -p "$pid" -o args= 2>/dev/null || true)"
   case "$cmd" in *"$project_root"*)  # 只杀命令行含本项目路径的进程，不按模糊名误杀。
     kill "$pid" 2>/dev/null || true
     waited=0
     while kill -0 "$pid" 2>/dev/null && [ "$waited" -lt 10 ]; do  # 等待进程真正退出。
       sleep 0.5; waited=$((waited+1))
     done
     if kill -0 "$pid" 2>/dev/null; then
       echo "进程未退出: $label pid=$pid" >&2
       exit 1
     fi
     echo "STOPPED=$label:$pid"
     ;;
   esac
 fi
done < "$pid_file"
rm -f "$pid_file"
echo "STOP=OK"
