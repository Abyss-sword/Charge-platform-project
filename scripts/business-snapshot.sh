#!/usr/bin/env bash
# 只读业务快照（T08/T09，成员1）：sqlite3 backup 一致性快照 → 只读导出站点/桩/状态时间。
# 无业务写权限：所有读取都在备份副本上执行，绝不触碰正在写入的主库。
set -euo pipefail
project_root="$(cd "$(dirname "$0")/.." && pwd -P)"
db="$project_root/runtime/charge_platform.sqlite"
out="$project_root/runtime/analytics/business_snapshot.json"
while [ "$#" -gt 0 ]; do
 case "$1" in
  --db) db="$2"; shift 2 ;;
  --out) out="$2"; shift 2 ;;
  *) echo "用法: $0 [--db PATH] [--out PATH]" >&2; exit 2 ;;
 esac
done
[ -f "$db" ] || { echo "业务库不存在: $db" >&2; exit 2; }
command -v sqlite3 >/dev/null || { echo "缺少sqlite3" >&2; exit 2; }
[ "$(sqlite3 "$db" 'PRAGMA integrity_check;')" = "ok" ] || { echo "业务库完整性检查失败" >&2; exit 1; }
mkdir -p "$(dirname "$out")"
snapshot="$(mktemp --tmpdir="$(dirname "$out")" '.business_snapshot.XXXXXX.sqlite')"
cleanup() { rm -f -- "$snapshot"; }
trap cleanup EXIT
sqlite3 "$db" ".backup '$snapshot'"  # backup API 产出一致事务快照（含未合并日志语义），不复制热文件。
sqlite3 "$snapshot" 'PRAGMA query_only=ON;' 2>/dev/null || true
sqlite3 -json "$snapshot" <<SQL > "$out"
SELECT
  s.id AS stationId,
  s.name AS name,
  s.latitude AS latitude,
  s.longitude AS longitude,
  s.price_yuan_per_kwh AS priceYuanPerKwh,
  s.status AS status,
  (SELECT COUNT(*) FROM pile_info p WHERE p.station_id = s.id) AS capacityPiles,
  (SELECT COUNT(*) FROM pile_info p WHERE p.station_id = s.id AND p.status = 'IDLE') AS idlePiles,
  (SELECT COUNT(*) FROM pile_info p WHERE p.station_id = s.id AND p.status = 'CHARGING') AS chargingPiles,
  (SELECT COUNT(*) FROM pile_info p WHERE p.station_id = s.id AND p.status = 'FAULT') AS faultPiles,
  (SELECT MAX(o.started_at) FROM charge_order o
    WHERE o.pile_id IN (SELECT p.id FROM pile_info p WHERE p.station_id = s.id)) AS lastChargingStartedAt
FROM station_info s ORDER BY s.id;
SQL
printf 'SNAPSHOT_STATIONS=%s\n' "$(sqlite3 "$snapshot" 'SELECT COUNT(*) FROM station_info;')"
printf 'SNAPSHOT_PILES=%s\n' "$(sqlite3 "$snapshot" 'SELECT COUNT(*) FROM pile_info;')"
printf 'SNAPSHOT_ORDERS=%s\n' "$(sqlite3 "$snapshot" 'SELECT COUNT(*) FROM charge_order;')"
echo "SNAPSHOT=OK"
echo "SNAPSHOT_OUT=$out"
