# 2026-09-09成员3最新要求验证记录

## 结论

成员3负责的模拟器、只读分析服务和浅色Web大屏已达到`READY FOR INTEGRATION`的技术条件。唯一未由代码自动完成的门禁是成员1对累计电表契约的书面确认；全组最终验收还需要成员1、成员2实际客户端参与两遍跨端业务流程。

## Windows验证

| 范围 | 结果 |
| --- | --- |
| 根工程干净构建 | 205个构建目标成功 |
| CTest独立回归 | 29/29通过 |
| 数据库迁移/约束精选用例 | 7/7通过，覆盖迁移007 |
| 模拟器场景单测 | 1/1通过，覆盖9个稳定场景名 |
| Analytics | 23/23通过 |
| Dashboard | 10/10通过 |
| Dashboard生产构建 | 通过 |
| PowerShell启动/停止脚本 | 真实一键链路通过：服务端可达检查、FastAPI、Web、模拟器2/2响应、停止清理 |
| Ubuntu Bash启动脚本 | `bash -n`通过；当前Windows会话未发现运行中的虚拟机，需在Ubuntu补跑根工程 |

Windows Qt运行时在同一`ctest`进程连续启动多个Qt测试时可能出现后续进程`0xc0000135`，因此本记录按测试名分别启动`ctest`；29项全部通过。`reset_demo_database_test`依赖真正的Ubuntu/GNU Bash，未用Windows BusyBox结果冒充Ubuntu通过。

## 真实TCP与SQLite验证

在独立复制的业务库上启动真实C++服务端和模拟器，不使用本地假响应，结果如下：

| 场景 | 结果 |
| --- | --- |
| `normal` | 2/2帧收到成功响应 |
| `charging` | 4/4帧收到成功响应，累计电量增加 |
| `overheat` | 3/3帧收到成功响应，仅形成1个活动过热告警/工单 |
| `offline` | 最后一帧成功，超过15秒后电桩变为`OFFLINE` |
| `recover` | 2/2帧成功，电桩恢复为`IDLE` |
| `duplicate` | 2/2请求成功，数据库仅保留1条精确记录 |
| `time-backward` | 首帧成功，倒退帧被`TELEMETRY_OUT_OF_ORDER`拒绝 |
| `energy-backward` | 首帧成功，倒退帧被`TELEMETRY_OUT_OF_ORDER`拒绝 |
| `unknown-pile` | 被`PARAM_INVALID`拒绝，未写脏数据 |

测试结束后数据库`PRAGMA integrity_check=ok`，外键错误数为0。

## API和Web验证

- FastAPI使用SQLite URI只读模式、`PRAGMA query_only=ON`和短忙等待；缺库、字段不兼容、数据库忙和空数据均有测试。
- 营收只累计`FINISHED`订单并使用Python `Decimal`，按Asia/Shanghai确定“今日”。
- 用电趋势按每桩生命周期累计电表的有效相邻差值统计，不求和累计读数。
- 在线率和即时使用率分别展示并明确分母。
- 预测使用三点移动平均；样本不足时返回`insufficient_data`，不生成伪造准确率。
- Web正常链路只请求`/api/dashboard`，3秒增量刷新；失败保留最后成功数据并标记过期，恢复后自动更新。
- 实际页面已在本地浏览器加载；窄窗口自动单列且无横向溢出，浅色文字和状态色清晰。
- Windows统一脚本会自动查找Qt/MinGW运行库目录，已解决`libstdc++-6.dll`、`libgcc_s_seh-1.dll`缺失；模拟器状态文件改放纯英文`LocalAppData`路径，已解决中文项目路径下`std::filesystem`转换失败。

## 未替团队完成的事项

1. 成员1在团队记录中确认[`member3-metering-contract.md`](../stage1-integration/member3-metering-contract.md)。
2. Ubuntu虚拟机启动后执行根工程构建、31项CTest和`scripts/run-member3-stack.sh`。
3. 成员1、2、3共同连续执行两遍正常充电支付与故障运维流程并保存截图/录屏。
