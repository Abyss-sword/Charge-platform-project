# 成员3 Windows 与 Ubuntu 联调前逐项验收记录

验收日期：2026-09-10

验收分支：`feature/member3-simulator-analytics-dashboard`

代码基线：`9143a2f`，另含本次验收发现的 Ubuntu 启动脚本可执行权限修复

对照清单：`docs/stage1-integration/member3-pre-integration-checklist-2026-09-09.md`

## 1. 验收结论

成员3负责的电桩模拟器、FastAPI只读分析服务和浅色Web大屏已在 Windows 与 Ubuntu 22.04 上完成构建、自动测试和真实进程联调，技术状态可标记为：

```text
READY FOR INTEGRATION
```

该结论表示成员3技术门禁已通过，可以进入三端联调；不等同于全组三端最终验收完成。仍需成员1在团队记录中书面确认累计电表口径，并由三名成员共同执行Qt车主端、Qt运营端、服务端、模拟器与Web大屏的两条完整跨端流程。

## 2. 双平台实际验证结果

| 验证项 | Windows 11 | Ubuntu 22.04 虚拟机 | 结论 |
| --- | --- | --- | --- |
| CMake/Ninja完整构建 | 214/214 targets | 214/214 targets | 通过 |
| 根工程CTest | 31/31 | 31/31 | 通过 |
| FastAPI pytest | 23/23 | 23/23 | 通过 |
| Dashboard Vitest | 10/10 | 10/10 | 通过 |
| Dashboard生产构建 | 617 modules transformed | 617 modules transformed | 通过 |
| 模拟器场景测试 | 9个命名场景 | 9个命名场景 | 通过 |
| TCP长度帧回环 | `TCP_FRAME_TEST=PASS` | `TCP_FRAME_TEST=PASS` | 通过 |
| 第一阶段补充验收 | 45/45 | 45/45 | 通过 |
| 真实C++服务端九场景 | 通过 | 通过 | 通过 |
| FastAPI同库读取 | HTTP 200、8项指标 | HTTP 200、8项指标 | 通过 |
| Web大屏实际打开 | `http://127.0.0.1:4173/` | HTTP 200 | 通过 |
| 统一启动入口 | PowerShell通过 | Bash通过 | 通过 |

Ubuntu 实测环境为 CMake 3.22.1、Ninja 1.10.1、g++ 11.4、Python 3.10.12、Node.js 22.23.2、npm 10.9.8 和 SQLite 3.37.2。Ubuntu 验收同时发现并修复了 `scripts/run-member3-stack.sh` 缺少 Git 可执行位的问题，文件模式由 `100644` 改为 `100755`。

## 3. 九类真实场景逐项结果

所有场景均连接真实 `charge_platform_server` 的 `127.0.0.1:48080`，没有使用本地假响应。

| 场景 | 实际结果 | 数据库或协议核查 |
| --- | --- | --- |
| `normal` | 发送2帧，确认2帧 | 遥测正常入库，桩保持正常状态 |
| `charging` | 发送4帧，确认4帧 | 功率与生命周期累计电量递增 |
| `overheat` | 发送3帧，确认3帧 | 产生且复用一个活动过热告警 |
| `offline` | 发送1帧，确认1帧，等待超过15秒 | 桩进入离线状态 |
| `recover` | 发送2帧，确认2帧 | 离线桩恢复为 `IDLE` |
| `duplicate` | 重发2帧，服务端均幂等确认 | 数据库只保留1条精确遥测 |
| `time-backward` | 第1帧确认，第2帧拒绝 | 返回 `TELEMETRY_OUT_OF_ORDER` |
| `energy-backward` | 第1帧确认，第2帧拒绝 | 返回 `TELEMETRY_OUT_OF_ORDER` |
| `unknown-pile` | 明确拒绝 | 返回 `PARAM_INVALID`，脏数据行数为0 |

真实九场景结束后的数据库检查结果：`PRAGMA integrity_check=ok`、外键错误0条、模拟遥测15条、重复场景遥测1条、活动过热告警1条、未知电桩遥测0条。

## 4. 按原清单逐项核查

### 4.1 模拟器计量与协议

- [x] `PILE_TELEMETRY.energyKwh`按电桩生命周期累计电表解释，正常情况下只增不减。
- [x] 状态文件持久化累计读数，设备进程重启后不会自动归零。
- [x] 时间戳包含时区，时间倒退和电量倒退均由服务端明确拒绝。
- [x] 精确重复上报保持幂等，不产生重复遥测、重复活动告警或重复计费输入。
- [x] `pileId`使用统一数据库中的 `pile_info.id`；未知电桩不写脏数据。
- [x] 模拟器只有收到服务端成功响应才计入 `ACKNOWLEDGED_FRAMES`。
- [x] 4字节网络序长度头、`"version":"1.0"`和唯一`requestId`均通过协议与回环测试。
- [ ] 成员1在团队记录中的书面确认尚未完成。这是协作签字项，不是代码缺陷。

主要代码证据：`simulator/pile/pile_simulator.cpp`、`simulator/pile/scenario.cpp`、`server/services/telemetry_service.cpp`、`database/migrations/007_meter_baseline_and_telemetry_idempotency.sql`。

