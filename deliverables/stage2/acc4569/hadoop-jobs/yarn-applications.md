# YARN 应用证据（acc4569）

采集日期：2026-09-14（`yarn application -list -appStates ALL`，ResourceManager localhost:8032）。

## 本轮真实业务作业

### application_1789301464634_0006 — charge-hourly-aggregation（HourlyJob）

```text
Application-Id : application_1789301464634_0006
Application-Name : charge-hourly-aggregation
Application-Type : MAPREDUCE
State : FINISHED
Final-State : SUCCEEDED
Progress : 100%
Tracking-URL : http://bitdev:19888/jobhistory/job/job_1789301464634_0006
Start-Time : 1789388044173
Finish-Time : 1789388054558
```

- 输入：`/charge_platform/staging/course-v1/course-001`
- 输出：`/charge_platform/hourly/course-v1/course-001`
- VALID_ROWS=6700，INVALID_ROWS=0

### application_1789301464634_0010 — course-hourly-smoke（Spark 冒烟）

```text
Application-Id : application_1789301464634_0010
Application-Name : course-hourly-smoke
Application-Type : SPARK
State : FINISHED
Final-State : SUCCEEDED
Progress : 100%
Tracking-URL : N/A
Start-Time : 1789390112407
Finish-Time : 1789390124777
```

## 佐证：`yarn application -list -appStates ALL`（head -20）

```text
Total number of applications (application-types: [], states: [NEW, NEW_SAVING, SUBMITTED, ACCEPTED, RUNNING, FINISHED, FAILED, KILLED] and tags: []):10
                Application-Id	    Application-Name	    Application-Type	      User	     Queue	             State	       Final-State	       Progress	                       Tracking-URL
application_1789301464634_0003	SELECT analysis_station_id, hour_start_u...3 (Stage-1)	           MAPREDUCE	       bit	   default	          FINISHED	         SUCCEEDED	           100%	http://bitdev:19888/jobhistory/job/job_1789301464634_0003
application_1789301464634_0004	SELECT SUM(CASE WHEN ...v_smoke.hourly_smoke (Stage-1)	           MAPREDUCE	       bit	   default	          FINISHED	         SUCCEEDED	           100%	http://bitdev:19888/jobhistory/job/job_1789301464634_0004
application_1789301464634_0001	SELECT analysis_statio...analysis_station_id (Stage-1)	           MAPREDUCE	       bit	   default	          FINISHED	         SUCCEEDED	           100%	http://bitdev:19888/jobhistory/job/job_1789301464634_0001
application_1789301464634_0002	SELECT analysis_statio...analysis_station_id (Stage-2)	           MAPREDUCE	       bit	   default	          FINISHED	         SUCCEEDED	           100%	http://bitdev:19888/jobhistory/job/job_1789301464634_0002
application_1789301464634_0007	SELECT COUNT(*) AS ro....hourly_course_smoke (Stage-1)	           MAPREDUCE	       bit	   default	          FINISHED	         SUCCEEDED	           100%	http://bitdev:19888/jobhistory/job/job_1789301464634_0007
application_1789301464634_0008	SELECT COUNT(DISTINCT....hourly_course_smoke (Stage-1)	           MAPREDUCE	       bit	   default	          FINISHED	         SUCCEEDED	           100%	http://bitdev:19888/jobhistory/job/job_1789301464634_0008
application_1789301464634_0005	    charge-env-smoke	               SPARK	       bit	   default	          FINISHED	         SUCCEEDED	           100%	                                N/A
application_1789301464634_0006	charge-hourly-aggregation	           MAPREDUCE	       bit	   default	          FINISHED	         SUCCEEDED	           100%	http://bitdev:19888/jobhistory/job/job_1789301464634_0006
application_1789301464634_0009	SELECT SUM(CAST(energ....hourly_course_smoke (Stage-1)	           MAPREDUCE	       bit	   default	          FINISHED	         SUCCEEDED	           100%	http://bitdev:19888/jobhistory/job/job_1789301464634_0009
application_1789301464634_0010	 course-hourly-smoke	               SPARK	       bit	   default	          FINISHED	         SUCCEEDED	           100%	                                N/A
```

备注：`-list` 命令在未导出 `HADOOP_HOME`/`PATH` 时返回空（`yarn` 不在 PATH），设置 `PATH=/home/bit/opt/hadoop-3.2.1/bin:$PATH` 后正常返回 10 条应用。`_0001`~`_0009` 含 Hive 三条查询（_0007/_0008/_0009）与早前 WordCount/环境冒烟，非本轮业务作业；本轮业务作业为 `_0006`。
