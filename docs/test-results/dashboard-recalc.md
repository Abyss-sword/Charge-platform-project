# 大屏回算口径（D10 · 两条链分开）

运营大屏数字只回算统一 SQLite，禁止用 Leeds 小时表去对今日营收或服务车次。Leeds 衍生表 `amount=0`，不能当营收。

## 运营链：`GET /dashboard`

口径来自 `sqlite_dashboard.metricDefinitions`，应用同一套 SQL/Decimal 才能对上。

| 大屏项 | 回算方法 | 不能用什么顶替 |
| --- | --- | --- |
| 站点总数 | `station_info` 行数 | curated 3 个 Leeds 站 |
| 电桩总数 | `pile_info` 行数 | 教学容量表 4+3+2 |
| 设备在线率 | 非 `OFFLINE` 桩数 / 电桩总数 | 公开聚合没有桩状态 |
| 充电中订单 | `charge_order.status=CHARGING` | 分析小时订单数 |
| 今日服务量 | `started_at` 落在 Asia/Shanghai 当日的订单数（含未完成） | curated `order_count` |
| 今日营收 | 当日 `ended_at` 且 `FINISHED` 的 `amount_yuan` 用 Decimal 相加 | Leeds `amount=0` |
| 即时使用率 | `CHARGING / (IDLE+RESERVED+CHARGING)` | 预测负荷比 |
| 未处理告警 | `alarm_event` 中打开/已确认 | 质量拒绝条数 |
| 24h 用电折线 | 同桩相邻累计电表非负差分，按上海整点归并 | curated `load_kwh` |
| 站点排行 | 本站 `CHARGING / 可服务桩` | `/recommend` 教学排序 |
| 折线预测 | 至少 2 个有效小时才画 3 点均值；不足则 `forecast=null` | 不得把 actual 抄成预测 |

分析接口停止后，大屏应保留最近成功帧并标接口失败；基础充电仍走成员1 服务端，不依赖 FastAPI 写库。

## 分析链：`GET /health` `/forecast` `/recommend`

| 项 | 回算 / 核对 |
| --- | --- |
| dataMode / 版本 / 覆盖 | `runtime/analytics/curated/public-v1/metadata.json` |
| 一小时电量 | `docs/test-results/data-quality.md` 中站 1、`2021-03-11T08:00:00+00:00`，`load_kwh=168.26` |
| 模型徽章 | 只读 `analytics/models/evaluation.json`；模拟管线禁止写该文件 |
| 1/6/24 时域 | 各自 `selected_mode`，不能拿一个时域顶替另一个 |
| 预计空闲桩 | `capacity.estimate_free_piles`，教学 4/3/2 桩 × 60 kW，不是第二训练目标 |

关闭动效做截图：打开 `?motion=off`，或系统“减少动效”。数据与按钮结果不得因关掉动画而改变。
