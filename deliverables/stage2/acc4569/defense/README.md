# defense/ 交付材料索引（acc4569，成员1 第二阶段）

| 文件 | 内容 |
|---|---|
| `stage2-member1-report.md` | **最终交付报告**：四类结论（本地单元／真实 YARN／真实数据／三端集成）、逐任务结论、S2 需求矩阵、4 项缺陷修复、遗留与阻塞、运行与恢复指引、交付限制声明 |
| `non-author-rerun.md` | S2-T24 非作者复跑记录（REPRODUCED_OK 逐步对照与环境性偏差） |

可引用的证据根内容：

- `../environment.md`：主机、JDK/Hadoop/Hive/Spark/Python 3.12 版本事实与 HDFS/YARN 状态。
- `../hive-spark-smoke/`：四项冒烟（HDFS 上传、Java 作业、Hive 外部表、Spark 同路径）与版本锁定结论。
- `../hadoop-jobs/`：真实 YARN applicationId、MR 参考比对、HDFS 目录树、Driver 错误退出码。
- `../data-quality/`：课程真实批次质量报告、拒绝记账、金额/容量/占用边界与三项推定声明。
- `../script-tests/`：S2-T18 故障隔离、T19 启停安全、T20 init 安全与两处缺陷修复。
- `../loadtest/`：S2-T21 20 桩 × 1Hz × 600s 压测结果。
- `../test-results/`：ctest 37/37、pytest 67、vitest 13/13、mvn 6/6 摘要与原始日志。
- `../requirements.csv`：S2-01~S2-12 需求追踪（状态/证据/命令/实际结果/阻塞原因）。
