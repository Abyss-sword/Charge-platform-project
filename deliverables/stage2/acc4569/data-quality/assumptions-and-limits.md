# 课程批次数据质量：推定与边界（acc4569）

采集日期：2026-09-14。数据源：`runtime/analytics/runs/course-v1/course-001/`、`runtime/controlled/course/run-001-*.json{,l}`。
原始 CSV 及任何含 `userId` 的文件**未入仓库**；本目录仅聚合产物与不含隐私列的记录级 JSONL。

## 1. 三项推定（列名/地域/单位推断，非元数据确证）

| 推定项 | 结论 | 依据 |
|--------|------|------|
| 年份偏移 | `year_offset=2000`（源年字段需 +2000） | `weekday` 字段与真实日历一致（`0014-11-18` = Tuesday）证明年份被减 2000 |
| 源时区 | `Asia/Shanghai` | 地域推定（列名无时区信息） |
| 电量单位 | `kwh` | 列名推定 |

来源：`course-001-stats.json` 的 `assumptions` 块。三项均为推定，非权威元数据。

## 2. observation_kind = SESSION_ALLOCATED（分摊估计）

`observation_kind=SESSION_ALLOCATED`：小时电量按会话时长把整段会话能量**分摊**到各小时，属估计值而非表计直读。
**图表与任何下游展示必须注明为分摊估计**，不得呈现为直接计量值。

## 3. 一律 null + NOT_COLLECTED 的字段

金额 / 容量 / 占用三类字段一律为 `null`，采集状态 `NOT_COLLECTED`（数据源不含、本轮未采集），不得以 0 或推算值填充。

## 4. 金额交叉核对

`course-001-amount-crosscheck.json` 为本地聚合核对（`sum_half_up_2dp=20071.12`，`unit=UNVERIFIED`）。
注意：该文件原始 `note` 声明“仅供本地核对、不进入发布产物、仓库或共享区”；本轮按任务要求作为证据归档入本目录（仅聚合统计、无 `userId`）。如与上游约束冲突，以主流程裁定为准。

## 5. 许可原文链接待补

数据许可（license）原文链接 / 授权凭证在本轮采集范围内**未取得**，标记为待补（TODO）。

## 6. 站点筛选规则

`distinct_days>=28 and records>=50; keep all qualifying stations`：105 站中选出 19 站（`selected_count=19`），排除 86 站、1528 行。

## 7. 数据缺陷优先于站点范围判定

异常判定先于站点筛选：1 行会话时长 `DURATION_GT_24H`（>24h）被判 `ANOMALY` 并 reject，记入 `course-001-rejected.jsonl`（`row_number=173`, `record_id=2162299`），不因站点入选而放行。`excluded_count=1527` 为未入选站点行数（105 站总输入 3340 行中，1 行 ANOMALY 先剔除）。

## 8. 计数对账

```text
input_count=3340
accepted_count=1812
rejected_count=1（ANOMALY / DURATION_GT_24H）
excluded_count=1527
selected stations=19 / total stations=105
coverage 2014-12-02T09:24:49Z ~ 2015-10-04T04:44:59Z
```

发布侧 run manifest：accepted=1812 / hours=6700 / rejected=0，`qualityGate.ok=true`（minValidRate 0.95），status=PUBLISHED，dataMode=REAL_ANONYMIZED。
