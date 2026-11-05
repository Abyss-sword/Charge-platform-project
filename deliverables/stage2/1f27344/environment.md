# 大数据环境启停与持久化核验（T02 证据）

> 执行日期：2026-09-13。环境：`bit@192.168.41.128`（BitDev VM，2 vCPU / 7.7GiB / Ubuntu 22.04）。
> 所有输出为实际命令回显，命令见各节；用例映射：S2-T23（环境部分：冷重启无需 format）。

## 1. 只读预检（scripts/bigdata-preflight.sh）

```text
USER=bit ARCH=x86_64 CPU=2 MEM_TOTAL=7.7Gi DISK_AVAIL_KB=463356060 DISK=OK
JAVA=java version "1.8.0_261"  JAVA_VERSION=OK
HADOOP=Hadoop 3.2.1            HADOOP_VERSION=OK
PORT_9000/9870/9864/8088/8042=LISTENING  PORTS=OK
HDFS=OK HDFS_DIRS=OK YARN=OK   PREFLIGHT=OK
```

初版预检曾因版本比对模式 `3.2.1` 不匹配实际输出 `Hadoop 3.2.1` 报 FAIL，已修复为子串匹配并复测通过（有效失败测试→最小修复→复测）。

## 2. 幂等启动 → 停止 → 再启动

命令：`bash scripts/start-bigdata.sh`（Hadoop 已在运行时）→ `bash scripts/stop-bigdata.sh` → `bash scripts/start-bigdata.sh`。

```text
=== 幂等启动（已在运行）===
HDFS=ALREADY_RUNNING  HDFS=STARTED  YARN=ALREADY_RUNNING  YARN=STARTED  HDFS_DIRS=OK  BIGDATA=STARTED
=== 停止 ===
Stopping nodemanagers / resourcemanager / namenodes / datanodes / secondary namenodes
BIGDATA=STOPPED     （jps 无任何 Hadoop 进程）
=== 再启动 ===
Starting namenodes on [localhost] ... HDFS=STARTED  YARN=STARTED  HDFS_DIRS=OK  BIGDATA=STARTED
jps: NameNode DataNode SecondaryNameNode ResourceManager NodeManager（五进程齐全）
```

## 3. 持久化复核（S2-T23 环境部分）

重启后读取 9/13 安装冒烟时的 WordCount 输出（未经任何 format）：

```text
hdfs dfs -cat /charge_platform/smoke/output/part-r-00000
10.27 1 / 20.54 1 / 5.13 1 / station-1 2 / station-2 1
hdfs dfsadmin -report: Live datanodes (1) / Missing blocks: 0
bash scripts/bigdata-preflight.sh  → PREFLIGHT=OK
```

结论：停止/重启循环未触发任何格式化，NameNode 元数据与数据块持久有效。

## 4. 版本与组件事实

见 `infra/bigdata/versions.md`：JDK 1.8.0_261、Hadoop 3.2.1、Maven 3.9.9（/home/bit/opt，未动系统 Java）、Python 3.10.12（独立 venv）、Hive/Spark 未安装。

## 5. 已知限制

- Windows 宿主机浏览器访问 9870/8088 未在本次验证（命令行沙箱套接字受限，见 05 记录）；VM 内 curl 均 200/302。
- Hive/Spark 未部署：S2-05/S2-06 保持 BLOCKED，属成员3 T05 范围。
