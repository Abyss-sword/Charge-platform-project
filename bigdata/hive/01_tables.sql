-- T05：成员3正式 Hive 表。
-- 运行示例（注意 beeline 只认双横线的 --hivevar，单横线不会注入变量）：
--   beeline -u jdbc:hive2://localhost:10000 \
--     --hivevar dataset_version=course-v1 --hivevar run_id=course-001 -f 01_tables.sql
-- hourly 原始产物来自成员1 Java MapReduce；本文件只建立外部元数据，不复制或删除 HDFS 原始结果。
SET hive.exec.dynamic.partition=true;
SET hive.exec.dynamic.partition.mode=nonstrict;

-- 不指定 LOCATION：库目录落在 HiveServer2 默认 warehouse 下。
-- 成员1 冒烟建的 smoke_db 走的就是默认 warehouse，该路径已被实测验证；
-- 若改为 /charge_platform 下的自定义位置，需先确认 HiveServer2 运行用户（doAs=false）对该父目录有写权限。
CREATE DATABASE IF NOT EXISTS charge_platform_stage2;

USE charge_platform_stage2;

CREATE EXTERNAL TABLE IF NOT EXISTS station_hour (
  station_id STRING COMMENT '带命名空间的分析站点，例如 course:369001',
  hour_utc STRING COMMENT 'UTC整点，格式 yyyy-MM-ddTHH:00:00Z',
  energy_kwh DECIMAL(38,12) COMMENT '未提前舍入的小时分摊电量',
  session_count BIGINT COMMENT '在该小时开始的会话数',
  coverage_seconds INT COMMENT '该站该小时区间并集覆盖秒数，范围0到3600',
  observation_kind STRING COMMENT '当前课程数据固定为 SESSION_ALLOCATED'
)
PARTITIONED BY (
  dataset_version STRING,
  run_id STRING
)
ROW FORMAT DELIMITED
FIELDS TERMINATED BY '\t'
STORED AS TEXTFILE
LOCATION '/charge_platform/hourly'
TBLPROPERTIES (
  'skip.header.line.count'='0',
  'member3.contract'='station_hour_v1',
  'member3.timezone'='UTC'
);

ALTER TABLE station_hour ADD IF NOT EXISTS
PARTITION (dataset_version='${hivevar:dataset_version}', run_id='${hivevar:run_id}')
LOCATION '/charge_platform/hourly/${hivevar:dataset_version}/${hivevar:run_id}';

-- 表级自检：行数、站点数、最早/最晚UTC小时和Decimal总电量必须可追溯到同一版本。
-- 口径提示：本表 energy_kwh 为 DECIMAL(38,12)，源文本的长尾小数按 HALF_UP 截到 12 位后再求和；
-- 成员1 冒烟表用 DOUBLE，其总电量 10349.589999999990 带二进制浮点长尾。
-- 两个数字不会逐位相同属于预期，T09 报告需写明该口径差异，不得当成对账失败。
SELECT
  dataset_version,
  run_id,
  COUNT(*) AS row_count,
  COUNT(DISTINCT station_id) AS station_count,
  MIN(hour_utc) AS coverage_start_utc,
  MAX(hour_utc) AS coverage_end_utc,
  CAST(SUM(energy_kwh) AS DECIMAL(38,12)) AS total_energy_kwh
FROM station_hour
WHERE dataset_version='${hivevar:dataset_version}' AND run_id='${hivevar:run_id}'
GROUP BY dataset_version, run_id;
