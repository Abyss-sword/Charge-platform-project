# 脚本实测证据（acc4569）

采集日期：2026-09-14。本轮已实测结果（原始输出由执行方获得，本文件按记录整理，**未重跑**）。脚本实现已在本轮 acc4569 工作副本中核对（`scripts/start-all.sh`、`scripts/stop-all.sh`、`scripts/init-demo.sh`、`scripts/reset-demo-database.sh`）。

## S2-T19 启动器与端口防护

### 重复 start

```text
$ bash scripts/start-all.sh ...（已有运行中启动器）
已有运行中的启动器进程（server pid=111098），先执行 scripts/stop-all.sh 再启动
EXIT=3
```

结论：exit=3；PID 未被覆盖（沿用既有 `server pid=111098`）。

### 端口被占

```text
$ bash scripts/start-all.sh ...（目标端口已被非本项目进程占用）
端口 <host>:<port> 已被占用: <listener 进程>
EXIT=4
```

结论：exit=4；输出列出占用进程。

### stop-all 幂等

```text
$ bash scripts/stop-all.sh
STOPPED=server:111098
STOP=OK

$ bash scripts/stop-all.sh   # 再停（无 pid 记录）
没有统一启动器进程记录
STOP=OK
```

结论：按进程身份停止（`STOPPED=server:111098`），幂等再停返回 `STOP=OK`。

## S2-T20 init 路径防护

| 场景 | 结果 |
|------|------|
| `--db /` 或 `--db /usr` 等根级路径 | `拒绝根级路径: <path>`，exit=2 |
| `--db <源码目录>`（目录） | `目标是目录而非数据库文件: <path>`，exit=2 |
| 空路径 | exit=2 |
| 运行中重置 | exit=3，且主库 md5 不变 |
| 正常重置 | 生成 `.bak` 备份（`backup=<path>`） |

## WAL 缺陷修复（S2-T20 扩展 / T08 关联）

**缺陷**：`scripts/reset-demo-database.sh` 原用 `mv` 备份主库，会丢失仍在 `-wal` 中已提交的数据，并残留孤儿 `-wal`/`-shm` 边车文件污染新库。

**修复**：改用 `sqlite3 "$database_path" ".backup '<bak_path>'"`（含 WAL 中已提交事务的快照）；失败回退 `PRAGMA wal_checkpoint(TRUNCATE);` 后再整文件移动；并在备份后清理边车文件。

**测试**：`tests/integration/reset_demo_database_test.sh` 新增 WAL 断言（保持打开连接把事务留在 WAL → 重置 → 校验备份内含 `committed-in-wal`、新库不残留旧 `-wal`/`-shm`）。
结果：**修复前失败、修复后通过（exit 0）**。

## 语法校验（bash -n）

以下脚本 `bash -n` 全部通过（本轮 acc4569 工作副本实测）：

```text
OK scripts/bigdata-preflight.sh
OK scripts/business-snapshot.sh
OK scripts/export-analysis-sample.sh
OK scripts/health-check.sh
OK scripts/init-demo.sh
OK scripts/load-test-20piles.sh
OK scripts/mr-reference-check.sh
OK scripts/prepare-analytics-data.sh
OK scripts/reset-demo-database.sh
OK scripts/run-member3-stack.sh
OK scripts/start-all.sh
OK scripts/start-bigdata.sh
OK scripts/stop-all.sh
OK scripts/stop-bigdata.sh
OK scripts/switch-simulator-scenarios.sh
```

共 15 个脚本，0 失败。
