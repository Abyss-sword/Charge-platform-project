# 冒烟四项版本锁定（acc4569）

采集日期：2026-09-14。本文件汇总本轮四项真实冒烟，用于锁定大数据组件版本组合。

| # | 冒烟项 | 证据来源 | 结果 | 锁定组件 |
|---|--------|----------|------|----------|
| 1 | HDFS 上传 | `hdfs dfs -put` 上传 staging TSV；`hdfs dfs -ls -R`（见 hadoop-jobs/hdfs-tree.txt） | `hourly_fragments.tsv` 602404 字节 + `_SUCCESS` | Hadoop 3.2.1 |
| 2 | Java 作业 | `job_1789301464634_0006`（HourlyJob，见 hadoop-jobs/yarn-applications.md） | SUCCEEDED，VALID_ROWS=6700，INVALID_ROWS=0 | JDK 1.8.0_261 |
| 3 | Hive 外部表 | `/tmp/s2-hive-smoke.log`（见 hive-smoke.txt） | rows=6700 / stations=19 / total_kwh=10349.589999999990 | Hive 3.1.3 |
| 4 | Spark 读同一路径 | `/tmp/s2-spark-smoke.log`（见 spark-smoke.txt） | SPARK_ROWS=6700 / SPARK_STATIONS=19 / SPARK_TOTAL_KWH=10349.589999999990 | Spark 3.4.1 |

## 锁定组合

```text
Hadoop 3.2.1
JDK    1.8.0_261
Hive   3.1.3
Spark  3.4.1
```

结论：四项冒烟均成功，且 Hive 与 Spark 对同一 `/charge_platform/hourly/course-v1/course-001` 路径的三项指标一致，可作为 acc4569 基线的版本锁定依据。环境版本命令原文见 `../environment.md`。
