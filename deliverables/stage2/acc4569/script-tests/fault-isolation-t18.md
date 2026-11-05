# S2-T18 故障隔离实测证据（T08 复跑，成员1）

采集日期：2026-09-14（VM `bit@192.168.41.128`，工作副本 `/home/bit/projects/stage2_acceptance`）

## 用例 1：停分析服务 → 业务闭环仍可用

| 步骤 | 命令 | 结果 |
|---|---|---|
| 记录停机前 | `ss -tln \| grep -E ':8000\|:8010'` | 8000 与 8010 均在 LISTEN |
| 停分析服务 | `pkill -f uvicorn` / `pkill -f src.api:app` | 端口无监听（`NO_ANALYSIS_PORT`） |
| 分析不可达 | `curl -sS -m 5 http://127.0.0.1:8010/dashboard` | `curl: (7) Failed to connect ... 连接被拒绝`（rc=7） |
| 业务仍可用 | 向业务服务 48099 发协议 `PING` | `PING_OK=True code=OK` |

结论：分析服务不可用时，业务 TCP 服务照常响应（业务闭环不受分析依赖影响）。

## 用例 2：停 Hadoop → 新批次如实失败、旧产物保持可读

| 步骤 | 命令 | 结果 |
|---|---|---|
| 停 Hadoop | `bash scripts/stop-bigdata.sh` | `BIGDATA=STOPPED`；`jps` 仅剩 JobHistoryServer/RunJar |
| 新批次（Hadoop 停机） | `python -m src.run_governance ... --run-id t18-hadoop-down2` | `HDFS=FAILED`、`HDFS_REASON=/charge_platform 不可达`、**GOV_EXIT=1** |
| active 未被失败批次替换 | 读 `active_manifest.json` | 失败批次未激活为新版本（配合修复后的中止语义） |
| 旧产物可读 | `cat runtime/analytics/runs/course-v1/course-001/quality.json` | 正常读出（accepted=1812，reconciled=true） |

## 缺陷与修复（本轮实测发现，均已提交）

| # | 缺陷 | 现象 | 修复 | 证据 |
|---|---|---|---|---|
| F1 | `run_governance` 在 HDFS 上传 `FAILED` 时仍激活新版本并返回 0 | Hadoop 停机时输出 `HDFS=FAILED` 却同时输出 `PUBLISHED_RUN=t18-hadoop-down`，退出码 0 | HDFS=FAILED 即打印原因、保留候选与质量账、返回非 0、不替换 active；`SKIPPED_NO_HDFS` 语义不变 | 提交 `2a98231`；测试 `analytics/tests/test_run_governance.py` 两条用例；修复后复验 `GOV_EXIT=1` |
| F2 | `reset-demo-database.sh` 用 `mv` 移走主库，丢失 WAL 中已提交数据并残留孤儿 `-wal`/`-shm` | 备份库缺少 WAL 内事务；孤儿边车文件可能污染新库 | 改用 `sqlite3 .backup`（不可用时先 `wal_checkpoint(TRUNCATE)` 再移动），并清理边车 | 提交 `877b501`；测试 `tests/integration/reset_demo_database_test.sh` 新增 WAL 断言 |

## Hadoop 恢复确认（用例结束后必须恢复）

```
bash scripts/start-bigdata.sh → HDFS=STARTED / YARN=STARTED / BIGDATA=STARTED
jps: DataNode JobHistoryServer NameNode NodeManager ResourceManager SecondaryNameNode
hdfs dfsadmin -report → Live datanodes (1)
```

恢复后重跑 `course-001` 治理批次：`HDFS=UPLOADED`、`PUBLISHED_RUN=course-001`，`active_manifest` 已回到 `course-001`（纠正修复前那次错误激活留下的指针）。

## 状态

| 用例 | 状态 |
|---|---|
| S2-T18 停分析服务、业务可用 | PASS |
| S2-T18 停 Hadoop、新批次如实失败且旧产物可读 | PASS（修复 F1 后） |
| S2-T20 WAL 备份完整性 | PASS（修复 F2 后） |
