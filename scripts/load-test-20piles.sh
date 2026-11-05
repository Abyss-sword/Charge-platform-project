#!/usr/bin/env bash
# 20桩×1Hz×600秒 业务负载压测（T08/S2-T21，成员1）。
# 自包含：独立工作目录+独立数据库（补足到N个桩），不触碰演示库与生产数据。
# 负载由 scripts/load_pile_client.py 产生（逐帧延迟统计；不修改成员3模拟器）。
# 产出：发送/确认/拒绝帧计数、延迟分位数、服务端CPU/内存采样、库完整性与外键检查。
set -euo pipefail
project_root="$(cd "$(dirname "$0")/.." && pwd -P)"
server_bin="$project_root/build/charge_platform_server"
client="$project_root/scripts/load_pile_client.py"
python_bin="$project_root/analytics/.venv/bin/python"
[ -x "$python_bin" ] || python_bin="$(command -v python3)"
workdir="$project_root/runtime/load-test"
piles=20
duration=600
rate=1
port=48099
while [ "$#" -gt 0 ]; do
 case "$1" in
  --server-bin) server_bin="$2"; shift 2 ;;
  --workdir) workdir="$2"; shift 2 ;;
  --piles) piles="$2"; shift 2 ;;
  --duration) duration="$2"; shift 2 ;;
  --rate) rate="$2"; shift 2 ;;
  --port) port="$2"; shift 2 ;;
  *) echo "用法: $0 [--piles N] [--duration 秒] [--rate Hz] [--port N] [--workdir PATH]" >&2; exit 2 ;;
 esac
done
case "$piles" in ''|*[!0-9]*) echo "piles 必须是整数" >&2; exit 2 ;; esac
case "$duration" in ''|*[!0-9]*) echo "duration 必须是秒数" >&2; exit 2 ;; esac
[ -x "$server_bin" ] || { echo "缺少服务端 $server_bin" >&2; exit 2; }
[ -f "$client" ] || { echo "缺少压测客户端 $client" >&2; exit 2; }
command -v sqlite3 >/dev/null || { echo "缺少sqlite3" >&2; exit 2; }
if timeout 2 bash -c "</dev/tcp/127.0.0.1/$port" 2>/dev/null; then
  echo "端口 $port 已被占用，压测需要独占端口" >&2
  exit 4
fi
mkdir -p "$workdir/logs" "$workdir/state"
db="$workdir/loadtest.sqlite"
rm -f "$db" "$db".bak.* 2>/dev/null || true
"$project_root/scripts/reset-demo-database.sh" "$db" >/dev/null  # 独立压测库，备份逻辑同样生效。
# 补足到 N 个桩：种子库 101-112，按同一模式追加 113+，全部挂在1号站。
extra=$((piles - 12))
if [ "$extra" -gt 0 ]; then
  sqlite3 -bail "$db" "WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM seq WHERE n < $extra)
    INSERT OR IGNORE INTO pile_info (id, station_id, pile_sn, charge_type, rated_power_kw, status, last_heartbeat_at, created_at)
    SELECT 112 + n, 1, printf('PILE-LOAD-%02d', 12 + n), 'FAST', 60.0, 'IDLE', NULL, '2026-08-01T00:00:00Z' FROM seq;"
fi
actual_piles="$(sqlite3 "$db" 'SELECT COUNT(*) FROM pile_info;')"
[ "$actual_piles" -ge "$piles" ] || { echo "桩数量不足: $actual_piles < $piles" >&2; exit 2; }
"$server_bin" --host 127.0.0.1 --port "$port" --db "$db" >"$workdir/logs/server.log" 2>&1 &
server_pid=$!
cleanup() { kill "$server_pid" 2>/dev/null || true; }
trap cleanup EXIT
ready=0
for _ in $(seq 1 20); do
  if timeout 1 bash -c "</dev/tcp/127.0.0.1/$port" 2>/dev/null; then ready=1; break; fi
  sleep 0.5
