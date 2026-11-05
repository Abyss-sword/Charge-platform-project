# 成员1第二阶段交付与验收记录

## 范围

成员1负责第二阶段公共服务端、协议、业务库访问边界、遥测和告警基础、故障隔离、公共启动脚本、第一阶段回归和最终集成。

成员1不负责成员2 Qt页面和成员3数据分析算法的实现，但必须提供稳定接口并复核其对业务库的访问边界。

## 已具备的基础能力

- 用户会话和请求授权；
- `REQUEST_STATUS`不确定请求查询；
- `PILE_TELEMETRY`唯一计量入口；
- 累计电量倒退、非法物理量和时间异常拒绝；
- 高温告警、离线扫描和活动告警去重；
- 告警与工单事务处理；
- 管理员状态覆盖保护；
- `charge_event_v2`导出契约；
- 金额、电量和单价使用十进制字符串；
- FastAPI只读业务出口。

## 本次补齐的公共工具

| 文件 | 用途 |
|---|---|
| `scripts/init-demo.sh` | 初始化或检查指定演示数据库，第二阶段检查分析依赖 |
| `scripts/start-all.sh` | 按阶段启动C++服务端和第二阶段分析服务 |
| `scripts/stop-all.sh` | 根据受保护的进程记录停止本项目启动的后台进程 |
| `scripts/health-check.sh` | 检查数据库完整性、服务端端口和分析接口 |
| `scripts/prepare-analytics-data.sh` | 运行分析演示流水线并输出契约、模式和质量摘要 |

脚本均使用显式数据库路径，不覆盖已有数据库；数据库重置由原有`reset-demo-database.sh`完成，并保留备份。

## 验收命令

在Linux/Ubuntu候选工作树执行：

```bash
bash -n scripts/init-demo.sh scripts/start-all.sh scripts/stop-all.sh scripts/health-check.sh scripts/prepare-analytics-data.sh
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure --no-tests=error
bash scripts/init-demo.sh --reset-demo-data --db /tmp/charge-stage2-member1.sqlite
```

若分析依赖已安装：

```bash
bash scripts/prepare-analytics-data.sh
bash scripts/start-all.sh --stage 2 --db /tmp/charge-stage2-member1.sqlite
bash scripts/health-check.sh --stage 2 --db /tmp/charge-stage2-member1.sqlite
bash scripts/stop-all.sh
```

## 通过条件

- 所有C++构建和测试通过；
- 数据库`PRAGMA integrity_check`返回`ok`；
- 服务端只使用指定数据库；
- 分析接口停止时，基础登录、预约、充电和支付仍可用；
- 分析流程不修改业务表；
- 启停脚本可重复执行；
- 不提交数据库、raw数据、密钥、令牌和个人隐私；
- 第一阶段回归没有被第二阶段改动破坏。

## 未由成员1单独判定的项目

以下项目必须在三端集成后由成员1主持最终验收：

- Qt车主端和运营端对新增接口的实际页面操作；
- 成员3模拟器到服务端的真实遥测链路；
- FastAPI和Web大屏在真实或公开数据模式下的展示；
- 数据来源、质量报告、模型指标和大屏回算；
- 20个模拟桩稳定性测试；
- 9月17日答辩现场运行状态。

## 交付状态记录

- 代码修改范围：`server/`、`shared/`、`database/`、`tests/`、`scripts/`、公共文档；
- 成员2、成员3目录未修改；
- 未执行推送、合并或覆盖`main`；
- 最终通过状态必须以Ubuntu候选工作树的实际构建和测试输出为准。

