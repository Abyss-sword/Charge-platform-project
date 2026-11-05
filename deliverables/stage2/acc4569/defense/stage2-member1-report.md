# 成员1 第二阶段最终交付报告（T01/T02/T03/T04/T08/T09）

> 交付日期：2026-09-14；分支 `integration/stage2-acceptance`；统一基线提交 `acc4569`（证据根同名）；实现提交 `652cff3`→`8551802`→`877b501`→`2a98231`→`d4c6ae8`→`25b79a4`。
> 依据：`docs/superpowers/plans/stage2-complete-20260912/` 的 00/01/02/03/06/07；成员1 提示词见 `03:84-91`。
> 状态口径：只用 NOT_RUN/PASS/FAIL/BLOCKED；降级另记 degraded。

## 1. 结论：严格区分四类

| 类别 | 结论 | 依据 |
|---|---|---|
| **本地单元/集成** | **通过** | ctest 37/37、pytest 67、vitest 13/13、mvn 6/6、e2e 全过（最终副本 `stage2_verify` 复跑） |
| **真实 YARN 业务作业** | **通过** | `job_1789301464634_0006` SUCCEEDED，VALID_ROWS=6700/INVALID_ROWS=0；`MR_REFERENCE=OK ROWS=6700`（与 Python Decimal 完全一致）；Driver 错误路径 缺参=2/输入不存在=3/输出已存在=4 |
| **真实数据** | **通过（含一项保留）** | 课程真实会话 `course-v1`：适配输入 3340 → 通过 1812 → 拒绝 1（ANOMALY）→ 范围外 1527，19 站，小时片段 6700，质量门 100% 发布 `course-001`；**保留项：许可原文链接待提供方书面说明** |
| **三端集成** | 通过（第一阶段） | 第一阶段协议驱动验收 ACCEPTANCE=OK（S1-A~F）；二阶段成员2/3 端未完成，见 §5 |

**总体：成员1 侧通过；不宣称第二阶段总体完成**（真实数据许可链接待补、T05–T11 归成员3/2、S2-08 BLOCKED）。

## 2. 逐任务结论

| 任务 | 内容 | 状态 | 关键证据 |
|---|---|---|---|
| T01 | 契约/夹具/基线/需求追踪 | PASS | `docs/stage2/baseline.md`（基线统一 acc4569）、`requirements.csv`、契约与夹具 pytest 通过；隐私黑名单修复 `652cff3` |
| T02 | 单节点大数据环境 | PASS | Hadoop 3.2.1 / JDK 1.8.0_261 / Hive 3.1.3 / Spark 3.4.1 **四项冒烟后锁定版本**；Python 3.12.13 主环境（`requirements-lock-py312.txt`）；`environment.md`、`hive-spark-smoke/` |
| T03 | 数据治理与发布 | PASS | 适配器 `course_sessions.py` + 预留 `acn.py`；夹具四用例 + 真实批次；`data-quality/` |
| T04 | Java MapReduce 小时聚合 | PASS | `hadoop-jobs/`：applicationId、MR 参考比对、HDFS 树、Driver 退出码 |
| T08 | 脚本/隔离/压测 | PASS | S2-T18 故障隔离、T19 启停安全、T20 init 安全、T21 20桩压测；`script-tests/`、`loadtest/` |
| T09 | 证据/缺陷/复跑/矩阵 | PASS | 证据根齐全；缺陷 F1–F4 收口；非作者复跑 REPRODUCED_OK；`defense/` 本报告 |

## 3. S2 需求矩阵（详细见 `requirements.csv`）

| 需求 | 状态 | 说明 |
|---|---|---|
| S2-01 保留第一阶段全业务 | PASS | ctest+e2e+协议驱动 |
| S2-02 保留运营闭环 | PASS | 同上 |
| S2-03 真实数据治理 | PASS（夹具链路+真实来源） | 真实 `course-v1` 批次；许可链接待补 |
| S2-04 Hadoop 真实参与 | PASS | 真实数据 + 真实 YARN + 冷启动持久化 |
| S2-05 Hive 真实查询 | NOT_RUN（环境已就绪） | 成员3 T05；成员1 只读冒烟 6700/19/10349.59 |
| S2-06 Spark 真实处理 | NOT_RUN（环境已就绪） | 成员3 T05；成员1 只读冒烟 application_..._0010 |
| S2-07 负荷预测 | NOT_RUN | 成员3 T06 |
| S2-08 空闲桩预测 | **BLOCKED** | 数据无容量/桩数/停用列，`NO_CAPACITY_DATA` |
| S2-09 高峰预警 | NOT_RUN | 成员3 T06 |
| S2-10 车主推荐 | NOT_RUN | 成员3 T06 + 成员2 T07 |
| S2-11 Qt/Web 联动 | NOT_RUN | 成员2 T07 |
| S2-12 运维与交付 | PASS（成员1侧） | T18/T19/T20/T21/T23 实测 |

## 4. 本轮实测发现并修复的缺陷（4 项，均 TDD）

| # | 缺陷 | 影响 | 修复提交 | 测试 |
|---|---|---|---|---|
| F1 | `run_governance` 在 HDFS 上传 FAILED 时仍激活新版本并返回 0 | 失败批次被标成成功（违反 03 S2-T18） | `2a98231` | `analytics/tests/test_run_governance.py` |
| F2 | `reset-demo-database.sh` 用 `mv` 移主库，丢失 WAL 已提交数据并残留孤儿 `-wal`/`-shm` | 备份不完整、新库可能被污染（违反 01 T08） | `877b501` | `tests/integration/reset_demo_database_test.sh` |
| F3 | 隐私黑名单缺 `user_id/userid/account/plate` | 含 84 个稳定 `userId` 的课程数据会被放行 | `652cff3` | `test_source_registry.py`、`test_history_adapter.py` |
| F4 | 测试第 2 行畸形 `//` 注释 + WAL 管道噪声报错 | 输出不干净（非致命） | `25b79a4` | 同 F2 测试 |

