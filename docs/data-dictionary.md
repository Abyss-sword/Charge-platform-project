# 业务数据字典 v2

006迁移新增`charge_order.price_locked`（INTEGER，0或1，默认0）。新开始充电时，`unit_price_yuan_per_kwh`保存当时单价并将标记设为1，与订单开始和电桩占用在同一事务提交；停止时使用此单价。标记0用于升级前没有快照的订单，保留原有停止时取价规则。不得用单价是否为0判断是否锁价，免费充电的0.00也属于有效快照。迁移只添加标记，不删除订单或改写既有金额。

| 表 | 作用 | 关键约束 |
| --- | --- | --- |
| `admin_user` | 运营人员账户 | 用户名唯一；状态受控 |
| `user_info` | 车主账户 | 手机号唯一；`balance_yuan` 为元、固定两位文本 |
| `station_info` | 充电站 | 保存名称、地址、经纬度和状态；`price_yuan_per_kwh` 为元/kWh、固定两位文本 |
| `pile_info` | 电桩资产 | 桩编号唯一；必须属于已有站点；保存最新心跳和 `IDLE/RESERVED/CHARGING/FAULT/OFFLINE` 状态 |
| `charge_order` | 预约、充电与结算事实 | 订单号唯一；状态受控；`energy_kwh`、`amount_yuan`、`unit_price_yuan_per_kwh` 均为固定两位文本 |
| `wallet_transaction` | 充值和支付流水 | 流水号唯一；`amount_yuan` 为带正负号的两位文本；关联用户和可选订单 |
| `pile_telemetry` | 电桩遥测 | 关联电桩；保存时间、温度、电压、电流和功率；`energy_kwh` 为固定两位文本；按电桩和时间查询 |
| `alarm_event` | 设备告警 | 关联电桩和可选订单；状态按 `OPEN → ACKNOWLEDGED → CLOSED` 推进；同一桩同一类型只允许一条活动告警 |
| `maintenance_ticket` | 运维工单 | 每条告警至多一个工单；状态按 `OPEN → PROCESSING → RESOLVED → CLOSED` 推进；保存负责人、处理说明、解决和关闭时间 |
| `operation_log` | 管理操作审计 | 保存操作者、动作、对象和时间；站点修改、电桩状态修改、模拟重启必须与业务修改同事务写入 |
| `request_dedup` | 请求去重 | `request_id` 唯一；保存原处理结果 |

SQLite 每个连接必须执行 `PRAGMA foreign_keys=ON`。服务线程不得共享同一个数据库连接；需要多表更新的订单、支付、遥测告警和工单操作在短事务内完成。金额、电量和单价在服务端使用高精度十进制计算，在数据库中保存为固定两位文本；禁止用 SQLite `REAL` 或 `SUM` 直接处理金额。演示种子固定为 3 站、12 桩、20 用户、30 订单、3 告警和3张对应工单，重复执行数量不增加。005迁移会保留历史告警，为缺失工单的告警补建工单；如果旧库存在同桩同类的重复活动告警，则关闭较早重复项及其工单，只保留最新一条活动项。

第一阶段所有可写操作统一经过C++服务端；Qt端和桩模拟器通过TCP/JSON访问，Web数据服务以只读方式访问同一数据库。数据库文件路径统一由`CHARGE_PLATFORM_DB`指定；公共演示库应使用`scripts/reset-demo-database.sh <明确路径>`生成，脚本会在替换已有库前保留备份并输出完整性、外键和种子数量检查结果。
