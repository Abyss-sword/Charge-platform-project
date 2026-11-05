# 数据来源登记（成员3 · 第二阶段 D7）

第二阶段分析流水线只接受已经准入的来源。业务大屏继续只读成员1统一 SQLite；本登记管的是离线 raw/staging/curated，不把模拟业务库改名为真实数据。

| 数据集 | data_mode | 版本 | 批次 | 覆盖时间 | 许可与边界 | 状态 |
| --- | --- | --- | --- | --- | --- | --- |
| member3-simulated-charge-events | `SIMULATED` | sim-v2 | sim-20260905-001 | 2026-08-30 ～ 2026-09-02 | 教学演示；禁止宣传为真实运营 | 第一阶段流水线示例，`manifests/source.example.json` |
| Leeds City Council EVCP 2021 Q1 小时衍生表 | `PUBLIC_AGGREGATED` | public-v1 | public-leeds-2021q1-001 | 2021-01-01 07:00 UTC ～ 2021-03-31 21:00 BST | [Data Mill North 原始 CSV](https://datamillnorth.org/download/electric-vehicle-chargepoints-in-council-car-parks-/fc2f9674-6952-43c8-be63-e18b4a26047e/2021%20EVCP%20use%20Q1.csv)，Open Government Licence v3.0；禁止重新识别 | D7 获准；上游 1011 条中 999 条有效、12 条异常；D8 衍生表 6450/6450 通过 |
| member3-six-class-quality-fixtures | `PUBLIC_AGGREGATED` | fixture-v1 | fixture-six-class-001 | 2026-01-07 10:00～13:00 | 仅测试；隐私样例单独文件 | 周末质量夹具，见 `docs/test-results/data-quality.md` |

## 准入规则

- 清单必须声明提供者、地址、允许/禁止用途、取得日期、模式、版本、覆盖时间、分发许可、保留期限、隐私边界和批次号。
- 允许用途必须包含 `teaching-analysis`。
- `SIMULATED` 不得 `claims_real_data=true`。
- `PUBLIC_AGGREGATED` 不得声称真实个人脱敏数据。
- 表头或清单字段出现手机号、姓名、支付账号、车辆标识、连续轨迹标识时，整批拒绝，不生成可用 raw。
- 当前没有学校授权的匿名真源，因此 **不登记 `REAL_ANONYMIZED`**。Leeds 数据虽来源真实公开记录，但仓库保存的是二次聚合产物，仍标 `PUBLIC_AGGREGATED`，不得改名。

## Leeds 来源转换规则

- 上游文件由 data.gov.uk 编目、Leeds City Council / Data Mill North 发布；下载文件 SHA-256 为 `7046f4084e4f9383a7f575b166b28c5d87fd1e9373bc5631ed53e43dc9ae966a`。
- 原文件中的 `Unique User ID`、`Charging event`、`CP ID`、`Connector`、`Model` 不写入仓库；仓库只提交站点—小时聚合结果。
- 1011 条记录中，6 条缺少结束时间和电量、6 条为负电量，转换时记录并剔除；其余 999 条按会话开始小时汇总。
- 源文件没有区间电表曲线，因此电量归入开始小时；没有会话的站点小时补零，形成连续时间轴。源文件也没有费用字段，衍生表 `amount=0.00`，禁止用于营收分析。

## 导入命令

在 `analytics` 虚拟环境中，只复制源文件并写批次清单，不改源字节：

```bash
python -m src.ingest_raw --manifest manifests/source.public-aggregated.json --source datasets/public_aggregated/hourly_charge_events.csv
python -m src.curate_batch
```

预期：`DATA_MODE=PUBLIC_AGGREGATED`；`INPUT=6450 ACCEPTED=6450 REJECTED=0`；`RAW_UNCHANGED=yes`；源文件修改时间与哈希不变。非法清单或带隐私列表头时退出码非 0，且不留下该批次目录。

大屏不从 raw 目录读指标。实时卡片仍走 FastAPI `/dashboard` 只读 SQLite；适配器在接口不可用时保留 `SIMULATED` 预览，不把 raw 行显示成运营数字。
