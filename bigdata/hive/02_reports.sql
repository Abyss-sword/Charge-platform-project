-- T05：三条必做业务查询 + 两类二维分析。所有结果继续携带 dataset_version/run_id。
-- 运行示例（beeline 只认双横线的 --hivevar；写成 -hivevar 变量不会注入，
-- ${hivevar:dataset_version} 会被当字面量，三条查询将静默返回 0 行）：
--   beeline -u jdbc:hive2://localhost:10000 \
--     --hivevar dataset_version=course-v1 --hivevar run_id=course-001 -f 02_reports.sql
USE charge_platform_stage2;

DROP VIEW IF EXISTS station_daily_energy_v;
CREATE VIEW station_daily_energy_v AS
SELECT
  dataset_version,
  run_id,
  station_id,
  SUBSTR(hour_utc, 1, 10) AS day_utc,
  CAST(SUM(energy_kwh) AS DECIMAL(38,12)) AS energy_kwh,
  SUM(session_count) AS session_count,
  MAX(observation_kind) AS observation_kind
FROM station_hour
GROUP BY dataset_version, run_id, station_id, SUBSTR(hour_utc, 1, 10);

DROP VIEW IF EXISTS hourly_load_rank_v;
CREATE VIEW hourly_load_rank_v AS
SELECT
  dataset_version,
  run_id,
  hour_utc,
  station_id,
  energy_kwh,
  DENSE_RANK() OVER (
    PARTITION BY dataset_version, run_id, hour_utc
    ORDER BY energy_kwh DESC, station_id ASC
  ) AS load_rank
FROM station_hour;

DROP VIEW IF EXISTS station_coverage_v;
CREATE VIEW station_coverage_v AS
WITH station_bounds AS (
  SELECT
    dataset_version,
    run_id,
    station_id,
    MIN(hour_utc) AS min_hour_utc,
    MAX(hour_utc) AS max_hour_utc,
    COUNT(DISTINCT hour_utc) AS observed_hours,
    SUM(CASE WHEN coverage_seconds > 0 THEN 1 ELSE 0 END) AS valid_hours
  FROM station_hour
  GROUP BY dataset_version, run_id, station_id
),
station_span AS (
  SELECT
    dataset_version,
    run_id,
    station_id,
    observed_hours,
    valid_hours,
    -- 只做日期差与整点差，不经过 unix_timestamp，因此不受 HiveServer2 本地时区和夏令时影响。
    -- hour_utc 是定长 ISO 文本，字典序等于时间序，MIN/MAX 可直接取边界。
    DATEDIFF(SUBSTR(max_hour_utc, 1, 10), SUBSTR(min_hour_utc, 1, 10)) * 24
      + CAST(SUBSTR(max_hour_utc, 12, 2) AS INT)
      - CAST(SUBSTR(min_hour_utc, 12, 2) AS INT)
      + 1 AS expected_hours
  FROM station_bounds
)
SELECT
  dataset_version,
  run_id,
  station_id,
  CAST(expected_hours AS BIGINT) AS expected_hours,
  observed_hours,
  valid_hours,
  CAST(expected_hours - observed_hours AS BIGINT) AS missing_hours,
  CAST(valid_hours / expected_hours AS DECIMAL(18,6)) AS valid_hour_ratio,
  CAST((expected_hours - observed_hours) / expected_hours AS DECIMAL(18,6)) AS missing_hour_ratio
FROM station_span;

-- 二维分析1：站点 × 小时（平均电量热力）。
DROP VIEW IF EXISTS station_hour_heatmap_v;
CREATE VIEW station_hour_heatmap_v AS
SELECT
  dataset_version,
  run_id,
  station_id,
  CAST(SUBSTR(hour_utc, 12, 2) AS INT) AS hour_of_day,
  CAST(AVG(energy_kwh) AS DECIMAL(38,12)) AS avg_energy_kwh,
  COUNT(*) AS observed_rows
FROM station_hour
GROUP BY dataset_version, run_id, station_id, CAST(SUBSTR(hour_utc, 12, 2) AS INT);

-- 二维分析2：星期 × 小时。from_unixtime 按 UTC 会话配置运行。
DROP VIEW IF EXISTS weekday_hour_compare_v;
CREATE VIEW weekday_hour_compare_v AS
SELECT
  dataset_version,
  run_id,
  PMOD(DATEDIFF(SUBSTR(hour_utc, 1, 10), '1970-01-05'), 7) AS weekday_monday_0,
  CAST(SUBSTR(hour_utc, 12, 2) AS INT) AS hour_of_day,
  CAST(AVG(energy_kwh) AS DECIMAL(38,12)) AS avg_energy_kwh,
  COUNT(*) AS observed_rows
FROM station_hour
GROUP BY
  dataset_version,
  run_id,
  PMOD(DATEDIFF(SUBSTR(hour_utc, 1, 10), '1970-01-05'), 7),
  CAST(SUBSTR(hour_utc, 12, 2) AS INT);

-- 查询1：站点日电量；输出前20行供验收回算。
SELECT * FROM station_daily_energy_v
WHERE dataset_version='${hivevar:dataset_version}' AND run_id='${hivevar:run_id}'
ORDER BY day_utc, station_id
LIMIT 20;

-- 查询2：每个绝对小时的负荷排名；输出前20行。
SELECT * FROM hourly_load_rank_v
WHERE dataset_version='${hivevar:dataset_version}' AND run_id='${hivevar:run_id}'
ORDER BY hour_utc, load_rank, station_id
LIMIT 20;

-- 查询3：有效/缺失小时比例；完整输出19个站点。
SELECT * FROM station_coverage_v
WHERE dataset_version='${hivevar:dataset_version}' AND run_id='${hivevar:run_id}'
ORDER BY station_id;

-- 两类二维分析摘要，供 Flask /analysis/compare 对照。
SELECT * FROM station_hour_heatmap_v
WHERE dataset_version='${hivevar:dataset_version}' AND run_id='${hivevar:run_id}'
ORDER BY station_id, hour_of_day
LIMIT 50;

SELECT * FROM weekday_hour_compare_v
WHERE dataset_version='${hivevar:dataset_version}' AND run_id='${hivevar:run_id}'
ORDER BY weekday_monday_0, hour_of_day;
