# Stage 2 环境事实（acc4569 冻结基线）

> 采集日期：2026-09-14（UTC 2026-09-14T12:52:37Z / CST 2026-09-14 20:52:37）。
> 采集主机：`bit@192.168.41.128`（BitDev VM）。VM 工作副本：`/home/bit/projects/stage2_acceptance`。
> 以下均为命令原文输出（部分用 grep/head 精简，已标注）。本轮冻结基线提交号 `acc4569`；`877b501` 为其后的修复提交（见文末说明）。

## 1. 主机与资源

```text
$ uname -m
x86_64

$ nproc
2

$ free -h
               total        used        free      shared  buff/cache   available
内存：        7.7Gi       3.4Gi       1.6Gi        47Mi       2.7Gi       4.0Gi
交换：        2.0Gi       715Mi       1.3Gi

$ df -h /
文件系统        大小  已用  可用 已用% 挂载点
/dev/sda3       491G   29G  437G    7% /
```

## 2. JDK

JDK 路径：`/home/bit/opt/jdk1.8.0_261`

```text
$ /home/bit/opt/jdk1.8.0_261/bin/java -version
java version "1.8.0_261"
Java(TM) SE Runtime Environment (build 1.8.0_261-b12)
Java HotSpot(TM) 64-Bit Server VM (build 25.261-b12, mixed mode)
```

## 3. Hadoop

```text
$ /home/bit/opt/hadoop-3.2.1/bin/hadoop version   # head -6
Hadoop 3.2.1
Source code repository https://gitbox.apache.org/repos/asf/hadoop.git -r b3cbbb467e22ea829b3808f4b7b01d07e0bf3842
Compiled by rohithsharmaks on 2019-09-10T15:56Z
Compiled with protoc 2.5.0
From source with checksum 776eaf9eee9c0ffc370bcbc1888737
This command was run using /home/bit/opt/hadoop-3.2.1/share/hadoop/common/hadoop-common-3.2.1.jar
```

HDFS DataNode 状态（`hdfs dfsadmin -report` 中 Live datanodes 行）：

```text
Live datanodes (1):
```

YARN NodeManager 状态（`yarn node -list`；RUNNING 行数 = 1）：

```text
$ yarn node -list | tail -5
Total Nodes:1
         Node-Id	     Node-State	Node-Http-Address	Number-of-Running-Containers
    bitdev:40853	        RUNNING	      bitdev:8042	                           0
```

## 4. Hive

```text
$ HADOOP_HOME=/home/bit/opt/hadoop-3.2.1 /home/bit/opt/apache-hive-3.1.3-bin/bin/hive --version   # grep -i hive | head -3
Hive 3.1.3
Git git://MacBook-Pro.fios-router.home/Users/ngangam/commit/hive -r 4df4d75bf1e16fe0af75aad0b4179c34c07fc975
Compiled by ngangam on Sun Apr 3 16:58:16 EDT 2022
```

注：直接执行 `hive --version` 会报 `Cannot find hadoop installation: $HADOOP_HOME or $HADOOP_PREFIX must be set`；设置 `HADOOP_HOME=/home/bit/opt/hadoop-3.2.1` 后正常，为正常环境前置条件而非缺陷。

## 5. Spark

```text
$ /home/bit/opt/spark-3.4.1-bin-hadoop3/bin/spark-submit --version 2>&1 | head -3
Welcome to
      ____              __
     / __/__  ___ _____/ /__
    _\ \/ _ \/ _ `/ __/  '_/
   /___/ .__/\_,_/_/ /_/\_\   version 3.4.1
Using Scala version 2.12.17, Java HotSpot(TM) 64-Bit Server VM, 1.8.0_261
```

（原始输出首两行为 hostname loopback WARN `SPARK_LOCAL_IP`，此处已剔除。）

## 6. Python 3.12

```text
$ python3.12 --version
Python 3.12.13
```

Python 3.12 venv 事实：

```text
$ /home/bit/projects/stage2_acceptance/analytics/.venv312/bin/python --version
Python 3.12.13

$ /home/bit/projects/stage2_acceptance/analytics/.venv312/bin/pip freeze | wc -l
38

$ wc -l /home/bit/projects/stage2_acceptance/analytics/requirements-lock-py312.txt
38 /home/bit/projects/stage2_acceptance/analytics/requirements-lock-py312.txt
```

`pip freeze` 行数（38）与锁定文件 `analytics/requirements-lock-py312.txt` 行数（38）一致。

## 7. HDFS `/charge_platform`

```text
$ hdfs dfs -ls /charge_platform
Found 7 items
drwxr-xr-x   - bit supergroup          0 2026-09-13 17:00 /charge_platform/artifacts
drwxr-xr-x   - bit supergroup          0 2026-09-13 17:00 /charge_platform/features
drwxr-xr-x   - bit supergroup          0 2026-09-14 20:14 /charge_platform/hourly
drwxr-xr-x   - bit supergroup          0 2026-09-14 20:11 /charge_platform/raw
drwxr-xr-x   - bit supergroup          0 2026-09-14 20:11 /charge_platform/rejected
drwxr-xr-x   - bit supergroup          0 2026-09-13 17:01 /charge_platform/smoke
drwxr-xr-x   - bit supergroup          0 2026-09-14 20:11 /charge_platform/staging
```

## 8. 基线提交说明

本轮计划以 `acc4569` 为证据根命名（冻结基线）。任务说明中 `877b501` 为该基线之后的修复提交（工作副本当前 HEAD）。本文件所述环境事实采集时工作副本 HEAD 为 `877b501`；本证据根按计划仍命名 `acc4569`。若最终需以 `877b501` 为准，请以主流程裁定后重命名证据根或补充说明（本采集未执行任何 git 操作）。
