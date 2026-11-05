# YARN 作业证据（charge-hourly-aggregation）

## 1. 作业提交时的实时捕获（2026-09-13 18:50-18:51，作者执行，逐行摘自命令回显）

```text
=== run-001（输入 /charge_platform/staging/fixture-v1/run-001）===
2026-09-13 18:50:10,869 INFO impl.YarnClientImpl: Submitted application application_1789294421801_0001
JOB_ID=job_1789294421801_0001
JOB_STATUS=SUCCEEDED
VALID_ROWS=4
INVALID_ROWS=0
OUTPUT_PATH=/charge_platform/hourly/fixture-v1/run-001

=== run-003（输入 /charge_platform/staging/fixture-v1/run-003，Decimal 分摊片段）===
2026-09-13 18:51:27,862 INFO impl.YarnClientImpl: Submitted application application_1789294421801_0002
JOB_ID=job_1789294421801_0002
JOB_STATUS=SUCCEEDED

=== yarn application -list -appStates ALL（作业完成当日、RM 重启前）===
Total number of applications ...:2
application_1789294421801_0001  charge-hourly-aggregation  MAPREDUCE  bit  default  FINISHED  SUCCEEDED  100%  http://bitdev:19888/jobhistory/job/job_1789294421801_0001
application_1789294421801_0002  charge-hourly-aggregation  MAPREDUCE  bit  default  FINISHED  SUCCEEDED  100%  http://bitdev:19888/jobhistory/job/job_1789294421801_0002
```

## 2. 非作者复跑时的已知环境说明（2026-09-13 晚间）

S2-T18 隔离测试曾执行 `stop-bigdata.sh`/`start-bigdata.sh`（验证业务不受 Hadoop 停机影响）。本教学环境未配置 YARN RM 状态存储（yarn.resourcemanager.store.class 未设）与日志聚合，因此 **RM 重启后 `yarn application -list` 的内存应用列表清空、JobHistory 无已完成作业记录**——这是 YARN 单机教学环境的预期行为，不代表作业未执行。

复跑核验以持久化产物为准：
- `hdfs dfs -cat /charge_platform/hourly/fixture-v1/run-001/part-r-00000` 输出与提交时完全一致（4 行 TSV）；
- `scripts/mr-reference-check.sh` 与 Python Decimal 参考实现逐行一致（MR_REFERENCE=OK ROWS=4）；
- HDFS 目录时间戳（run-001 18:50、run-003 18:51）与作业提交时间吻合。

如需在答辩演示完整作业记录，重跑一次小批次作业即可现场生成新的 applicationId（命令见 environment.md/README）。
