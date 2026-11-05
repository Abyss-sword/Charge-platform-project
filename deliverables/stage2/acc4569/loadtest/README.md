# S2-T21 二十桩压测证据（acc4569，成员1）

采集日期：2026-09-14；VM `bit@192.168.41.128`（2 vCPU / 7.7GiB）；工作副本 `/home/bit/projects/stage2_acceptance`。

## 命令

```bash
bash scripts/load-test-20piles.sh --piles 20 --duration 600 --rate 1 --port 48099
```

脚本自包含：独立工作目录 `runtime/load-test/`、独立库 `loadtest.sqlite`（经 `reset-demo-database.sh` 重建为种子库），不触碰演示库与生产数据。

## 结果（原始日志见 `loadtest-600.log`）

| 指标 | 值 |
|---|---|
| 桩数 / 频率 / 时长 | 20 / 1 Hz / 600 s |
| 发送帧 | 12000 |
| 确认帧 | 12000 |
| 拒绝帧 | 0 |
| 重连次数 | 0 |
| busy 错误 | 0 |
| 延迟 P95（最大采样） | 57.6 ms |
| 延迟最大值 | 236.5 ms |
| CPU 采样 | 120 次，峰值 16% |
| 服务端 RSS 峰值 | 26440 KB |
| 完整性检查 | `integrity_check=ok` |
| 外键检查 | 0 错误 |
| 结论 | `LOADTEST=OK` |

独立复核（压测结束后直接查库）：

```
sqlite3 runtime/load-test/loadtest.sqlite "PRAGMA integrity_check;"   → ok
sqlite3 runtime/load-test/loadtest.sqlite "SELECT COUNT(*) FROM pile_telemetry;" → 12000
sqlite3 runtime/load-test/loadtest.sqlite "PRAGMA foreign_key_check;" → (空)
```

## 说明与限制

- 本轮为**独立业务负载**（压测期间 Hadoop 处于停机状态之后已恢复，分析服务已停）；文档要求的"与受限批处理并行"一轮因 2 vCPU/7.7GiB 资源预算尚未执行，如实标注为 NOT_RUN，不以独立轮次代替并行轮次结论（`03:52`）。
- 脚本不产生真实支付、不操作生产数据；压测库为一次性独立库。
- 早期基线证据见 `deliverables/stage2/1f27344/loadtest/loadtest-600.log` 与 `server-samples.txt`（fixture 数据轮次）。