### 4.2 可控演示场景

- [x] `normal`、`charging`、`overheat`、`offline`、`recover`、`duplicate`、`time-backward`、`energy-backward`、`unknown-pile`九个稳定名称均可从命令行切换。
- [x] 正常、过热、离线、恢复可重复运行。
- [x] 场景连接真实服务端地址；服务端不可达时失败退出，不输出假成功。

主要代码证据：`simulator/pile/scenario.cpp`、`simulator/pile/main.cpp`、`simulator/tests/scenario_test.cpp`、`simulator/tests/tcp_loopback_test.py`。

### 4.3 FastAPI同库只读

- [x] 数据库路径由 `CHARGE_PLATFORM_DB`传入，读取成员1服务端使用的同一个SQLite文件。
- [x] 连接使用SQLite只读URI，不创建第二套业务库，不修改订单、余额、电桩、告警或工单。
- [x] 正常、空数据、数据库不存在、忙碌、字段不兼容和只读性均有自动测试。
- [x] 返回总览、状态、趋势、告警、工单与数据质量；金额和电量以固定两位十进制字符串输出。
- [x] API指标与直接SQL抽查一致，访问前后数据库内容保持不变。

主要代码证据：`analytics/src/sqlite_dashboard.py`、`analytics/src/api.py`、`analytics/tests/test_api.py`。

### 4.4 指标口径与预测说明

- [x] 用电趋势按同一电桩相邻生命周期累计读数的非负差值汇总，不执行累计值求和。
- [x] 营收仅统计 `FINISHED`订单，空数据返回 `0.00`。
- [x] 在线率分母为全部电桩；即时使用率分母为 `IDLE + RESERVED + CHARGING`。
- [x] 预测明确标注规则估算、模型名称、更新时间、样本数和限制；样本不足时显示“数据不足”。
- [x] 数据质量检查覆盖缺失、时区、两位小数、倒退、孤儿关联、重复和 `SIMULATED`来源。

主要代码证据：`analytics/src/sqlite_dashboard.py`、`analytics/contracts/charge_event_v2.schema.json`、`docs/analytics-data-contract.md`。

### 4.5 Web实时刷新与异常状态

- [x] 总览卡片、状态、24小时电量趋势、告警、工单和数据质量均来自FastAPI，不依赖静态Mock。
- [x] 每3秒局部刷新，不整页重载；保留最后一次成功数据并显示过期状态。
- [x] 加载、空数据、接口断开和恢复自动刷新均通过组件测试。
- [x] 页面显示最后更新时间和 `SIMULATED`数据来源。
- [x] Windows实际前台打开浅色大屏；Ubuntu以真实进程验证首页与API均返回HTTP 200。

主要代码证据：`dashboard/src/App.vue`、`dashboard/src/adapters/dashboardDataAdapter.ts`、`dashboard/src/components/MetricStrip.vue`、`dashboard/src/components/PileStatusPanel.vue`、`dashboard/tests/dashboard.test.ts`。

### 4.6 启动、断线与可复现性

- [x] Windows使用 `scripts/run-member3-stack.ps1`，Ubuntu使用 `scripts/run-member3-stack.sh`。
- [x] 启动入口集中配置统一数据库、服务端地址、端口、桩ID与场景。
- [x] 启动入口只使用现有数据库，不自动重建、覆盖或复制业务库。
- [x] 空数据、服务断开、服务恢复和模拟器TCP回环均已验证。
- [x] Windows脚本可自动发现Qt/MinGW运行库，已消除 `Qt6Core.dll`、`Qt6Network.dll`、`Qt6Sql.dll`、`libgcc_s_seh-1.dll`和`libstdc++-6.dll`缺失弹窗。
- [x] Ubuntu脚本具有可执行权限，可由新克隆直接运行。

## 5. 复核命令

Windows：

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
& .\analytics\.venv\Scripts\python.exe -m pytest .\analytics\tests -q
npm --prefix .\dashboard test -- --run
npm --prefix .\dashboard run build
powershell -ExecutionPolicy Bypass -File .\scripts\run-member3-stack.ps1 -ServerHost 127.0.0.1 -PileId 101 -Scenario normal
```

Ubuntu：

```bash
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
analytics/.venv/bin/python -m pytest analytics/tests -q
npm --prefix dashboard test -- --run
npm --prefix dashboard run build
CHARGE_SERVER_HOST=127.0.0.1 CHARGE_PILE_ID=101 CHARGE_SCENARIO=normal ./scripts/run-member3-stack.sh
```

## 6. 尚需三人共同完成的事项

以下项目没有伪装成成员3可独立完成：

1. 成员1在团队记录中书面确认生命周期累计电表与订单起始基线规则；
2. 使用成员2车主端连续执行两遍登录、预约、开始、停止、支付，并核对Web大屏；
3. 使用成员2运营端执行告警确认和工单处理，并核对服务端、数据库和Web大屏；
4. 保存最终三端截图、录屏和三方验收记录。

因此，当前准确表述是：**成员3技术实现和双平台验证已完成，已准备进入三端联调；全组第一阶段最终验收仍待三方现场流程。**
