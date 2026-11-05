# 脚本行为与隔离测试证据汇总（T08/S2-T18~T21，成员1，2026-09-13）

> 执行环境：`bit@192.168.41.128`，工作副本 `/home/bit/projects/charge_platform_stage2`，代码状态 `1f27344`。
> 所有退出码为直接测量（`rc=$?`），非管道衍生态。

## 1. S2-T19 重复 start/stop 与端口占用

| 步骤 | 命令 | 预期 | 实测 |
|---|---|---|---|
| 首次 start | `bash scripts/start-all.sh --stage 1 --db $DB --port 48081` | START=OK | `START=OK SERVER_PID=24033` |
| 重复 start | 同上 | 拒绝且 PID 文件不被覆盖 | `已有运行中的启动器进程（server pid=...），先执行 scripts/stop-all.sh 再启动`，**EXIT=3**；`runtime/all.pids` 内容不变 |
| 端口被占 | 用 python3 占住 48083 后 start | 失败并指明占用者 | `端口 127.0.0.1:48083 已被占用: ... users:(("python3",pid=24004,fd=3))`，**EXIT=4** |
| stop | `bash scripts/stop-all.sh` | 只杀本项目进程并等待退出 | `STOPPED=server:...` → `STOP=OK` |
| 重复 stop | 再执行 | 幂等成功 | `没有统一启动器进程记录` → `STOP=OK` |
| 就绪等待 | start 后立即 health-check | SERVER=OK | `bash scripts/health-check.sh --stage 1 --db $DB --port 48081` → `SERVER=OK DATABASE=OK HEALTH=OK` |

## 2. S2-T20 init 防护

| 输入 | 实测退出码 | 消息 |
|---|---:|---|
| `--db /` | **2** | `拒绝根级路径: /` |
| `--db $PWD/scripts/x.sqlite`（源码目录） | **2** | `拒绝把数据库放进项目源码目录（仅允许 .../runtime/ 下）` |
| `--reset-demo-data` 且项目进程存活 | **3** | `存在运行中的项目进程（server pid=...），禁止重置业务库；先 stop-all` |

正常重置（无运行进程）：`reset-demo-database.sh` 先 `mv` 备份旧库为 `.bak.<时间戳>`，新库 integrity_check + foreign_key_check 通过后替换；实测输出 `backup=... integrity_check=ok foreign_key_errors=0`。业务库未启用 WAL（journal_mode=delete），`.bak` 文件替换语义安全，故未调整 C++（按"仅针对实际失败修改"原则记录在案）。

## 3. S2-T18 故障隔离（停 Hadoop / 无分析服务）

Hadoop 完全停止（`stop-bigdata.sh` → BIGDATA=STOPPED）、分析 API 未启动的条件下，通过 TCP 协议直连服务端执行完整业务闭环：

```text
LOGIN_OK= True  TELEMETRY_OK= True  RESERVE_OK= True  START_OK= True
CHARGING_ORDER= True（遥测挂接订单）  STOP_ENERGY= 0.50  PAY_OK= True
```

随后 `start-bigdata.sh` → `BIGDATA=STARTED`（分析侧恢复不污染业务数据）。

## 4. S2-T21 20桩×1Hz×600秒 压测（正式）

`bash scripts/load-test-20piles.sh --duration 600 --piles 20 --port 48099` → **EXIT=0**

```text
LOADTEST_SENT=12000        LOADTEST_ACKNOWLEDGED=12000   LOADTEST_REJECTED=0
LOADTEST_RECONNECTS=0      LOADTEST_BUSY_ERRORS=0
LOADTEST_LAT_P95_MAX_MS=61.5  LAT_MAX_MS=189.5
LOADTEST_CPU_SAMPLES=153   MAX_CPU=148%   MAX_RSS_KB=28520（≈28.5MiB）
LOADTEST_INTEGRITY=ok      LOADTEST_FK_ERRORS=0           LOADTEST=OK
```

说明：负载由 `scripts/load_pile_client.py` 产生（每桩独立长连接、1Hz、设备时间戳单调递增、逐帧 RTT 统计；服务端 CPU 采样见 `loadtest/server-samples.txt`）。20 桩=12 预置 + 8 按种子同模式插入压测库（独立库，未触碰演示库）。压测期间无任何崩溃/忙超时；停止后 `PRAGMA integrity_check=ok`、`foreign_key_check` 0 行。

## 5. 只读业务快照（T08 交付项）

`bash scripts/business-snapshot.sh --db $DB --out runtime/analytics/business_snapshot.json` → `SNAPSHOT=OK`：经 sqlite3 `.backup` 一致性快照后只读导出 3 站（含 capacity/idle/charging/fault 计数与最近充电开始时间），12 桩 30 单，原始 JSON 见 `script-tests/business-snapshot.json`；全程无业务写连接。
