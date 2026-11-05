#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")/.." && pwd -P)"
stage="1"; db="$project_root/runtime/charge_platform.sqlite"; host="127.0.0.1"; port="48080"; api_port="8000"
while [ "$#" -gt 0 ]; do
 case "$1" in
  --stage) stage="$2"; shift 2 ;;
  --db) db="$2"; shift 2 ;;
  --host) host="$2"; shift 2 ;;
  --port) port="$2"; shift 2 ;;
  --api-port) api_port="$2"; shift 2 ;;
  *) echo "用法: $0 [--stage 1|2] [--db PATH] [--host HOST] [--port PORT] [--api-port PORT]" >&2; exit 2 ;;
 esac
done
[ -f "$db" ] || { echo "DATABASE=FAIL"; exit 1; }
[ "$(sqlite3 "$db" 'PRAGMA integrity_check;')" = "ok" ] || { echo "DATABASE=FAIL"; exit 1; }
if timeout 2 bash -c "</dev/tcp/$host/$port" 2>/dev/null; then echo "SERVER=OK"; else echo "SERVER=FAIL"; exit 1; fi
if [ "$stage" = "2" ]; then
 curl --fail --silent --show-error "http://127.0.0.1:$api_port/health" >/dev/null || { echo "ANALYTICS=FAIL"; exit 1; }
 echo "ANALYTICS=OK"
 timeout 2 bash -c "</dev/tcp/127.0.0.1/4173" 2>/dev/null || { echo "DASHBOARD=FAIL"; exit 1; }
 echo "DASHBOARD=OK"
fi
echo "DATABASE=OK"
echo "HEALTH=OK"
