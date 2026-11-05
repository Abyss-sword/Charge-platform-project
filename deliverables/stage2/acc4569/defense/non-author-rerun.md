# 非作者复跑记录（S2-T24，成员1 第二阶段）

复跑日期：2026-09-14；复跑者：general-purpose 子代理（非实现者）；副本：`/home/bit/projects/stage2_final`（从冻结提交 tar 同步）。

## 结论

**REPRODUCED_OK** —— 所有步骤预期与实测一致，无产品级偏差。

## 逐步结果

| 步骤 | 预期 | 实际 | 结论 |
|---|---|---|---|
| 静态检查 | `bash -n` 全通过 | EXIT=0 | PASS |
| ctest | 37/37 | 100% passed, 0 failed out of 37 | PASS |
| pytest（3.12） | 全通过 | 67 passed | PASS |
| mvn test | 6/6 | Tests run: 6, Failures: 0, BUILD SUCCESS | PASS |
| 真实适配 | 3340/1812/1/1527/19 | 完全一致；`input_md5` 与源文件一致 | PASS |
| 适配器独立核验 | 主因 ANOMALY(DURATION_GT_24H)、表头无 user | 一致（HEADER_HAS_USER=False） | PASS |
| 治理链 | HDFS=UPLOADED / PUBLISHED_RUN=rerun-001 | 一致；reconciled=true | PASS |
| YARN 业务作业 | SUCCEEDED, VALID_ROWS=6700 | `job_1789391498058_0001` SUCCEEDED | PASS |
| MR 参考比对 | MR_REFERENCE=OK ROWS=6700 | 一致（双方各 6700 行） | PASS |
| init 危险路径 | exit 2 | exit 2（`/`、仓库根） | PASS |
| 重复 start | exit 3、PID 不变 | EXIT=3，pid 未变 | PASS |
| stop-all | STOP=OK | STOP=OK，进程退出 | PASS |
| 端口占用 start | exit 4 | exit 4（用空闲端口复测同样 exit 4） | PASS |
| WAL 备份回归 | exit 0 | EXIT=0，断言全部通过 | PASS |

## 环境性偏差（非产品缺陷）

1. 副本缺 `build/` 与 `charge-hourly.jar`：复跑者用 cmake/mvn 重建后复跑通过（构建产物不在 tar 中属预期）。
2. 提示词的端口 48090 被前次遗留 `charge_platform_server` 占用：换空闲端口复测仍得 exit 4，占用检测逻辑正确。
3. `hdfs_upload_run` 用 `shutil.which("hdfs")` 判定：未导出 Hadoop PATH 时返回 `SKIPPED_NO_HDFS`（本机无 hdfs 的既有语义），导出后为 `UPLOADED`。

## 复跑发现并修复的瑕疵

- 测试第 2 行畸形 `//` 注释、WAL 管道裸 `COMMIT` 噪声报错 → 提交 `25b79a4`（测试输出干净、exit 0）。
