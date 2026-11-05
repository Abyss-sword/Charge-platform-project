# 两阶段分析数据契约：charge_event_v2

`charge_event_v2` 是当前活动契约；`charge_event_v1` 只保留为历史参考。第一阶段使用模拟数据和 `data_mode=SIMULATED`，第二阶段只替换为合法的匿名真实数据或公开聚合数据，不修改界面、字段含义和单位，因此可以直接复用第一阶段的展示与分析接口。

## 必填字段

| 字段 | 类型/单位 | 规则 |
| --- | --- | --- |
| `source_record_id` | 字符串 | 数据源内稳定且唯一 |
| `source_station_id` | 字符串 | 原始站点编号，不用站名猜测 |
| `station_id` | 整数 | 平台内部站点编号 |
| `occurred_at` | ISO 8601 | 事件发生时间，必须带时区 |
| `ingested_at` | ISO 8601 | 导入时间，必须带时区 |
| `timezone` | 字符串 | IANA 时区，例如 `Asia/Shanghai` |
| `energy_kwh` | 两位小数字符串，kWh | 非负，格式固定为 `0.00`；不得用二进制浮点再次换算 |
| `duration_seconds` | 整数秒 | 非负 |
| `amount_yuan` | 两位小数字符串，元 | 非负，格式固定为 `0.00`；不得用二进制浮点处理金额 |
| `status` | 字符串 | 受控状态值 |
| `data_mode` | 枚举 | `SIMULATED`、`REAL_ANONYMIZED` 或 `PUBLIC_AGGREGATED` |
| `dataset_name` | 字符串 | 数据集名称 |
| `dataset_version` | 字符串 | 可复现版本 |
| `ingest_batch` | 字符串 | 一次导入批次号 |

金额、电量和单价沿用业务端统一规则：使用高精度十进制计算，只在结果处四舍五入为两位。例如 `10.2705` 为 `10.27`，`10.275` 为 `10.28`。导出脚本直接读取数据库的规范化文本，不在 Shell 中重新计算。

禁止导出手机号、姓名、支付账户等个人字段。隐私黑名单同时覆盖个人标识列：`user_id`/`userid`/`account`/`account_id` 以及 `license_plate`/`plate`、`vehicle_id`/`vin`、`trajectory_id` 等，命中来源表头或清单 `fields` 即整批阻断（T03 接入课程真实会话数据时实测补充：该数据含 84 个稳定 `userId`，属可重识别标识，须在本地受控区裁剪后才进入 raw）。原始数据进入 `raw` 后不修改；字段映射写入 `staging`，拒绝记录进入 `rejected`，通过记录按小时聚合到 `curated`。分析流程只读业务出口，不能反写订单、余额、电桩和工单状态。第二阶段切换数据源时必须保留 `source_record_id`、`source_station_id`、数据集版本和导入批次，以便追踪来源和复现实验。
