# 实测版本与环境事实（T02）

> 登记时间：2026-09-13。所有数值来自 VM `bit@192.168.41.128` 实测，不是计划值。
> 复测命令：`bash scripts/bigdata-preflight.sh`；版本变化时必须更新本文件。

## 1. 硬件与系统

| 项 | 实测值 |
|---|---|
| 虚拟机 | VMware BitDev（`E:\学生资料\01.开发环境\Ubuntu2204Bit\BitDev.vmx`，NAT） |
| 系统 | Ubuntu 22.04（内核 6.8.0-138-generic），x86_64 |
| CPU / 内存 | 2 vCPU / 7.7GiB（可用约 5.1GiB），交换 2GiB |
| 磁盘 | 根分区 491G，已用 24G（约 443G 可用） |

## 2. 大数据组件（隔离安装在 /home/bit/opt）

| 组件 | 版本 | 位置 | 来源 |
|---|---|---|---|
| JDK | 1.8.0_261 | `/home/bit/opt/jdk1.8.0_261` | 课程包 `jdk-8u261-linux-x64.tar.gz` |
| Hadoop | 3.2.1（伪分布式，副本=1） | `/home/bit/opt/hadoop-3.2.1` | 课程包 `hadoop-3.2.1.tar.gz` |
| Maven | 3.9.9 | `/home/bit/opt/apache-maven-3.9.9` | 2026-09-13 自 archive.apache.org 下载（T04 构建需要，未替换系统 Java） |
| Python | **3.12.13（主）**：独立 venv `analytics/.venv312`；3.10.12（系统）保留兼容 | `/home/bit/projects/stage2_acceptance` | 3.12 由用户按 06 文档要求经 apt 安装（`/usr/bin/python3.12`）；`analytics/requirements-lock-py312.txt` 锁定 38 项依赖；3.12 下 pytest 67 通过 |
| Hive | 3.1.3 | `/home/bit/opt/apache-hive-3.1.3-bin` | 2026-09-13 安装：guava 冲突修复（换 Hadoop 27.0-jre）；Derby 元数据库初始化于 `/home/bit/data/hive`；`conf/hive-site.xml` 设 `hive.server2.enable.doAs=false`（单用户教学环境）；HS2 端口 10000，启动命令 `cd /home/bit/data/hive && nohup $HIVE_HOME/bin/hiveserver2 > /home/bit/data/hive/hiveserver2.log 2>&1 &` |
| Spark | 3.4.1（bin-hadoop3） | `/home/bit/opt/spark-3.4.1-bin-hadoop3` | 2026-09-13 安装：`conf/spark-env.sh` 设 JAVA_HOME 与 HADOOP_CONF_DIR；冒烟 `spark-submit --master yarn --deploy-mode client --executor-memory 512m --num-executors 1` |
| Hive/Spark 冒烟 | ✅ 通过（四项齐备，已锁版本） | — | **锁定结论：Hadoop 3.2.1 / JDK 1.8.0_261 / Hive 3.1.3 / Spark 3.4.1**。四项冒烟：①HDFS 上传（`/charge_platform/staging/course-v1/course-001/hourly_fragments.tsv`，602404 字节）②Java 作业（`job_1789301464634_0006` SUCCEEDED，VALID_ROWS=6700）③Hive 外部表（beeline 连 HS2:10000，`smoke_db.hourly_course_smoke` 指向 `/charge_platform/hourly/course-v1/course-001`，COUNT=6700、站点=19、SUM=10349.589999999990）④Spark 读同一路径（`application_1789301464634_0010` SUCCEEDED，SPARK_ROWS=6700、SPARK_TOTAL_KWH=10349.589999999990，与 Hive/Decimal 口径一致）。早期 fixture 冒烟另见 `application_1789301464634_0004/_0005` |
| 安装包来源 | archive.apache.org | `/home/bit/packages/` | 两次下载：首批因代理截断损坏（两侧 sha 一致但 gzip EOF），换 `curl -fL` 重下并 `gzip -t` 双侧校验通过——教训：大文件下载必须以 `gzip -t`/官方校验为准，不能用 `tar | head` 截断式验证 |

## 3. 端口与目录

| 项 | 值 |
|---|---|
| HDFS RPC / NN UI / DN HTTP | 9000 / 9870 / 9864 |
| YARN RM / RM UI / NM local | 8032 / 8088 / 8042 |
| HDFS 项目目录 | `/charge_platform/{raw,staging,rejected,hourly,features,artifacts,smoke}` |
| 数据目录 | `/home/bit/data/hadoop/{tmp,namenode,datanode,logs}`（与程序目录分离） |
| 配置备份 | `/home/bit/opt/hadoop-3.2.1/etc/hadoop.before-charge-platform-20260913` |

## 4. 冒烟与验收证据指针

- WordCount YARN 冒烟（仅证环境，非业务作业）：job `job_1789289961838_0002` SUCCEEDED，见 05 记录。
- 项目 Java 业务作业（HourlyJob）YARN 证据：`deliverables/stage2/<commit>/hadoop-jobs/`（T04 完成后登记）。
- 环境启停与持久化复核：`deliverables/stage2/<commit>/environment.md`（T02 验收后登记）。
