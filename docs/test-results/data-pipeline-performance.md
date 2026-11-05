# 数据管线性能（D9/D10 · Leeds 正式链）

命令：在 `analytics` 虚拟环境执行 `python tests/benchmark_pipeline.py`。脚本在临时目录连续跑两遍 `curate_public_batch`，**不写** `analytics/models/evaluation.json`，也不会覆盖正式树模型。

本机记录（2026-09-10，Windows 11，Python 3.12.7）：

| 项 | 值 |
| --- | --- |
| data_mode | `PUBLIC_AGGREGATED` |
| dataset_version | `public-v1` |
| 输入 / 通过 / 拒绝 | 6450 / 6450 / 0 |
| curated 行 | 6450 |
| curated 目录字节 | 4 142 702 |
| 第一遍墙钟 | 1.9873 s |
| 第二遍墙钟 | 1.9397 s |
| Python 分配峰值 | 28.9922 MiB |
| raw 哈希两遍相同 | 是 |

这是离线清洗聚合成本，不是 `/dashboard` 延迟，也不是 20 桩设备压测。20 桩 600 秒仍要成员1 服务端。