done
[ "$ready" = 1 ] || { echo "SERVER=FAIL 压测服务端未就绪" >&2; exit 1; }
echo "LOADTEST_SERVER_PID=$server_pid"
echo "LOADTEST_PILES=$piles"
echo "LOADTEST_DURATION=$duration"
echo "LOADTEST_RATE=$rate"
# 启动 N 个客户端进程（每桩一条长连接、每秒1帧、设备时间单调递增）。
pids=""
for i in $(seq 1 "$piles"); do
  pile_id=$((100 + i))
  "$python_bin" "$client" --pile "$pile_id" --host 127.0.0.1 --port "$port" \
    --duration "$duration" --rate "$rate" \
    >"$workdir/logs/pile-$pile_id.log" 2>&1 &
  pids="$pids $!"
done
(
  while kill -0 "$server_pid" 2>/dev/null; do  # 每5秒采样服务端 CPU%/RSS。
    ps -p "$server_pid" -o %cpu=,rss= 2>/dev/null >>"$workdir/logs/server-samples.txt" || break
    sleep 5
  done
) &
sampler_pid=$!
fail=0
for pid in $pids; do  # 等待全部客户端退出并收集退出码。
  wait "$pid" || fail=1
done
kill "$sampler_pid" 2>/dev/null || true
sleep 1
# 用 Python 聚合各桩汇总行（SENT/ACK/REJ/RECONN 与延迟分位数最大值）。
read -r sent acked rejected reconnects p95_max lat_max <<<"$("$python_bin" - "$piles" "$workdir" <<'PY'
import sys
from pathlib import Path

piles = int(sys.argv[1])
workdir = Path(sys.argv[2])
sent = acked = rejected = reconnects = 0
p95_max = lat_max = 0.0
missing = 0
for i in range(1, piles + 1):
    log = workdir / "logs" / f"pile-{100 + i}.log"
    if not log.exists():
        missing += 1
        continue
    values = {}
    for line in log.read_text(encoding="utf-8").splitlines():
        if line.startswith("PILE="):
            for token in line.split():
                if "=" in token:
                    key, _, value = token.partition("=")
                    values[key] = value
    sent += int(values.get("SENT", "0"))
    acked += int(values.get("ACK", "0"))
    rejected += int(values.get("REJ", "0"))
    reconnects += int(values.get("RECONN", "0"))
    p95_max = max(p95_max, float(values.get("LAT_P95_MS", "0")))
    lat_max = max(lat_max, float(values.get("LAT_MAX_MS", "0")))
if missing:
    print(f"MISSING_PILE_LOGS={missing}", file=sys.stderr)
print(sent, acked, rejected, reconnects, p95_max, lat_max)
PY
)"
busy_count=$(grep -ci "busy" "$workdir/logs/server.log" 2>/dev/null || true)  # grep -c 零匹配时输出0且退出1，不能叠加 echo。
samples=$(wc -l < "$workdir/logs/server-samples.txt" 2>/dev/null || echo 0)
max_cpu=$(awk '{if($1>max)max=$1} END{print max+0}' "$workdir/logs/server-samples.txt" 2>/dev/null || echo 0)
max_rss_kb=$(awk '{if($2>max)max=$2} END{print max+0}' "$workdir/logs/server-samples.txt" 2>/dev/null || echo 0)
integrity="$(sqlite3 "$db" 'PRAGMA integrity_check;')"
fk_errors="$(sqlite3 "$db" 'PRAGMA foreign_key_check;' | wc -l)"
echo "LOADTEST_SENT=$sent"
echo "LOADTEST_ACKNOWLEDGED=$acked"
echo "LOADTEST_REJECTED=$rejected"
echo "LOADTEST_RECONNECTS=$reconnects"
echo "LOADTEST_BUSY_ERRORS=$busy_count"
echo "LOADTEST_LAT_P95_MAX_MS=$p95_max LAT_MAX_MS=$lat_max"
echo "LOADTEST_CPU_SAMPLES=$samples MAX_CPU=$max_cpu MAX_RSS_KB=$max_rss_kb"
echo "LOADTEST_INTEGRITY=$integrity"
echo "LOADTEST_FK_ERRORS=$fk_errors"
if [ "$sent" -lt 1 ] || [ "$acked" -ne "$sent" ] || [ "$rejected" -ne 0 ] \
   || [ "$integrity" != "ok" ] || [ "$fk_errors" -ne 0 ]; then
  echo "LOADTEST=FAIL"
  exit 1
fi
echo "LOADTEST=OK"
