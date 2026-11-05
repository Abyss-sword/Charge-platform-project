#!/usr/bin/env bash
# 成员1 · 第一阶段
# 分析数据导出脚本（成员1 与成员3 的数据边界）：SQLite 只读导出订单 CSV，
# 保留源记录 ID、站点 ID、时间、元/kWh 单位、状态、来源与批次字段，默认标注 SIMULATED。
# 注意口径：不导出姓名手机号不等于已实现完整匿名化治理或模型训练。

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
database_path="${1:-${project_root}/runtime/charge_platform.sqlite}"
output_path="${2:-${project_root}/runtime/analytics/staging/stage1-sample.csv}"

if [[ ! -f "${database_path}" ]]; then
    echo "database not found: ${database_path}" >&2
    exit 2
fi

mkdir -p "$(dirname "${output_path}")"
temporary_path="${output_path}.tmp"
trap 'rm -f "${temporary_path}"' EXIT

sqlite3 -readonly -header -csv "${database_path}" >"${temporary_path}" <<'SQL'
SELECT
    o.id AS source_record_id,
    CAST(p.station_id AS TEXT) AS source_station_id,
    p.station_id AS station_id,
    strftime('%Y-%m-%dT%H:%M:%SZ', COALESCE(o.started_at, o.created_at)) AS occurred_at,
    strftime('%Y-%m-%dT%H:%M:%SZ', 'now') AS ingested_at,
    'UTC' AS timezone,
    o.energy_kwh AS energy_kwh,
    CASE
        WHEN o.started_at IS NULL OR o.ended_at IS NULL THEN 0
        ELSE MAX(0, CAST(ROUND((julianday(o.ended_at) - julianday(o.started_at)) * 86400) AS INTEGER))
    END AS duration_seconds,
    o.amount_yuan AS amount_yuan,
    o.status AS status,
    'SIMULATED' AS data_mode,
    'charge_platform_stage1' AS dataset_name,
    '1.0' AS dataset_version,
    'stage1-sample' AS ingest_batch
FROM charge_order AS o
JOIN pile_info AS p ON p.id = o.pile_id
ORDER BY COALESCE(o.started_at, o.created_at), o.id;
SQL

invalid_rows="$(sqlite3 -readonly "${database_path}" "
SELECT COUNT(*)
FROM charge_order AS o
JOIN pile_info AS p ON p.id=o.pile_id
WHERE o.id='' OR p.station_id<1
   OR typeof(o.energy_kwh)!='text'
   OR o.energy_kwh GLOB '*[^0-9.]*'
   OR length(o.energy_kwh)-length(replace(o.energy_kwh,'.',''))!=1
   OR instr(o.energy_kwh,'.')<=1
   OR length(o.energy_kwh)-instr(o.energy_kwh,'.')!=2
   OR CAST(o.energy_kwh AS REAL)<0
   OR typeof(o.amount_yuan)!='text'
   OR o.amount_yuan GLOB '*[^0-9.]*'
   OR length(o.amount_yuan)-length(replace(o.amount_yuan,'.',''))!=1
   OR instr(o.amount_yuan,'.')<=1
   OR length(o.amount_yuan)-instr(o.amount_yuan,'.')!=2
   OR CAST(o.amount_yuan AS REAL)<0
   OR o.status NOT IN ('RESERVED','CHARGING','PENDING_PAYMENT','FINISHED','CANCELED','EXPIRED');")"

if [[ "${invalid_rows}" != "0" ]]; then
    echo "INVALID_ROWS=${invalid_rows}" >&2
    exit 3
fi

mv "${temporary_path}" "${output_path}"
trap - EXIT

record_count="$(sqlite3 -readonly "${database_path}" 'SELECT COUNT(*) FROM charge_order;')"
coverage_start="$(sqlite3 -readonly "${database_path}" "SELECT COALESCE(MIN(COALESCE(started_at,created_at)),'') FROM charge_order;")"
coverage_end="$(sqlite3 -readonly "${database_path}" "SELECT COALESCE(MAX(COALESCE(ended_at,started_at,created_at)),'') FROM charge_order;")"

echo "CONTRACT=charge_event_v2"
echo "DATA_MODE=SIMULATED"
echo "RECORDS=${record_count}"
echo "COVERAGE_START=${coverage_start}"
echo "COVERAGE_END=${coverage_end}"
echo "INVALID_ROWS=0"
echo "OUTPUT=${output_path}"
