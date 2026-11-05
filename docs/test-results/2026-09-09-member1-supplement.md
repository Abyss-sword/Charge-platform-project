# 2026-09-09 成员1第一阶段补充复核记录（缺口清点 + 纯新增验证件）

基线：`feature/member1-stage1-integration` @ `f428fbc`。本记录不覆盖
`2026-09-09-member1-independent-fixes.md` 与 `2026-09-09-member1-reassessment.md`
已记载的修复（开始锁价、006 迁移、包装脚本断言修正等），只补充它们之后的
缺口清点与验证结果。未合并 main，未重置运行库。

## 1. 复核方法

在 20 项 CTest 与既有验收记录之外，对照 `docs/stage1-integration/acceptance-checklist.md`、
`member1-tasks.md`、`protocol-gap-matrix.md` 清点成员1**独立范围**（排除三端联调）的剩余
缺口。方法：将 `request_router.cpp` 注册的 35 个动作与全部测试源码中出现的动作字符串
求差集，并逐条核对任务卡未勾选项。

## 2. 复核发现（三条实质缺口，均属成员1独立范围）

1. **8 个管理动作缺少协议级（TCP 入口）测试**：`ADMIN_STATIONS / ADMIN_PILES /
   ADMIN_ORDERS / ADMIN_USERS / ADMIN_LOGS / ADMIN_OVERVIEW / ADMIN_REVENUE_TREND /
   ADMIN_USER_FREEZE` 此前仅被服务层测试（`admin_service_test`）覆盖，未经过
   `request_router` → 网络层 → JSON 响应的完整链路；任务卡“为每个动作补充成功、
   参数错误、未授权和业务冲突测试”对这 8 个动作未闭环。
2. **两项对外交付物缺失**：任务卡中“向成员2提供完整请求/成功/失败响应样例”与
   “向成员3提供合法 `PILE_TELEMETRY` 样例和桩号映射”未勾选，仓库中无对应文档。
3. **e2e 链路少一步**：任务卡要求的“登录→查询站点→预约→开始→遥测→停止→支付→
   历史”中，`member1_flow.cpp` 未包含 `ORDER_HISTORY` 步骤（历史查询另有
   `client_query_protocol_test` 覆盖，但端到端链路本身不完整）。

经核对**不构成缺口**的项：`USER_PROFILE / WALLET_BALANCE / STATION_DETAIL /
ORDER_ACTIVE / ORDER_DETAIL / ORDER_HISTORY / ADMIN_ALARMS / ADMIN_TICKETS /
ADMIN_STATION_UPDATE / ADMIN_PILE_SET_STATUS / ADMIN_PILE_RESTART` 均已实现且有测试
（协议矩阵“必须补齐”清单已闭环）；遥测入库可被 SQL 只读查询由
`telemetry_protocol_test` 与 `schema_test` 覆盖；旧包装脚本断言已随 `f428fbc` 修复，
不再是过时验收入口。

## 3. 补齐措施（未修改任何既有文件，全部为新增）

| 新增文件 | 作用 |
| --- | --- |
| `tests/integration/stage1_supplement_check.py` | 独立协议级验证脚本：临时库 + 独立服务进程，45 项检查 = 完整端到端链路（含 `ORDER_HISTORY` 补充步骤）+ 同 requestId 重复支付幂等 + 8 个管理动作的成功路径与 8×未授权（AUTH_INVALID）+ 冻结预约（USER_FROZEN）/ 电量倒退 / 空冻结原因 / 缺坐标建站等业务冲突与参数错误。运行前探测 48080 端口，被占用则拒绝执行，绝不影响现有服务；使用系统临时目录，不触碰 `runtime/`。未注册进 CTest（遵守“不修改既有文件”约束），注册片段见脚本 docstring。 |
| `docs/stage1-integration/delivery-samples.md` | 成员2/成员3联调交付样例：全部车主端动作的请求/成功/失败 JSON 样例与失败码清单；公共规则（requestId 防重语义、code 与 message 的区别、两位小数字符串）；演示库桩号映射表（PILE-DEMO-01~12 → pileId 101~112，按站点分组）与演示用户号段；合法 `PILE_TELEMETRY` 样例与全部约束；明确 `PILE_TELEMETRY` 不回写订单电量、需用 `ORDER_TELEMETRY` 的边界（与 reassessment 4.1 一致）。 |

另：本轮修正了首轮注释中一处已过时的描述——`tests/e2e/basic_charge_flow.sh`
的头注释曾写“断言（REVENUE_DELTA=1200）已过时”，但该断言已由 `f428fbc` 修复为
`REVENUE_BEFORE=0.00 / REVENUE_AFTER=10.27`，现注释已更正为实际行为（仅注释行，
脚本语句零改动，经注释剥离校验确认）。

## 4. 执行结果

- `python3 tests/integration/stage1_supplement_check.py`：**45/45 通过**。
  过程中修正过一处脚本自身断言：服务端把服务层 `AUTH_FAILED` 统一映射为规范码
  `AUTH_INVALID` 返回（原始码保留在 `message` 字段），属预期行为。
- 补充验证同时实证了既有机制：开始充电锁价 1.23、`1.00×1.23=1.23` 两位结算、
  同 requestId 重复支付幂等返回成功、换 requestId 重复支付返回
  `ORDER_STATE_CONFLICT`、余额恰好只扣一次（100.00→98.77）、冻结用户
  `USER_FROZEN`、历史订单可查、营收与趋势（UTC 日口径）一致。
- 既有 20 项 CTest 复跑全部通过；`git diff` 校验确认全部既有文件的改动仍仅为
  注释行（+679 / -0），本轮除修正上述头注释外零修改。

## 5. 剩余待办（属集成日，不属于成员1独立编码）

- 三端联调人工验收（acceptance-checklist 第 1~5 节）与连续两遍流程；
- 成员2/成员3按 `delivery-samples.md` 完成接入后确认样例与实际对接无偏差；
- reassessment 报告 4.1（设备计量与订单计量衔接）、4.2（用户会话与订单归属）、
  4.3（头像跨设备传输）为跨端改造项，需接口协调后实施，不属于本轮补充范围；
- 如团队同意，可由维护者将 `stage1_supplement_check.py` 注册进 CTest 或转换为
  C++/QtTest 用例，纳入每日回归（注意与其他占用 48080 的测试不要并行）。
