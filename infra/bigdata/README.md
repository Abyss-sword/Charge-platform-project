# 大数据运行环境（T02，成员1）

> 目标环境：`bit@192.168.41.128`（Ubuntu 22.04，VMware BitDev 虚拟机）。
> 安装手册与核验记录为外部课程文档（`04-Hadoop单节点安装与配置指南.md`、`05-Hadoop安装核验记录-20260913.md`），按交付要求不纳入仓库；本仓库只保留配置模板与实测事实。
> 本目录只放"配置模板与环境事实"，不是安装完成的证明；实际版本以 `versions.md` 实测记录为准。

## 1. 目录规划（虚拟机内）

```text
/home/bit/packages/                 # 上传的原始压缩包（hadoop-3.2.1.tar.gz、jdk-8u261-linux-x64.tar.gz、apache-maven-3.9.9-bin.tar.gz）
/home/bit/opt/jdk1.8.0_261/         # JDK（项目专用，不改系统 java）
/home/bit/opt/hadoop-3.2.1/         # Hadoop 程序与配置
/home/bit/opt/apache-maven-3.9.9/   # Maven（T04 Java 构建用）
/home/bit/data/hadoop/{tmp,namenode,datanode,logs}  # 数据与日志，与程序目录分离
/home/bit/projects/charge_platform_stage2/          # 第二阶段工作副本（宿主仓库 tar+scp 同步）
```

## 2. 配置模板（hadoop/）

`hadoop/` 内 5 个文件与 2026-09-13 实际部署到 VM 的配置一致（部署前备份：`/home/bit/opt/hadoop-3.2.1/etc/hadoop.before-charge-platform-20260913`）：

| 文件 | 关键项 |
|---|---|
| `hadoop-env.sh` | `JAVA_HOME=/home/bit/opt/jdk1.8.0_261`、`HADOOP_HEAPSIZE_MAX=1024`、`HADOOP_LOG_DIR=/home/bit/data/hadoop/logs`；**不含** 课程三节点手册的 `HDFS_*_USER=root` |
| `core-site.xml` | `fs.defaultFS=hdfs://localhost:9000`、`hadoop.tmp.dir=/home/bit/data/hadoop/tmp` |
| `hdfs-site.xml` | `dfs.replication=1`、NN/DN 持久化目录在 `/home/bit/data/hadoop/`、UI `9870/9864` |
| `mapred-site.xml` | `mapreduce.framework.name=yarn`、map 768MB/-Xmx512m、reduce 1024MB/-Xmx768m |
| `yarn-site.xml` | RM=localhost、NM 3072MB/2 vcores、容器 512–2048MB、shuffle 辅助服务 |

改动这些模板时：先改 VM 实际配置并复测，再把实测生效的文件同步回本目录，保持"模板=事实"。

## 3. 三个环境脚本（scripts/）

| 脚本 | 作用 | 约束 |
|---|---|---|
| `bigdata-preflight.sh` | 只读检查：用户/架构/CPU/内存/磁盘/Java/Hadoop 版本/端口监听/HDFS 与 YARN 状态/项目目录 | 不修改任何状态；root 运行直接退出 2 |
| `start-bigdata.sh` | 幂等启动 HDFS→YARN：已在运行则跳过；等待 **1 个 Live DataNode** 与 **1 个 RUNNING NodeManager**；确保 `/charge_platform/*` 六目录 | **绝不包含 format**；超时返回非 0 |
| `stop-bigdata.sh` | 先 `stop-yarn.sh` 后 `stop-dfs.sh`，等待进程真正退出 | 幂等；未退出时报日志位置并返回非 0 |

用法（在 VM 中）：

```bash
bash scripts/bigdata-preflight.sh
bash scripts/start-bigdata.sh    # 预期末行 BIGDATA=STARTED
bash scripts/stop-bigdata.sh     # 预期末行 BIGDATA=STOPPED
```

## 4. 资源预算（2 vCPU / 7.7GiB）

| 预算项 | 预留 |
|---|---|
| Ubuntu + VMware 开销 + 桌面 | ~1.5GiB |
| NameNode/DataNode/SecondaryNameNode（堆上限 1024MB） | ~2GiB |
| ResourceManager/NodeManager | NodeManager 容器配额 3072MB |
| 单个 MR 作业（AM + map 768MB + reduce 1024MB，串行调度） | ≤2GiB |
| C++ 服务 / Qt / Web / 分析 API | ~1.5GiB |

规则：批作业串行执行；不并行训练与 20 桩压测；资源不足时减小时间窗而不是关闭内存检查。

## 5. 安全与禁止事项（继承 00/01/04 文档）

- 不重复 `hdfs namenode -format`（只允许在全新空目录时执行一次，安装时已做）；不用删除 data/logs 解决 ID 不一致。
- 不关闭防火墙；9000（HDFS RPC）不对教学网段外开放；9870/8088 仅受信教学网络访问。
- 不以 root 运行任何 Hadoop/项目进程；不向 `/etc/profile` 反复追加环境变量。
- Hadoop 停机不影响在线业务：SQLite 业务库与本环境完全隔离。