## 5. 遗留与阻塞（如实）

1. **许可原文链接待补**：课程数据按"教授提供的教学数据集"登记（`provider`、`allowed_uses=[teaching-analysis]`、`redistribution_allowed=false`），但缺提供方书面说明；补齐前原始 raw 不入共享区、不入仓库。补齐后 HDFS 共享 raw 腿即可闭环。
2. **S2-08 空闲桩预测 BLOCKED**：来源无容量/桩数/停用列；解阻塞需含站级 EVSE 数的来源（ACN 适配器已预留）。
3. **T05/T06（Hive 业务 SQL、PySpark 特征、模型、Flask）归成员3**：成员1 已交付版本化 hourly + 质量报告 + 环境冒烟（见 `docs/stage2/handoff-member3.md`）。
4. **T07/T09 成员2 部分** 与 T09 成员3 部分未完成。
5. **20 桩并行轮次 NOT_RUN**：仅完成独立业务负载轮次；"与受限批处理并行"一轮因 2 vCPU/7.7GiB 资源预算未执行（`03:52`）。
6. **数据覆盖不连续**：来源 105 站中仅 19 站满足 `distinct_days>=28 且 records>=50`；不得整批声明"≥28 天连续覆盖"。
7. **观测类型为分摊估计**：`SESSION_ALLOCATED`，图表与评估必须注明，不得称逐小时实测（`00:103`）。

## 6. 非作者复跑（S2-T24）

- 复跑者：general-purpose 子代理，在干净副本 `/home/bit/projects/stage2_final` 按书面步骤独立执行。
- 结果：**REPRODUCED_OK**——ctest 37/37、pytest 67、mvn 6/6、适配器 3340/1812/1/1527/19（含 `input_md5` 一致）、治理链 HDFS=UPLOADED、YARN `job_1789391498058_0001` SUCCEEDED + `MR_REFERENCE=OK ROWS=6700`、脚本安全用例（exit 2/3/4）、WAL 回归 exit 0。
- 环境性偏差（非产品缺陷，已如实记录）：副本缺 build/ 与 jar（复跑者重建后通过）；提示词的端口被前次遗留进程占用（换空闲端口复测仍 exit 4）；`hdfs_upload_run` 依赖 PATH 中的 hdfs（未导出 Hadoop 环境时会 SKIPPED_NO_HDFS）。

## 7. 运行与恢复指引

```bash
# 环境
export JAVA_HOME=/home/bit/opt/jdk1.8.0_261 HADOOP_HOME=/home/bit/opt/hadoop-3.2.1 \
       HIVE_HOME=/home/bit/opt/apache-hive-3.1.3-bin SPARK_HOME=/home/bit/opt/spark-3.4.1-bin-hadoop3 \
       HADOOP_CONF_DIR=$HADOOP_HOME/etc/hadoop
export PATH=$JAVA_HOME/bin:$HADOOP_HOME/bin:$HIVE_HOME/bin:$SPARK_HOME/bin:$PATH
# 全量回归
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc) && ctest --test-dir build -j1 --output-on-failure
PYTHONPATH=analytics analytics/.venv312/bin/python -m pytest analytics/tests -q
cd dashboard && npm run test:run; cd .. && mvn -f bigdata/mapreduce/pom.xml test
BUILD_DIR=build bash tests/e2e/basic_charge_flow.sh
# 真实数据批次
PYTHONPATH=analytics analytics/.venv312/bin/python -m src.adapters.course_sessions --input /home/bit/data/controlled/course/source-raw.csv --output runtime/controlled/course/run-001.csv --stats runtime/controlled/course/run-001-stats.json --rejected runtime/controlled/course/run-001-rejected.jsonl
PYTHONPATH=analytics analytics/.venv312/bin/python -m src.run_governance --manifest analytics/sources/course/manifest.json --source-csv runtime/controlled/course/run-001.csv --run-id course-001 --runtime-root runtime
hadoop jar bigdata/mapreduce/target/charge-hourly.jar org.group29.charge.HourlyJob --input /charge_platform/staging/course-v1/course-001 --output /charge_platform/hourly/course-v1/course-001
```

回退：全部改动在 `integration/stage2-acceptance` 分支，按提交粒度 `git revert`（`2a98231`/`877b501`/`652cff3`/`8551802` 等）；迁移与契约向后兼容；原始数据与运行库备份不入仓。

## 8. 交付限制遵守声明

- 未 push、未 force push、未合并 main、未 reset --hard；保留他人未提交改动。
- 未改 Decimal 精度、空值未填 0；未用夹具/静态图冒充真实 HDFS/PySpark/Flask 运行。
- 未替代成员3 实现 T05/T06；未把 FastAPI 记录称为 Flask 通过。
- 含 `userId` 的原始文件只存于本地受控区 `/home/bit/data/controlled/course/`；仓库与证据目录只放聚合与统计，无隐私列、无令牌、无运行库备份。
- 04/05 安装文档未入库；`model-evaluation/`、`screenshots/` 归属成员3/2，未建空目录冒充。
