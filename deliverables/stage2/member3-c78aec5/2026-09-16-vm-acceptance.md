# 成员3第二阶段虚拟机验收记录（2026-09-16）

## 结论

成员3负责的 Hive 分析、PySpark 特征工程、1—24 小时负荷模型、Flask 只读分析接口和动态推荐已在 Ubuntu 虚拟机上完成真实链路验证。Hadoop、Hive、Spark 均使用要求版本，Spark 作业由 YARN 执行，不是本地模式模拟。

空闲桩预测仍按需求约束标记为 `BLOCKED / NO_CAPACITY_DATA`：课程会话数据没有容量和占用时序，接口返回 `freePilesExpected=null`，没有使用电量伪造桩占用。

## 运行环境

- Ubuntu 虚拟机：`bitdev`，地址 `192.168.192.132`
- Hadoop：3.2.1
- Hive：3.1.3，HiveServer2 监听 `127.0.0.1:10000`
- Spark：3.4.1
- 进程：NameNode、DataNode、SecondaryNameNode、ResourceManager、NodeManager 均在运行
- 项目提交基线：`714cbaf`，接口覆盖修复：`c78aec5`

## 数据与治理

虚拟机验证使用本机课程目录中的 `nvv2t.csv`，MD5 为 `2BF780A447A49FDA2AA336D7A5E753F5`。该文件与成员1证据中 MD5 为 `d0d5d4f49bbde206886c586fcb6d7c75` 的来源不是同一份，因此没有冒用 `course-v1`，而是单独发布为：

- `datasetVersion=course-local-v1`
- `runId=course-local-20260915`
- 原始输入：3395 行
- 适配通过：1828 行
- 适配拒绝：1 行
- 按既定站点范围排除：1566 行
- 入选站点：19 个
- 治理层有效：1828/1828，HDFS 上传及发布成功

原始 CSV 只存放于虚拟机受控目录，权限为当前用户私有，没有进入 Git 仓库。

## Hadoop 与 Hive

- MapReduce Job ID：`job_1789486682725_0001`
- Job 状态：`SUCCEEDED`
- 输入小时片段：6715 行
- 无效行：0
- 输出：`/charge_platform/hourly/course-local-v1/course-local-20260915`
- Hive 外部分区表：6715 行、19 个站点
- 覆盖时间：`2014-12-02T09:00:00Z` 至 `2015-10-04T07:00:00Z`
- 总电量：`10349.589999999990 kWh`
- 已执行查询：站点日电量、绝对小时负荷排名、有效/缺失小时比例、站点×小时、星期×小时
- 星期×小时二维分析返回 102 行

## Spark 特征工程

- YARN Application ID：`application_1789486682725_0013`
- Application 名称：`member3-features-course-local-v1-course-local-20260915`
- YARN 状态：`FINISHED / SUCCEEDED`
- 输入观测：6715 行
- 补齐后的站点完整 UTC 时间轴：90213 行
- 仍为真实观测的时间轴行：6715 行
- 训练样本：29898 行
- 可用特征维度：13 个
- 输出：完整时间轴、1—24 小时训练表、站点×小时与星期×小时 Parquet 和 JSON

缺失小时保持空值和缺失标记，没有把缺失电量补成 0；滚动均值只使用预测原点之前的数据。

## 模型评估

模型按时间顺序进行 70%/15%/15% 训练、验证、测试切分，选择只看验证集，测试集不参与选模。

| 时域 | 发布模式 | 测试集模型 MAE | 测试集基线 MAE |
| --- | --- | ---: | ---: |
| 1 小时 | TRAINED | 0.637805 | 0.866064 |
| 6 小时 | TRAINED | 0.854812 | 1.054351 |
| 24 小时 | TRAINED | 0.683029 | 0.750154 |

1、6、24 小时三个验收时域中，模型测试 MAE 均低于对应基线。模型文件实际生成 1—24 小时共 24 个时域版本。

## Flask 接口

- `/health`：`status=OK`，特征和模型版本一致
- `/forecast?horizon=24`：200，返回连续 24 个预测点，`modelMode=TRAINED`
- `/analysis/compare?kind=weekday_hour`：200，返回 102 行
- `/analysis/peak?horizon=24`：200，返回峰值结果
- `/recommend?limit=3`：200，从成员1只读业务快照动态返回 3 个推荐站点

真实运行时发现默认站点在第 10—14 小时时域没有模型站点编码。修复后，这些局部时域降级到同版本基线，并显式返回 `MODEL_STATION_H10_UNAVAILABLE` 等原因，不再错误返回 400。

## 测试结果

- Ubuntu 完整测试：106 passed，2 个第三方弃用警告
- Ubuntu 修复后 Flask 回归：4 passed
- Windows 修复后成员3分析测试：全部通过
- API 最终自动断言：`API_ASSERTIONS=PASS`

## 证据

完整虚拟机证据包位于 Git 仓库外：

`C:\Users\shenm\Desktop\university\课程\大三上\计算机软件与大数据开发实训\hadoop-vm-install\member3-stage2-evidence-20260916.tar.gz`

SHA-256：`DA88B99834CB8C2F3B50DD3CCBC531270F562B29CE56A1F5B792EBF9AE49E1F0`

证据包包含版本信息、治理统计、MapReduce/Hive/Spark 输出、YARN 状态、HDFS 树、测试输出、模型评估以及五个 Flask 接口的原始 JSON 响应。
