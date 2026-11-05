# 东软电动汽车充电桩应用管理平台 14日压缩实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 由3人在9月17日前完成可重复演示、可测试、具备找桩—充电—结算—运维—分析闭环的教学型充电桩管理平台，并在当天完成全量验收和最终交付。

**Architecture:** 采用一个 C++/Qt 多线程服务端作为业务状态唯一来源，Qt 车主端、Qt 运营端和桩模拟器通过带长度头的 JSON/TCP 协议通信，SQLite 保存业务事实；Vue/ECharts 大屏和 Python 分析服务只读业务数据并返回预测/推荐结果。实施按纵向业务切片推进，每个任务都产生可运行结果，避免各端长期分开开发后才集中联调。

**Tech Stack:** Ubuntu 22.04、C++17、Qt 6.2+（Widgets/Network/Sql/Charts/WebEngine/Tests）、CMake、SQLite、Python 3.10+、pandas、scikit-learn、FastAPI、Vue 3、Vite、ECharts、Vitest、Git。

**团队执行补充：** 三人4:3:3分工、两阶段14日压缩日历、技术替代方案和合并测试流程见 [三人协作与逐日实施计划](2026-09-03-three-person-collaboration-plan.md)。成员领取任务时分别使用 [成员2：Qt双端与界面执行手册](2026-09-03-member-2-qt-ui-execution-guide.md) 和 [成员3：模拟、数据与大屏执行手册](2026-09-03-member-3-data-dashboard-execution-guide.md)。

---

## 0. 执行约定

- 项目根目录统一为 `charge_platform_project/`；本文路径均相对该目录。
- 每个任务由一名主负责人修改其归属文件；共享协议、数据库脚本和状态定义由成员 1 审核后合并。
- 每个代码任务遵循“先写失败测试 → 确认失败 → 最小实现 → 确认通过 → 提交”。
- 每日结束前将任务中的复核证据写入 `docs/evidence/day-N.md`，至少包含执行命令、结果和已知限制。
- 提交格式使用 `feat:`、`fix:`、`test:`、`docs:`、`chore:`；一个提交只完成一个可说明的结果。
- 若某步失败，先修复当前任务，不跳到后续任务建立兼容补丁。

### 0.1 两阶段验收和衔接规则

- **第一阶段：** 9月3～9日开发，9月9日14:00停止新增功能并完成内部验收，9月10日正式验收。此阶段必须完成项目说明书涉及的主要窗口、页面导航、交互状态和基础业务闭环。车主端、运营端与Web大屏均可使用可重复生成的模拟数据；关闭真实数据导入和正式模型后，仍能演示全部主要页面及登录、找站、预约、模拟充电、计费、结算、设备管理、告警工单和运营查看。
- **第二阶段：** 9月11～16日开发，9月16日12:00停止新增功能并完成内部验收，9月17日进行第二阶段和全项目的正式验收。此阶段以第一阶段已经完成的窗口、接口和模拟业务为基础，接入来源明确的匿名历史或公开聚合数据，补齐准入、脱敏、映射、质量检查、聚合、基线、模型、推荐和大屏回算，并完善与真实数据有关的逻辑。第二阶段不得推翻第一阶段界面，也不能让基础充电流程依赖分析服务。
- 第一阶段已经冻结 `charge_event_v2` 契约和只读导出样例。金额、电量和单价使用固定两位十进制字符串，单位分别为元、kWh 和元/kWh；字段至少包含来源记录号、来源站点号、内部站点号、事件发生时间、导入时间、时区、`energy_kwh`、秒、`amount_yuan`、数据模式、数据集名称/版本和导入批次。`charge_event_v1` 仅作历史参考。
- 数据模式只允许 `SIMULATED`、`REAL_ANONYMIZED`、`PUBLIC_AGGREGATED`；模型、接口和大屏必须显示当前模式，禁止把模拟数据标为真实数据。
- SQLite 始终是订单、余额、设备和工单的唯一业务事实来源。第二阶段数据放在隔离的 raw/staging/curated 目录中，分析流程只读业务数据，禁止反向写入业务状态。
- 真实原始数据不得进入 Git；是否随发布包分发由来源许可决定。来源、允许用途、覆盖时间、字段映射、拒绝记录和处理批次必须可追溯。
- 9 月 9 日内部验收未通过时不得进入正式真实数据导入；9 月 11 日无法确认来源或教学使用许可时，改用来源明确的公开匿名/聚合数据，不得伪造真实数据。

### 0.2 两阶段14日压缩日历

项目从9月3日开始准备，9月17日完成全量验收，实际可用时间约14个自然日。后文Task 1～20全部保留，它们是技术工作包，不是20个自然日；同一天必须并行执行多个工作包。9月17日之后没有开发、测试或补交窗口。

| 工作日 | 日期 | 当日停止条件 | 主要技术工作包 | 当日必须留下的证据 |
| ---: | --- | --- | --- | --- |
| D1 | 9月3日 | 需求、协议草案、目录责任和全部界面清单没有歧义 | Task 1～3 的骨架与契约；Task 5、10、15 的页面清单 | 构建日志、需求矩阵、协议样例、页面清单、`day-1.md` |
| D2 | 9月4日 | 12:00冻结协议；数据库、三类连接和所有主要窗口骨架可启动 | Task 2～5；Task 10、15 的模拟数据页面骨架 | 帧测试、数据库测试、窗口导航录屏、连接日志 |
| D3 | 9月7日 | 登录、站点、账户、管理查询和大屏模拟数据可运行 | Task 6～7；Task 10、15 的模拟数据适配 | 登录、冻结、距离排序、空数据、断网和大屏截图 |
| D4 | 9月8日 | 预约、充电、支付前状态、告警工单页面均能用模拟数据演示 | Task 8；Task 11～12 的模拟异常和页面部分 | 状态机、并发预约、活动订单、告警工单记录 |
| D5 | 9月9日 | 14:00后停止新增功能；所有主要页面和模拟业务完成内验 | Task 9～10；Task 18～20 的阶段一测试、脚本和材料 | 全量C++测试、`basic_charge_flow.sh`、页面清单、内验记录 |
| D6 | 9月10日 | 第一阶段正式验收通过并冻结界面、协议、业务库和数据契约 | 第一阶段缺陷修复与冻结 | `stage-1-acceptance.md`、阶段标签、验收截图 |
| D7 | 9月11日 | 遥测、固定异常、来源准入、raw导入和大屏真实数据接口骨架通过 | Task 11；Task 15 的真实数据接口骨架 | 遥测测试、来源清单、raw批次和五种页面状态 |
| D8 | 9月14日 | 告警工单、staging/rejected、curated和基线均通过 | Task 12～13；Task 17 的60秒预检 | 工单闭环、质量报告、聚合回算、基线结果、负载预检 |
| D9 | 9月15日 | 树模型、分析API、推荐、大屏、恢复和600秒性能测试通过 | Task 14～17 | Python/Web测试、两次评测、跨端流程、性能报告 |
| D10 | 9月16日 | 12:00后停止新增功能；全部功能、发布脚本、文档和全量测试完成内验 | Task 18～20；全体工作包回归 | C++、Python、Web、端到端、冷启动、两遍演练和内验记录 |
| D11 | 9月17日 | 第二阶段及全项目验收通过，当天完成最终冻结和交付 | 仅修验收阻断问题；完成Task 20冻结步骤 | 全量验收、数据回算、最终标签、提交包和上传记录 |

### 0.3 Agent执行契约

把某个工作日交给其他模型或 Agent 时，提示词必须包含以下字段，缺一项就不派发：

```text
日期与工作日：D<n> / 2026-09-<dd>
唯一目标：一句话描述当天可运行的结果
允许修改：逐条列出文件或目录
禁止修改：其他成员当天拥有的共享文件、已冻结契约和业务状态含义
输入：依赖的协议版本、数据库版本、配置和测试数据
步骤：先失败测试，再最小实现，再相关测试，再主线回归
验收命令：可复制执行的完整命令
预期输出：通过数量、状态值或文件名
证据：写入 docs/evidence/day-<n>.md 的内容
失败处理：停止条件、回退动作和需要通知的人
```

Agent 每次只领取一个可独立验证的工作包。协议、数据库迁移、订单状态机和发布脚本由成员1单写；两个 Qt 端由成员2单写；`simulator/`、`analytics/`、`dashboard/` 由成员3单写。共享文件需要改动时先提交接口变更单，不允许两个 Agent 同时修改。

## 1. 计划文件结构

```text
charge_platform_project/
├─ CMakeLists.txt                       # C++/Qt 总构建入口
├─ cmake/ProjectOptions.cmake           # 编译警告、测试开关
├─ shared/
│  ├─ protocol/                         # 帧编解码、请求/响应、错误码
│  ├─ domain/                           # 订单状态、金额、推荐输入等共享类型
│  └─ config/                           # 配置读取与示例配置
├─ server/
│  ├─ app/                              # 服务启动与依赖组装
│  ├─ network/                          # TCP 连接、线程池、请求路由
│  ├─ services/                         # 登录、站点、订单、支付、告警业务
│  └─ repositories/                     # SQLite 数据访问
├─ simulator/pile/                      # 充电桩心跳、遥测、故障模拟
├─ clients/user/                        # Qt 车主端页面、视图模型、服务代理
├─ clients/admin/                       # Qt 运营端页面、视图模型、服务代理
├─ database/
│  ├─ migrations/001_initial.sql        # 正式表结构
│  └─ seeds/demo.sql                    # 可重复演示数据
├─ analytics/
│  ├─ contracts/                        # 版本化分析字段和单位契约
│  ├─ manifests/                        # 数据来源、许可、版本和批次说明
│  ├─ src/                              # 准入、清洗、聚合、训练、预测、推荐
│  ├─ tests/                            # Python 单元与接口测试
│  └─ artifacts/                        # 本地模型与评测输出，不保存敏感数据
├─ dashboard/
│  ├─ src/                              # Vue/ECharts 大屏
│  └─ tests/                            # Vitest 测试
├─ tests/
│  ├─ unit/                             # C++ 规则与协议测试
│  ├─ integration/                      # 数据库、服务和协议集成测试
│  └─ e2e/                              # 端到端脚本和验收数据
├─ scripts/                             # 初始化、启动、停止、健康检查
├─ runtime/analytics/                   # raw/staging/curated/rejected；全部忽略，不进 Git
├─ config/app.example.ini               # 无密钥配置模板
└─ docs/                                # 协议、数据字典、测试与答辩证据
```

### 1.1 前端实现基线

所有界面必须遵循 [前端界面设计规格](../specs/2026-09-03-frontend-ui-design.md)。第一阶段先完成完整信息结构、导航、共用组件和状态，再增加统一配色、渐变与短时动效；第二阶段保持窗口不变，只替换数据适配器和完善真实数据逻辑。

新增前端文件职责如下：

```text
clients/shared/ui/
├─ theme.h/.cpp                         # 深海蓝、操作蓝、充电绿等统一设计变量
├─ common.qss                           # 按钮、输入框、卡片、表格、标签、弹窗样式
├─ motion.h/.cpp                        # 150～500ms短时动效和关闭开关
├─ icons/                               # 统一线性SVG图标
└─ widgets/                             # 状态标签、加载/空/错/降级页、确认弹窗、Toast
clients/user/
├─ shell/main_window.*                  # PageHeader、底部五项导航、活动订单摘要和页面切换
├─ adapters/user_data_provider.*        # SIMULATED/正式接口的同一视图模型入口
└─ pages/                               # 登录、首页、站点、导航、预约、充电、恢复、结算、账户、记录
clients/admin/
├─ shell/main_window.*                  # 深海蓝侧栏、顶栏和工作区
├─ adapters/admin_data_provider.*       # 管理查询的统一视图模型入口
└─ pages/                               # 登录、概览、营收、站点、设备、订单、用户、告警工单、日志
dashboard/src/
├─ styles/tokens.css                    # 与Qt含义一致的颜色、间距、圆角和字号
├─ styles/motion.css                    # 卡片、数字和图表动效；支持reduced-motion
├─ adapters/dashboardDataAdapter.ts     # 模拟/真实/公开数据与降级的唯一切换点
├─ components/common/                   # 来源栏、状态、骨架屏、空数据和错误反馈
└─ components/charts/                   # 负荷、排行、设备和质量图表
```

界面任务按日期冻结：

| 日期 | Qt车主端 | Qt运营端 | 浅色Web大屏 | 当日证据 |
| --- | --- | --- | --- | --- |
| 9月4日 | 主窗口、固定底部五项导航、11类页面骨架、通用状态 | 主窗口、固定侧栏、9类页面骨架、通用状态 | 顶栏、五指标、负荷、排行、告警、推荐、质量区域骨架 | 三端导航录屏；360×780、1180×720、1366×768截图 |
| 9月7日 | 登录、首页、站点列表/详情、导航降级 | 登录、概览、设备、订单骨架 | 同一批模拟站点驱动指标、负荷和排行 | 输入错误、加载、空数据、断线、地图降级截图 |
| 9月8日 | 预约、充电、活动订单恢复、待支付跳转 | 活动订单、告警列表、工单时间线、日志模拟状态 | 模拟告警流和分流建议 | 并发失败、过期、重连、异常状态录屏 |
| 9月9日 | 结算、账户、充值、充电记录和全部状态 | 营收、站点、设备、订单、用户、告警工单、日志完整 | 全区域 `SIMULATED`，浅色主题和允许的短动效完整 | 全页面清单、两种分辨率、关闭动效、第一阶段业务录屏 |
| 9月11～15日 | 不改变导航，接入模式、版本、覆盖时间、推荐理由和降级 | 不改变侧栏，接入故障、正式数据和工单反馈 | 统一适配器切换正式数据，完成回算和模型/基线状态 | 模拟与正式模式对照；分析停止后的降级录屏 |
| 9月16日 | 全窗口回归和用户指南 | 全窗口回归和操作说明 | 1366×768/1920×1080、动效开关、来源与回算 | 截图、录屏、测试输出和两遍演练记录 |

每个页面至少验证加载、正常、空数据、失败四种状态；网络或外部服务页面再验证断线/重连或降级。颜色必须同时配合图标或文字。动效不参与业务判断，关闭后所有测试和操作路径必须保持一致。

## 技术工作包 A：冻结基线并跑通最小骨架

### Task 1：建立工程骨架与统一构建入口

**Owner:** 成员 1；成员 2、3只验证各自环境。
**Files:**
- Create: `charge_platform_project/.gitignore`
- Create: `charge_platform_project/CMakeLists.txt`
- Create: `charge_platform_project/cmake/ProjectOptions.cmake`
- Create: `charge_platform_project/tests/unit/smoke_test.cpp`
- Create: `charge_platform_project/config/app.example.ini`
- Create: `charge_platform_project/docs/evidence/day-1.md`

- [ ] **Step 1: 初始化版本库并固定忽略规则**

Run: `cd /home/bit/projects/charge_platform_project && git init && git branch -M main`
Expected: 输出已初始化空版本库；`git branch --show-current` 输出 `main`。

```gitignore
build/
runtime/
analytics/.venv/
analytics/artifacts/*.joblib
dashboard/node_modules/
dashboard/dist/
config/app.ini
*.db
*.db-shm
*.db-wal
```

- [ ] **Step 2: 创建最小失败测试**

```cpp
// tests/unit/smoke_test.cpp
#include <QtTest>
class SmokeTest final : public QObject {
    Q_OBJECT
private slots:
    void qtRuntimeIsAvailable() { QCOMPARE(QStringLiteral("qt"), QStringLiteral("qt")); }
};
QTEST_MAIN(SmokeTest)
#include "smoke_test.moc"
```

- [ ] **Step 3: 在尚无构建配置时确认构建失败**

Run: `cmake -S charge_platform_project -B charge_platform_project/build -G Ninja -DBUILD_TESTING=ON`
Expected: FAIL，提示缺少 `CMakeLists.txt`。

- [ ] **Step 4: 写入最小 CMake 配置**

```cmake
cmake_minimum_required(VERSION 3.22)
project(charging_platform LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Network Sql Test Widgets Charts WebEngineWidgets)
include(CTest)
qt_add_executable(smoke_test tests/unit/smoke_test.cpp)
target_link_libraries(smoke_test PRIVATE Qt6::Test)
add_test(NAME smoke_test COMMAND smoke_test)
```

- [ ] **Step 5: 创建配置模板，禁止真实密钥进入仓库**

```ini
[server]
host=127.0.0.1
port=48080
database=runtime/charging.db
[map]
provider=tencent
api_key=
[analytics]
base_url=http://127.0.0.1:48100
```

- [ ] **Step 6: 构建并运行冒烟测试**

Run: `cmake -S charge_platform_project -B charge_platform_project/build -G Ninja -DBUILD_TESTING=ON && cmake --build charge_platform_project/build && ctest --test-dir charge_platform_project/build --output-on-failure`
Expected: `100% tests passed, 0 tests failed`。

- [ ] **Step 7: 提交**

```bash
git add charge_platform_project/CMakeLists.txt charge_platform_project/cmake charge_platform_project/tests charge_platform_project/config charge_platform_project/docs
git commit -m "chore: establish charging platform build baseline"
```

### Task 2：冻结报文帧、错误码和请求模型

**Owner:** 成员 1。
**Files:**
- Create: `charge_platform_project/shared/protocol/frame_codec.h`
- Create: `charge_platform_project/shared/protocol/frame_codec.cpp`
- Create: `charge_platform_project/shared/protocol/message.h`
- Create: `charge_platform_project/shared/protocol/error_code.h`
- Create: `charge_platform_project/tests/unit/frame_codec_test.cpp`
- Create: `charge_platform_project/docs/protocol.md`

- [ ] **Step 1: 写入半包、粘包和超长报文失败测试**

```cpp
// tests/unit/frame_codec_test.cpp
#include <QtTest>
#include "shared/protocol/frame_codec.h"
class FrameCodecTest final : public QObject {
    Q_OBJECT
private slots:
    void waitsForCompleteFrame() {
        FrameCodec codec;
        const QByteArray frame = FrameCodec::encode("{\"action\":\"PING\"}");
        QCOMPARE(codec.feed(frame.left(5)).size(), 0);
        QCOMPARE(codec.feed(frame.mid(5)), QList<QByteArray>{"{\"action\":\"PING\"}"});
    }
    void extractsTwoFrames() {
        FrameCodec codec;
        QCOMPARE(codec.feed(FrameCodec::encode("A") + FrameCodec::encode("B")),
                 QList<QByteArray>({"A", "B"}));
    }
};
QTEST_MAIN(FrameCodecTest)
#include "frame_codec_test.moc"
```

- [ ] **Step 2: 运行并确认失败**

Run: `cmake --build charge_platform_project/build && ctest --test-dir charge_platform_project/build -R frame_codec_test --output-on-failure`
Expected: FAIL，提示 `frame_codec.h` 不存在。

- [ ] **Step 3: 实现 4 字节长度头编解码**

```cpp
// shared/protocol/frame_codec.h
#pragma once
#include <QByteArray>
#include <QList>
class FrameCodec {
public:
    static constexpr quint32 MaxPayload = 1024 * 1024;
    static QByteArray encode(const QByteArray &payload);
    QList<QByteArray> feed(const QByteArray &bytes);
private:
    QByteArray buffer_;
};
```

实现要求：使用网络字节序读写长度；长度为 0 或超过 `MaxPayload` 时清空当前帧并返回协议错误；保留不足一帧的字节等待下次读取。

- [ ] **Step 4: 定义固定错误码**

```cpp
enum class ErrorCode {
    Ok, AuthInvalid, ParamInvalid, UserFrozen, UnfinishedOrder,
    PileUnavailable, OrderStateConflict, BalanceInsufficient,
    DuplicateRequest, MapUnavailable, DeviceTimeout,
    DatabaseError, ModelUnavailable, ProtocolError
};
```

- [ ] **Step 5: 构建并验证测试通过**

Run: `ctest --test-dir charge_platform_project/build -R frame_codec_test --output-on-failure`
Expected: PASS。

- [ ] **Step 6: 在 `docs/protocol.md` 固定请求字段、响应字段、动作名和最大报文长度并提交**

```bash
git add charge_platform_project/shared charge_platform_project/tests/unit/frame_codec_test.cpp charge_platform_project/docs/protocol.md charge_platform_project/CMakeLists.txt
git commit -m "feat: define framed json protocol contract"
```

### Task 3：建立 SQLite 结构、演示数据与阶段二分析契约

**Owner:** 成员 1负责业务库；成员 3设计分析字段和种子数据；成员 2复核客户端所需字段。
**Files:**
- Create: `charge_platform_project/database/migrations/001_initial.sql`
- Create: `charge_platform_project/database/seeds/demo.sql`
- Create: `charge_platform_project/server/repositories/database.cpp`
- Create: `charge_platform_project/server/repositories/database.h`
- Create: `charge_platform_project/tests/integration/schema_test.cpp`
- Create: `charge_platform_project/tests/integration/analytics_export_contract_test.cpp`
- Create: `charge_platform_project/docs/data-dictionary.md`
- Create: `charge_platform_project/docs/analytics-data-contract.md`
- Create: `charge_platform_project/analytics/contracts/charge_event_v1.schema.json`

- [ ] **Step 1: 写数据库约束失败测试**

```cpp
void SchemaTest::rejectsDuplicatePhoneAndPileSn() {
    QSqlDatabase db = openMemoryDatabaseAndMigrate();
    QVERIFY(exec(db, "INSERT INTO user_info(phone,nickname,balance_cents,status) VALUES('13800138000','用户8000',10000,'NORMAL')"));
    QVERIFY(!exec(db, "INSERT INTO user_info(phone,nickname,balance_cents,status) VALUES('13800138000','重复',10000,'NORMAL')"));
}
```

- [ ] **Step 2: 确认测试因无迁移脚本失败**

Run: `ctest --test-dir charge_platform_project/build -R schema_test --output-on-failure`
Expected: FAIL，提示 `user_info` 不存在。

- [ ] **Step 3: 创建 11 张核心表**

迁移后的数据库必须包含：`admin_user`、`user_info`、`station_info`、`pile_info`、`charge_order`、`wallet_transaction`、`pile_telemetry`、`alarm_event`、`maintenance_ticket`、`operation_log`、`request_dedup`；开启 `PRAGMA foreign_keys=ON`；手机号、桩编号和请求编号设置唯一约束。004迁移后金额、电量和单价使用固定两位 `TEXT`，单位分别为元、kWh 和元/kWh；业务计算统一使用 `DecimalValue`，不得恢复整数分/Wh或SQLite浮点金额计算。

```sql
CREATE TABLE charge_order (
  id TEXT PRIMARY KEY,
  user_id INTEGER NOT NULL REFERENCES user_info(id),
  pile_id INTEGER NOT NULL REFERENCES pile_info(id),
  status TEXT NOT NULL CHECK(status IN ('RESERVED','CHARGING','PENDING_PAYMENT','FINISHED','CANCELED','EXPIRED')),
  energy_wh INTEGER NOT NULL DEFAULT 0,
  amount_cents INTEGER NOT NULL DEFAULT 0,
  reserved_until TEXT,
  started_at TEXT,
  ended_at TEXT,
  end_reason TEXT,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
```

- [ ] **Step 4: 插入可重复演示数据**

`demo.sql` 生成 3 个站点、12 个电桩、20 个用户、30 天完成订单、3 条告警；使用固定时间种子和固定编号，使每次初始化结果一致。

- [ ] **Step 5: 冻结 `charge_event_v1` 只读契约**

契约规定：`source_record_id`、`source_station_id`、`station_id`、`occurred_at`、`ingested_at`、`timezone`、`energy_wh`、`duration_seconds`、`amount_cents`、`status`、`data_mode`、`dataset_name`、`dataset_version`、`ingest_batch`。第一阶段导出固定使用 `data_mode=SIMULATED`；时间保存为带时区的 ISO 8601，电量/金额/时长分别统一为 Wh/分/秒。契约不得包含完整手机号、姓名、支付账号或车辆唯一标识。

`analytics_export_contract_test` 使用演示订单导出一行，断言字段齐全、单位为整数、时间带时区、数据模式正确且不含 `phone`、`name`、`payment_account`。

- [ ] **Step 6: 运行结构与契约测试并查询数量**

Run: `ctest --test-dir charge_platform_project/build -R "schema_test|analytics_export_contract_test" --output-on-failure`
Expected: PASS。
Run: `sqlite3 charge_platform_project/runtime/test.db "select count(*) from station_info; select count(*) from pile_info;"`
Expected: 输出 `3` 和 `12`。

- [ ] **Step 7: 更新数据字典并提交**

```bash
git add charge_platform_project/database charge_platform_project/server/repositories charge_platform_project/tests/integration charge_platform_project/docs/data-dictionary.md charge_platform_project/docs/analytics-data-contract.md charge_platform_project/analytics/contracts
git commit -m "feat: freeze business schema and analytics export contract"
```

### Task 4：实现多线程服务端、请求路由和健康检查

**Owner:** 成员 1。
**Files:**
- Create: `charge_platform_project/server/app/main.cpp`
- Create: `charge_platform_project/server/network/tcp_server.h`
- Create: `charge_platform_project/server/network/tcp_server.cpp`
- Create: `charge_platform_project/server/network/request_router.h`
- Create: `charge_platform_project/server/network/request_router.cpp`
- Create: `charge_platform_project/tests/integration/server_health_test.cpp`

- [ ] **Step 1: 写 `PING` 集成测试**

```cpp
void ServerHealthTest::returnsPongWithSameRequestId() {
    TestServer server;
    const QJsonObject response = server.send({
        {"version", "1.0"}, {"requestId", "REQ-1"}, {"action", "PING"}, {"payload", QJsonObject{}}
    });
    QCOMPARE(response["requestId"].toString(), QString("REQ-1"));
    QCOMPARE(response["ok"].toBool(), true);
    QCOMPARE(response["data"].toObject()["reply"].toString(), QString("PONG"));
}
```

- [ ] **Step 2: 确认测试失败**

Run: `ctest --test-dir charge_platform_project/build -R server_health_test --output-on-failure`
Expected: FAIL，无法连接服务端。

- [ ] **Step 3: 实现监听与线程边界**

主线程只接受连接；每个连接对象归属其工作线程；数据库连接按线程名称创建；请求路由只接收解析后的 `Message`，不直接读取 Socket。

```cpp
router.registerHandler("PING", [](const Message &m, RequestContext &) {
    return Response::success(m.requestId, QJsonObject{{"reply", "PONG"}});
});
```

- [ ] **Step 4: 验证健康检查和未知动作**

Run: `ctest --test-dir charge_platform_project/build -R server_health_test --output-on-failure`
Expected: PASS；未知动作返回 `PARAM_INVALID`，服务端保持运行。

- [ ] **Step 5: 记录线程模型并提交**

```bash
git add charge_platform_project/server charge_platform_project/tests/integration/server_health_test.cpp charge_platform_project/docs
git commit -m "feat: run framed requests through threaded server"
```

### Task 5：建立车主端、运营端和桩模拟器最小连接

**Owner:** 成员 2负责两个 Qt 客户端；成员 3负责桩模拟器；成员 1负责共享连接契约。
**Files:**
- Create: `charge_platform_project/clients/user/main.cpp`
- Create: `charge_platform_project/clients/user/service_client.*`
- Create: `charge_platform_project/clients/user/shell/main_window.*`
- Create: `charge_platform_project/clients/admin/main.cpp`
- Create: `charge_platform_project/clients/admin/service_client.*`
- Create: `charge_platform_project/clients/admin/shell/main_window.*`
- Create: `charge_platform_project/clients/shared/ui/theme.*`
- Create: `charge_platform_project/clients/shared/ui/common.qss`
- Create: `charge_platform_project/clients/shared/ui/widgets/page_state_widget.*`
- Create: `charge_platform_project/clients/shared/ui/widgets/status_badge.*`
- Create: `charge_platform_project/clients/shared/ui/motion.*`
- Create: `charge_platform_project/simulator/pile/main.cpp`
- Create: `charge_platform_project/simulator/pile/pile_simulator.*`
- Create: `charge_platform_project/tests/integration/multi_client_test.cpp`
- Create: `charge_platform_project/tests/ui/navigation_smoke_test.cpp`

- [ ] **Step 1: 写三类客户端同时连接测试**

测试启动两个 Qt 服务代理和一个桩模拟器，分别发送 `PING` 与 `PILE_HEARTBEAT`；断言三个响应均保留请求编号。

- [ ] **Step 2: 确认测试失败**

Run: `ctest --test-dir charge_platform_project/build -R multi_client_test --output-on-failure`
Expected: FAIL，缺少客户端连接类。

- [ ] **Step 3: 实现共享连接状态**

```cpp
enum class ConnectionState { Disconnected, Connecting, Connected, Reconnecting };
```

`service_client` 负责帧编解码、请求编号、超时和重连；窗口只订阅 `connectionStateChanged` 与 `responseReceived`，不得直接操作 `QTcpSocket`。

- [ ] **Step 4: 建立统一主题、窗口外壳和页面状态**

`theme` 定义设计规格中的12类颜色、4px间距基线、8/12/16px圆角和字号；`common.qss` 统一按钮、输入框、卡片、表格、侧栏、状态标签、弹窗和Toast。车主端以Qt Widgets创建紧凑PageHeader＋中央QStackedWidget浅灰内容区＋底部固定五项导航（首页/找桩/订单/记录/我的），二级页面通过入口、返回和业务流程到达；基准480×1040、最小360×780，内容使用Layout与QScrollArea单列纵向排列，顶部关键操作、底部导航和主操作完整可见，核心业务不依赖横向滚动，运营端创建深海蓝侧栏＋顶部栏＋浅灰工作区。`page_state_widget` 必须能切换加载、正常、空数据、失败、断线/重连和降级，不允许页面自己绘制另一套提示。

`motion` 只提供页面淡入、卡片依次进入、数字变化和状态颜色过渡，默认150～500ms；配置 `ui/animations=false` 时立即显示最终状态。先让 `navigation_smoke_test` 断言车主端11类页面、运营端9类页面均已注册，再创建空骨架使测试通过。

- [ ] **Step 5: 实现桩心跳**

桩模拟器每 5 秒发送 `PILE_HEARTBEAT`，内容包含 `pileSn`、`status` 和时间；使用命令行参数 `--pile PILE-001` 设置编号。

- [ ] **Step 6: 验证并完成骨架里程碑**

Run: `ctest --test-dir charge_platform_project/build -R "server_health_test|multi_client_test|schema_test|frame_codec_test" --output-on-failure`
Expected: 连接测试全部PASS。人工检查车主端、运营端显示带图标和文字的“已连接”，全部导航入口可达；分别在360×780和1180×720下无关键内容裁切；关闭动画后导航和连接状态不变；数据库记录桩心跳。

- [ ] **Step 7: 提交**

```bash
git add charge_platform_project/clients charge_platform_project/simulator charge_platform_project/tests/integration/multi_client_test.cpp charge_platform_project/tests/ui/navigation_smoke_test.cpp charge_platform_project/CMakeLists.txt
git commit -m "feat: connect user admin and pile clients"
```

## 技术工作包 B：完成基础业务闭环

### Task 6：手机号登录、自动注册和受限模式

**Owner:** 成员 1（服务）、成员 2（界面）。
**Files:**
- Create: `charge_platform_project/server/services/user_service.*`
- Create: `charge_platform_project/server/repositories/user_repository.*`
- Create: `charge_platform_project/clients/user/pages/login_page.*`
- Create: `charge_platform_project/clients/user/pages/home_page.*`
- Create: `charge_platform_project/clients/user/models/user_session.*`
- Create: `charge_platform_project/clients/user/adapters/user_data_provider.*`
- Create: `charge_platform_project/tests/unit/user_service_test.cpp`

- [ ] **Step 1: 写新用户、重复登录、非法手机号、冻结用户测试**

```cpp
void UserServiceTest::createsNewUserOnce() {
    auto first = service.loginOrRegister("13988886666", "REQ-L1");
    auto second = service.loginOrRegister("13988886666", "REQ-L2");
    QCOMPARE(first.userId, second.userId);
    QCOMPARE(first.nickname, QString("用户6666"));
    QCOMPARE(repo.countByPhone("13988886666"), 1);
}
```

- [ ] **Step 2: 确认失败后实现手机号规则与事务注册**

手机号使用 `^1[3-9]\d{9}$`；新用户默认余额 10000 分并生成一条 `INITIAL_CREDIT` 流水；冻结用户返回会话但设置 `restricted=true`。

- [ ] **Step 3: 实现登录页状态**

登录页使用单列纵向品牌说明＋手机号表单布局，显示教学模拟和连接状态。登录按钮在请求期间禁用；非法输入就地提示；服务断开时保留输入并允许重试。首页使用可垂直滚动的纵向卡片，包含当前位置、连接状态、附近可用站点摘要、活动订单摘要、深海蓝渐变找站主卡、余额/订单/最近站点快捷卡和推荐站点。受限用户进入主界面后只保留订单、结算和个人信息入口。`user_data_provider` 是模拟与正式接口的唯一切换点，页面不得自行硬编码数据。

- [ ] **Step 4: 运行测试和人工检查**

Run: `ctest --test-dir charge_platform_project/build -R user_service_test --output-on-failure`
Expected: PASS。人工使用 `13988886666` 登录两次，数据库只有一个用户。

- [ ] **Step 5: 提交**

```bash
git add charge_platform_project/server/services/user_service.* charge_platform_project/server/repositories/user_repository.* charge_platform_project/clients/user charge_platform_project/tests/unit/user_service_test.cpp
git commit -m "feat: add idempotent phone login and restricted sessions"
```

### Task 7：站点查询、距离计算和降级定位

**Owner:** 成员 1（查询）、成员 2（车主端）。
**Files:**
- Create: `charge_platform_project/shared/domain/geo.*`
- Create: `charge_platform_project/server/services/station_service.*`
- Create: `charge_platform_project/clients/user/pages/station_list_page.*`
- Create: `charge_platform_project/clients/user/pages/station_detail_page.*`
- Create: `charge_platform_project/clients/user/pages/navigation_page.*`
- Create: `charge_platform_project/clients/user/services/map_service.*`
- Create: `charge_platform_project/tests/unit/geo_test.cpp`
- Create: `charge_platform_project/tests/unit/station_service_test.cpp`

- [ ] **Step 1: 写距离和稳定排序测试**

```cpp
void GeoTest::computesKnownDistance() {
    const double km = haversineKm({23.1291, 113.2644}, {23.1300, 113.2700});
    QVERIFY(qAbs(km - 0.58) < 0.08);
}
```

- [ ] **Step 2: 确认失败并实现 Haversine 计算**

输入纬度限制 `[-90,90]`，经度限制 `[-180,180]`；距离相同时按站点 ID 稳定排序。

- [ ] **Step 3: 实现本地定位降级**

`map_service` 读取腾讯地图密钥；无密钥、超时或错误响应时返回 `MAP_UNAVAILABLE`，车主端切换到 3 个预置区域坐标，并显示“地图服务不可用，当前使用模拟位置”。

- [ ] **Step 4: 实现站点、桩详情和导航界面**

附近站点采用列表/地图模式切换，默认纵向列表，同一内容区切换地图并保留筛选和所选站点，不新增页面或使用左右双栏。列表顶部提供可换行的位置、距离、快慢充、价格、仅看空闲和排序筛选；卡片显示站名、价格、总桩、空闲桩、距离和营业状态。详情采用纵向信息卡片显示站点地址、价格说明以及桩编号、快慢充、功率、空闲/在用/故障/离线；不可用桩不显示可执行预约按钮。`navigation_page` 使用 `QWebEngineView` 加载由起点、终点和驾车/步行模式生成的腾讯地图路线地址；收到 `MAP_UNAVAILABLE` 时隐藏空白Web视图，显示目标地址、经纬度、“复制地址”“返回详情”和“继续预约”，不阻断后续预约。

- [ ] **Step 5: 验证正常和降级路径**

Run: `ctest --test-dir charge_platform_project/build -R "geo_test|station_service_test" --output-on-failure`
Expected: PASS。人工清空地图密钥后仍可选择预置区域并查看站点。

- [ ] **Step 6: 提交**

```bash
git add charge_platform_project/shared/domain/geo.* charge_platform_project/server/services/station_service.* charge_platform_project/clients/user charge_platform_project/tests/unit
git commit -m "feat: list nearby stations with offline fallback"
```

### Task 8：订单状态机、预约与充电会话

**Owner:** 成员 1；成员 2 集成界面。
**Files:**
- Create: `charge_platform_project/shared/domain/order_state.*`
- Create: `charge_platform_project/server/services/order_service.*`
- Create: `charge_platform_project/server/repositories/order_repository.*`
- Create: `charge_platform_project/clients/user/pages/charging_page.*`
- Create: `charge_platform_project/clients/user/pages/reservation_page.*`
- Create: `charge_platform_project/clients/user/pages/active_order_page.*`
- Create: `charge_platform_project/tests/unit/order_state_test.cpp`
- Create: `charge_platform_project/tests/integration/reservation_race_test.cpp`

- [ ] **Step 1: 写合法与非法状态迁移测试**

```cpp
void OrderStateTest::allowsOnlyDeclaredTransitions() {
    QVERIFY(canTransition(OrderState::Reserved, OrderState::Charging));
    QVERIFY(canTransition(OrderState::Charging, OrderState::PendingPayment));
    QVERIFY(canTransition(OrderState::PendingPayment, OrderState::Finished));
    QVERIFY(!canTransition(OrderState::Reserved, OrderState::Finished));
    QVERIFY(!canTransition(OrderState::Finished, OrderState::Charging));
}
```

- [ ] **Step 2: 写两个用户争抢同一桩测试并确认失败**

并行发送两个预约请求；断言只有一个返回成功，数据库只有一个有效预约。

- [ ] **Step 3: 实现状态机和预约事务**

在事务内检查用户未完成订单、电桩状态和有效预约；创建 `RESERVED` 订单并写入 `reserved_until`；更新桩状态为 `RESERVED`。

- [ ] **Step 4: 实现预约过期扫描**

服务端每 10 秒查询 `reserved_until <= 当前时间` 的 `RESERVED` 订单，在同一事务内转为 `EXPIRED` 并释放桩位；测试使用可注入时钟，避免等待真实时间。

- [ ] **Step 5: 实现启动、遥测更新与停止**

只有有效 `RESERVED` 可启动；桩遥测的累计电量只允许递增；停止时保存最后有效电量、计算金额并转为 `PENDING_PAYMENT`。

- [ ] **Step 6: 实现车主端恢复页面**

预约确认页依次显示站点、桩、功率、单价、有效期和取消规则；并发失败时显示“查看其他空闲桩”。充电页以功率、电量、时长和预估费用为主，显示桩号、订单号、连接状态和更新时间；停止充电使用琥珀色边框按钮并二次确认。登录后首先查询活动订单；`RESERVED`、`CHARGING` 或 `PENDING_PAYMENT` 分别进入预约、充电或结算页面，禁止新建第二笔订单。断线时保留最后数值并标记可能过期，重连后以服务端权威状态覆盖。

- [ ] **Step 7: 运行测试并提交**

Run: `ctest --test-dir charge_platform_project/build -R "order_state_test|reservation_race_test" --output-on-failure`
Expected: PASS。

```bash
git add charge_platform_project/shared/domain/order_state.* charge_platform_project/server/services/order_service.* charge_platform_project/server/repositories/order_repository.* charge_platform_project/clients/user/pages charge_platform_project/tests
git commit -m "feat: enforce reservation and charging state machine"
```

### Task 9：计费、充值、支付和请求去重

**Owner:** 成员 1；成员 2 集成结算界面。
**Files:**
- Create: `charge_platform_project/shared/domain/money.*`
- Create: `charge_platform_project/server/services/wallet_service.*`
- Create: `charge_platform_project/server/repositories/wallet_repository.*`
- Create: `charge_platform_project/clients/user/pages/settlement_page.*`
- Create: `charge_platform_project/clients/user/pages/profile_page.*`
- Create: `charge_platform_project/clients/user/pages/charging_history_page.*`
- Create: `charge_platform_project/tests/integration/payment_idempotency_test.cpp`

- [ ] **Step 1: 写金额和重复支付失败测试**

```cpp
void PaymentTest::sameRequestChargesOnce() {
    const auto first = service.pay(orderId, "REQ-PAY-1");
    const auto second = service.pay(orderId, "REQ-PAY-1");
    QVERIFY(first.ok && second.ok);
    QCOMPARE(repo.balanceCents(userId), 8800);
    QCOMPARE(repo.countTransactions("REQ-PAY-1"), 1);
}
```

- [ ] **Step 2: 确认失败并实现整数计费**

金额计算：`amount_cents = round(energy_wh * price_cents_per_kwh / 1000.0)`；订单停止后固化单价和金额，支付重试不得重新计算。

- [ ] **Step 3: 实现支付事务**

同一事务内完成：插入去重记录、检查订单状态、检查余额、扣款、写 `PAYMENT` 流水、订单转 `FINISHED`、桩转 `IDLE`；任一步失败全部回滚。

- [ ] **Step 4: 实现模拟充值和资料保存**

充值金额限制 1～10000 元；头像只接受 JPG/PNG 且不超过 2 MB；昵称长度 2～20；相同充值请求编号只写一次流水。

- [ ] **Step 5: 验证余额不足与重复请求**

Run: `ctest --test-dir charge_platform_project/build -R payment_idempotency_test --output-on-failure`
Expected: PASS；余额不足返回 `BALANCE_INSUFFICIENT` 且订单保持 `PENDING_PAYMENT`。人工检查结算页逐项显示电量、时长、单价、服务费、应付金额和余额；账户页显示脱敏手机号、账户状态与模拟充值；充电记录页支持状态/时间筛选、详情和状态时间线，并验证空记录和查询失败反馈。

- [ ] **Step 6: 提交**

```bash
git add charge_platform_project/shared/domain/money.* charge_platform_project/server/services/wallet_service.* charge_platform_project/server/repositories/wallet_repository.* charge_platform_project/clients/user charge_platform_project/tests/integration/payment_idempotency_test.cpp
git commit -m "feat: settle orders with idempotent wallet transactions"
```

### Task 10：运营端管理、分析导出与第一阶段验收

**Owner:** 成员 2负责运营端；成员 1提供服务接口并主持验收；成员 3验证分析契约；三人交叉复跑。
**Files:**
- Create: `charge_platform_project/server/services/admin_service.*`
- Create: `charge_platform_project/clients/admin/pages/overview_page.*`
- Create: `charge_platform_project/clients/admin/pages/login_page.*`
- Create: `charge_platform_project/clients/admin/pages/revenue_page.*`
- Create: `charge_platform_project/clients/admin/widgets/revenue_chart.*`
- Create: `charge_platform_project/clients/admin/pages/pile_page.*`
- Create: `charge_platform_project/clients/admin/pages/station_page.*`
- Create: `charge_platform_project/clients/admin/pages/order_page.*`
- Create: `charge_platform_project/clients/admin/pages/user_page.*`
- Create: `charge_platform_project/clients/admin/pages/operation_log_page.*`
- Create: `charge_platform_project/clients/admin/adapters/admin_data_provider.*`
- Create: `charge_platform_project/tests/integration/admin_service_test.cpp`
- Create: `charge_platform_project/tests/e2e/basic_charge_flow.sh`
- Create: `charge_platform_project/scripts/export-analysis-sample.sh`
- Create: `charge_platform_project/docs/test-results/stage-1-acceptance.md`

- [ ] **Step 1: 写营收、冻结和新增站点测试**

测试数据包含已完成、待支付和取消订单；断言营收只汇总已完成订单。冻结操作必须写入原因和管理员日志。新增站点和初始电桩数量必须一致。

- [ ] **Step 2: 确认失败并实现管理员登录**

默认账号 `admin/123456`；部署脚本生成 16 字节随机盐，并用 `QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, password, salt, 100000, 32)` 写入摘要；客户端登录失败统一显示“账号或密码错误”。测试断言数据库中不存在明文 `123456`。

- [ ] **Step 3: 实现营收折线图**

`revenue_chart` 使用 `QLineSeries` 和 `QChartView`；下拉框切换近 7 日/30 日；缺失日期补 0；悬停显示日期和金额；测试验证输入 7/30 个有序点时系列点数、顺序和金额一致。

- [ ] **Step 4: 实现概览、设备、站点和用户页面**

运营端使用深海蓝侧栏、浅灰工作区和白色数据卡。概览显示今日营收、服务车次、利用率、在线率、待处理告警、趋势和快捷处置；营收页的图表与明细共用时间和站点筛选；站点、设备、订单、用户和日志表格均有加载、空数据、失败、排序、筛选和分页状态。手机号显示为 `138****8000`；冻结/解冻要求原因与二次确认；远程重启显示桩号和站点，只有桩回复后才显示成功。`admin_data_provider` 统一提供模拟和正式视图模型。

- [ ] **Step 5: 编写基础闭环脚本**

`basic_charge_flow.sh` 依次：初始化数据库、启动服务端和 `PILE-001` 模拟器、登录用户、预约、启动、注入 10 kWh、停止、支付、查询运营端指标；脚本使用 `set -euo pipefail`，任一步失败都以非 0 退出。

- [ ] **Step 6: 导出阶段二兼容的模拟分析样例**

`export-analysis-sample.sh` 只读业务库，按 `charge_event_v1.schema.json` 输出 `runtime/analytics/staging/stage1-sample.csv`；同时输出记录数、覆盖起止时间、`SIMULATED` 模式和契约版本。脚本不得启动分析服务，也不得修改业务表。

- [ ] **Step 7: 运行第一阶段独立验收**

Run: `ctest --test-dir charge_platform_project/build --output-on-failure`
Expected: 全部 PASS。
Run: `bash charge_platform_project/tests/e2e/basic_charge_flow.sh`
Expected: 输出 `ORDER=FINISHED`、`PILE=IDLE`、`PAYMENT_COUNT=1`、`REVENUE_DELTA=1200`。
Run: `bash charge_platform_project/scripts/export-analysis-sample.sh`
Expected: 输出 `CONTRACT=charge_event_v1`、`DATA_MODE=SIMULATED`、`INVALID_ROWS=0`，且基础系统全程未启动 analytics/dashboard。

- [ ] **Step 8: 保存第一阶段证据并提交**

`stage-1-acceptance.md` 记录基础闭环、并发预约、重复支付、数据导出、未启动分析服务的证据和已知限制，同时附车主端11类页面、运营端9类页面和浅色Web大屏的导航录屏、状态截图、分辨率检查与关闭动效结果。阻断/严重缺陷必须为0；通过后标记 `v0.1.0-stage1`，未通过时停止第二阶段数据导入。

```bash
git add charge_platform_project/server/services/admin_service.* charge_platform_project/clients/admin charge_platform_project/tests charge_platform_project/scripts/export-analysis-sample.sh charge_platform_project/docs/test-results/stage-1-acceptance.md
git commit -m "feat: complete stage one business and data contract"
git tag -a v0.1.0-stage1 -m "stage one basic system acceptance"
```

## 技术工作包 C：引入真实数据并完成分析与异常闭环

### Task 11：真实数据准入与 raw 导入，并行完成异常注入

**Owner:** 成员 1负责遥测服务；成员 3负责桩模拟器、来源准入和 raw 导入；成员 2核对故障状态展示。
**Files:**
- Create: `charge_platform_project/simulator/pile/scenario.*`
- Create: `charge_platform_project/server/services/telemetry_service.*`
- Create: `charge_platform_project/server/repositories/telemetry_repository.*`
- Create: `charge_platform_project/tests/unit/telemetry_service_test.cpp`
- Create: `charge_platform_project/analytics/src/source_registry.py`
- Create: `charge_platform_project/analytics/src/ingest_raw.py`
- Create: `charge_platform_project/analytics/pyproject.toml`
- Create: `charge_platform_project/analytics/tests/test_source_registry.py`
- Create: `charge_platform_project/analytics/tests/test_ingest_raw.py`
- Create: `charge_platform_project/analytics/manifests/source.example.json`
- Create: `charge_platform_project/docs/data-source-register.md`

- [ ] **Step 1: 写遥测和数据准入失败测试**

测试要求：累计电量倒退的遥测被拒绝；温度达到配置阈值创建高温告警；连续 15 秒无心跳把桩标记为 `OFFLINE`。

数据准入测试要求：缺少来源名称、允许用途、数据模式、版本或覆盖时间时拒绝导入；`REAL_ANONYMIZED` 数据若仍含手机号、姓名、支付账号、车辆标识或连续轨迹标识则失败；`SIMULATED` 数据不得登记为真实数据。

- [ ] **Step 2: 实现固定故障场景和来源登记**

桩模拟器支持 `--scenario normal|overheat|timeout|command-failure`；场景数据固定，便于每次演示得到相同结果。

`source_registry.py` 保存提供者或公开地址、允许/禁止用途、取得日期、数据模式、版本、覆盖时间、是否允许分发和保留期限。9月11日结束前拿不到来源明确且允许教学分析的数据时，停止该数据集路线并选择公开匿名或聚合数据。

`pyproject.toml` 在 `[project.optional-dependencies]` 中声明测试和数据处理依赖；先执行 `python3 -m venv .venv && .venv/bin/pip install -e '.[test]'`。来源清单使用 JSON，避免为读取清单额外依赖 YAML 解析库。

- [ ] **Step 3: 实现遥测校验、离线扫描和 raw 只读导入**

只接受已注册桩、合理时间戳和非递减累计电量；离线扫描每 5 秒执行一次，不在每个客户端线程重复运行。

`ingest_raw.py` 把获准输入复制到 `runtime/analytics/raw/<ingest_batch>/` 并生成批次清单；不修改源文件，不把原始数据提交 Git，不允许客户端或分析脚本从 raw 目录直接展示指标。

- [ ] **Step 4: 验证并提交**

Run: `ctest --test-dir charge_platform_project/build -R telemetry_service_test --output-on-failure`
Expected: PASS，三类异常均生成可查询告警。
Run: `cd charge_platform_project/analytics && .venv/bin/python -m pytest -q tests/test_source_registry.py tests/test_ingest_raw.py`
Expected: PASS；输出批次目录和来源清单，非法来源、隐私字段及错误数据模式均被拒绝。

```bash
git add charge_platform_project/simulator/pile charge_platform_project/server/services/telemetry_service.* charge_platform_project/server/repositories/telemetry_repository.* charge_platform_project/tests/unit/telemetry_service_test.cpp charge_platform_project/analytics charge_platform_project/docs/data-source-register.md
git commit -m "feat: admit traceable data and detect pile faults"
```

### Task 12：staging 清洗质量门禁与告警工单闭环

**Owner:** 成员 1负责告警服务；成员 2负责运营端工单；成员 3负责清洗映射；成员 1独立复跑成员 3的数据质量结果。
**Files:**
- Create: `charge_platform_project/server/services/alarm_service.*`
- Create: `charge_platform_project/server/repositories/alarm_repository.*`
- Create: `charge_platform_project/clients/admin/pages/ticket_page.*`
- Create: `charge_platform_project/tests/integration/alarm_ticket_test.cpp`
- Create: `charge_platform_project/analytics/src/normalize.py`
- Create: `charge_platform_project/analytics/src/station_mapping.py`
- Create: `charge_platform_project/analytics/src/quality.py`
- Create: `charge_platform_project/analytics/tests/test_normalize.py`
- Create: `charge_platform_project/analytics/tests/test_quality.py`
- Create: `charge_platform_project/docs/test-results/data-quality.md`

- [ ] **Step 1: 写告警工单和数据质量失败测试**

同一桩同类未恢复异常只保留一个活动告警。告警状态只允许 `OPEN → ACKNOWLEDGED → CLOSED`；工单状态只允许 `OPEN → PROCESSING → RESOLVED → CLOSED`。设备未恢复、尚未进入 `RESOLVED` 或处理说明为空时禁止关闭。

质量测试要求：无法解析的时间、未知时区、负电量、负时长、重复来源记录、未知必填状态和无法映射的站点进入 rejected 清单；程序不得静默补值或按站名自动猜测站点。

- [ ] **Step 2: 实现告警事务与 staging 规范化**

创建告警与工单必须在同一事务；工单记录处理人、确认时间、处理说明、恢复时间和关闭时间。

`normalize.py` 按 `charge_event_v2` 显式转换时间、时区、kWh/元/秒和状态；`energy_kwh`、`amount_yuan` 必须规范化为两位小数字符串，禁止用二进制浮点处理金额。`station_mapping.py` 使用经人工确认的来源编号映射表；输出 `runtime/analytics/staging/<batch>/` 和 `rejected/<batch>/`，原始层保持不变。

- [ ] **Step 3: 实现远程重启反馈和质量门禁**

告警工单页采用“告警列表＋工单详情”双栏结构：左侧按严重度、状态、站点和时间筛选，右侧显示设备摘要、告警时间线、处置记录、重启结果和关闭说明；未选择告警时显示操作引导。运营端发送 `REBOOT_PILE` 前显示对象和影响的二次确认；模拟器成功后，服务端先查询活动订单，再把桩恢复为 `RESERVED`、`CHARGING` 或 `IDLE`，不得无条件恢复 `IDLE`。失败或超时保留告警并把结果写入工单日志。严重度用颜色、图标和文字共同表达。

`quality.py` 统计输入、通过、拒绝、重复、缺失和未映射数量，并检查数据模式、覆盖时间和单位。必填字段错误或来源不合规时阻止进入 curated 层；可接受阈值写入数据集清单，不在代码中为所有数据集硬编码同一比例。

- [ ] **Step 4: 验证三类异常闭环**

Run: `ctest --test-dir charge_platform_project/build -R alarm_ticket_test --output-on-failure`
Expected: PASS。人工依次演示超温、心跳超时、重启失败，均能查看完整时间线。
Run: `cd charge_platform_project/analytics && .venv/bin/python -m pytest -q tests/test_normalize.py tests/test_quality.py`
Expected: PASS；同一批次重复运行得到相同通过/拒绝数量，原始层文件未改变。

- [ ] **Step 5: 提交**

```bash
git add charge_platform_project/server/services/alarm_service.* charge_platform_project/server/repositories/alarm_repository.* charge_platform_project/clients/admin/pages/ticket_page.* charge_platform_project/tests/integration/alarm_ticket_test.cpp charge_platform_project/analytics charge_platform_project/docs/test-results/data-quality.md
git commit -m "feat: clean traceable data and close alarm tickets"
```

### Task 13：构建 curated 分析层和真实数据负荷基线

**Owner:** 成员 3；成员 1独立复跑时间切分、隔离和评测。
**Files:**
- Modify: `charge_platform_project/analytics/pyproject.toml`
- Create: `charge_platform_project/analytics/src/dataset.py`
- Create: `charge_platform_project/analytics/src/build_curated.py`
- Create: `charge_platform_project/analytics/src/train.py`
- Create: `charge_platform_project/analytics/src/model.py`
- Create: `charge_platform_project/analytics/tests/test_dataset.py`
- Create: `charge_platform_project/analytics/tests/test_model.py`
- Create: `charge_platform_project/docs/data-lineage.md`

- [ ] **Step 1: 写聚合、来源隔离、时间切分和无泄漏测试**

```python
def test_time_split_never_uses_future_rows():
    train, test = time_split(frame, ratio=0.8)
    assert train.index.max() < test.index.min()
```

另写测试断言：小时聚合的订单数、电量和空闲桩可从 staging 输入回算；`SIMULATED` 与 `REAL_ANONYMIZED/PUBLIC_AGGREGATED` 不会在未显式选择时混入同一训练集；任何特征的时间不得晚于预测起点。

- [ ] **Step 2: 确认测试失败**

Run: `cd charge_platform_project/analytics && .venv/bin/python -m pytest -q`
Expected: FAIL，提示 `time_split` 未定义。

- [ ] **Step 3: 实现可追溯 curated 数据准备**

`build_curated.py` 从通过质量门禁的 staging 批次按站点和小时聚合订单数、充电量、平均时长和空闲桩；补充小时、星期和节假日特征。天气只有在来源、时间和站点可追溯时才加入，否则不生成模拟天气冒充真实特征。输出 `runtime/analytics/curated/<dataset_version>/hourly_station.csv`、行数、覆盖时间、数据模式、输入批次和拒绝数量。

在 `pyproject.toml` 中加入 pandas 和 scikit-learn 后重新执行 `.venv/bin/pip install -e '.[test]'`，依赖版本由项目文件统一记录，不在个人环境中临时安装未记录库。

`docs/data-lineage.md` 固定链条：`来源/许可 → raw 批次 → 清洗映射 → rejected → curated → 特征目标 → 时间切分 → 基线/模型 → API → 大屏`。

- [ ] **Step 4: 实现朴素基线和树模型**

```python
baseline = frame.groupby("station_id")["load_wh"].shift(24).loc[test.index]
model = HistGradientBoostingRegressor(max_depth=6, learning_rate=0.08, random_state=42)
```

主目标固定为站点未来 1/6/24 小时负荷，同一目标使用昨日同小时作为朴素基线。按时间切分训练/测试，输出 MAE、RMSE、样本量、数据模式和与基线的差值；模型未优于基线时发布基线。模拟与真实/公开数据分别评测，不把混合结果包装为真实数据指标。

- [ ] **Step 5: 验证可复现性**

Run: `cd charge_platform_project/analytics && .venv/bin/python -m pytest -q && .venv/bin/python -m analytics.src.train`
Expected: 测试 PASS；连续运行两次产生相同聚合行数、评测数字和模型选择；输出能反查输入批次。

- [ ] **Step 6: 提交**

```bash
git add charge_platform_project/analytics charge_platform_project/docs/data-lineage.md
git commit -m "feat: build traceable real-data load baseline"
```

### Task 14：预测服务与可解释推荐

**Owner:** 成员 3负责分析接口；成员 1负责服务端降级；成员 2负责两端来源提示。
**Files:**
- Create: `charge_platform_project/analytics/src/api.py`
- Create: `charge_platform_project/analytics/src/recommend.py`
- Create: `charge_platform_project/analytics/tests/test_api.py`
- Create: `charge_platform_project/analytics/tests/test_recommend.py`
- Create: `charge_platform_project/server/services/recommendation_service.*`

- [ ] **Step 1: 写固定输入推荐排序测试**

```python
def test_recommendation_explains_top_station():
    ranked = rank_stations(stations, weights={"distance": .3, "wait": .3, "price": .2, "load": .2})
    assert ranked[0].station_id == 2
    assert "预计等待短" in ranked[0].reasons
```

- [ ] **Step 2: 实现归一化和缺失数据降级**

所有分数归一化到 `[0,1]`；预测缺失时按距离 40%、当前等待 35%、价格 25% 重新分配权重；同分按距离、站点 ID 排序。

- [ ] **Step 3: 实现带数据来源的 `/health`、`/forecast`、`/recommend`**

响应必须包含 `modelMode`（`trained` 或 `baseline`）、`dataMode`、数据集名称和版本、覆盖时间、更新时间、样本量、预测范围和推荐理由；不返回原始文件路径或个人标识。模型文件缺失时服务仍启动并返回基线，真实数据缺失时明确返回降级来源而不是伪造真实结果。

- [ ] **Step 4: 服务端调用设置超时与降级**

分析服务 800 ms 未响应时返回本地基础推荐，并携带 `MODEL_UNAVAILABLE` 降级标记，不阻断站点列表。

- [ ] **Step 5: 运行测试并提交**

Run: `cd charge_platform_project/analytics && .venv/bin/python -m pytest -q`
Expected: PASS。
Run: `curl -s http://127.0.0.1:48100/health`
Expected: JSON 包含 `"status":"ok"`、`modelMode`、`dataMode`、`datasetVersion` 和覆盖时间。

```bash
git add charge_platform_project/analytics charge_platform_project/server/services/recommendation_service.*
git commit -m "feat: serve explainable station recommendations"
```

### Task 15：Web 大屏和多端联动验收

**Owner:** 成员 3负责大屏；成员 1提供只读接口并总集成；成员 2核对两个 Qt 端。
**Files:**
- Create: `charge_platform_project/dashboard/package.json`
- Create: `charge_platform_project/dashboard/src/App.vue`
- Create: `charge_platform_project/dashboard/src/styles/tokens.css`
- Create: `charge_platform_project/dashboard/src/styles/motion.css`
- Create: `charge_platform_project/dashboard/src/adapters/dashboardDataAdapter.ts`
- Create: `charge_platform_project/dashboard/src/components/common/DataSourceBar.vue`
- Create: `charge_platform_project/dashboard/src/components/common/PageState.vue`
- Create: `charge_platform_project/dashboard/src/components/MetricStrip.vue`
- Create: `charge_platform_project/dashboard/src/components/LoadChart.vue`
- Create: `charge_platform_project/dashboard/src/components/StationRanking.vue`
- Create: `charge_platform_project/dashboard/src/components/AlarmFeed.vue`
- Create: `charge_platform_project/dashboard/src/components/RecommendationPanel.vue`
- Create: `charge_platform_project/dashboard/src/components/DataQualityPanel.vue`
- Create: `charge_platform_project/dashboard/tests/dashboard.test.ts`
- Create: `charge_platform_project/tests/e2e/cross_client_flow.sh`

- [ ] **Step 1: 写大屏数据状态与来源标识测试**

测试加载中、真实/公开数据、模拟数据、空数据、接口失败和模型失败六种状态；断言页面始终显示数据模式、数据集版本、覆盖时间和更新时间，接口失败时显示最近成功数据或重试提示而不是空白屏，模型失败时实际数据和基线仍可展示。另在1366×768和1920×1080视口检查核心指标不被横向裁切，并断言 `prefers-reduced-motion` 时动画关闭。

- [ ] **Step 2: 确认测试失败**

Run: `cd charge_platform_project/dashboard && npm test -- --run`
Expected: FAIL，组件不存在。

- [ ] **Step 3: 实现浅色商业大屏和全部区域**

页面使用浅灰或极浅青灰背景、白色卡片、深海蓝文字，以及绿、蓝、紫、青、珊瑚红辅助色，不实现默认深色主题。首屏为累计电量、服务车次、营收、在线率、未处理告警、实际/预测负荷、利用率排行和告警流；第二屏为推荐分流、数据质量与来源、设备分布、站点对比和模型/基线说明。页首固定显示 `SIMULATED/REAL_ANONYMIZED/PUBLIC_AGGREGATED` 标签、数据集版本、覆盖时间和更新时间；地图资源失败时自动使用站点排行替代。

卡片进入、数字变化和图表绘制只在首次加载或数据变化时播放，时长按设计规格控制；在线圆点可轻缓呼吸，告警禁止闪烁。所有效果均由 `motion.css` 控制并支持关闭，不得成为数据加载或点击的前置条件。

- [ ] **Step 4: 编写多端联动脚本**

脚本启动所有服务，选择一个获准分析批次，创建并完成一笔模拟订单，然后注入超温；断言车主端订单完成、运营端营收增加、桩状态变化、大屏指标更新、告警和工单出现，并断言业务实时数据与历史分析数据的来源标签没有混淆。

- [ ] **Step 5: 运行第 3 周验收**

Run: `cd charge_platform_project/dashboard && npm test -- --run`
Expected: PASS；1366×768和1920×1080下关键区域可读，浅色卡片边界清晰，关闭动效后数据和操作不变。
Run: `bash charge_platform_project/tests/e2e/cross_client_flow.sh`
Expected: 输出 `CROSS_CLIENT_FLOW=PASS`、`ALARM_CREATED=1`、`TICKET_CREATED=1`、`DATA_LINEAGE=PASS`。

- [ ] **Step 6: 提交**

```bash
git add charge_platform_project/dashboard charge_platform_project/tests/e2e/cross_client_flow.sh charge_platform_project/server
git commit -m "feat: visualize live operations and alarms"
```

## 技术工作包 D：数据规模、系统回归、交付与答辩

### Task 16：重连、事务恢复和分析服务故障隔离

**Owner:** 成员 1；成员 2复核两个 Qt 客户端；成员 3验证分析降级。
**Files:**
- Modify: `charge_platform_project/clients/user/service_client.*`
- Modify: `charge_platform_project/clients/admin/service_client.*`
- Modify: `charge_platform_project/server/network/request_router.*`
- Create: `charge_platform_project/tests/integration/recovery_test.cpp`
- Create: `charge_platform_project/analytics/tests/test_service_isolation.py`

- [ ] **Step 1: 写断线、重连、重放和事务回滚测试**

测试在 `CHARGING` 时断开车主端，重新连接后恢复同一订单；支付响应丢失后用原请求编号重试，不重复扣费；模拟数据库写失败时订单、余额和桩状态全部保持原值。另测试删除模型、撤下 curated 数据或停止分析服务时，登录、找站、预约、充电和支付仍可完成，界面只显示明确降级提示。

- [ ] **Step 2: 实现指数退避重连**

重连间隔依次为 1、2、4、8 秒，最大 8 秒；连接成功后立即查询活动订单、相关桩和未读告警；窗口显示“重连中”而不是冻结。

- [ ] **Step 3: 实现请求结果重放**

关键动作先查 `request_dedup`；已完成请求直接返回原结果摘要；正在处理的相同请求返回 `DUPLICATE_REQUEST` 并提示稍后查询。

- [ ] **Step 4: 验证恢复测试**

Run: `ctest --test-dir charge_platform_project/build -R recovery_test --output-on-failure`
Expected: PASS，三类故障后数据保持一致。
Run: `cd charge_platform_project/analytics && .venv/bin/python -m pytest -q tests/test_service_isolation.py`
Expected: PASS；分析组件故障不会写入或阻断业务状态。

- [ ] **Step 5: 提交**

```bash
git add charge_platform_project/clients charge_platform_project/server/network charge_platform_project/tests/integration/recovery_test.cpp charge_platform_project/analytics/tests/test_service_isolation.py
git commit -m "fix: isolate analytics and recover authoritative state"
```

### Task 17：20 个模拟桩稳定性和真实数据处理性能

**Owner:** 成员 1处理服务性能；成员 3运行模拟负载并验证数据管线；成员 2观察界面反馈。
**Files:**
- Create: `charge_platform_project/tests/load/run_20_piles.py`
- Create: `charge_platform_project/tests/load/assert_database.py`
- Create: `charge_platform_project/docs/test-results/load-test.md`
- Create: `charge_platform_project/analytics/tests/benchmark_pipeline.py`
- Create: `charge_platform_project/docs/test-results/data-pipeline-performance.md`

- [ ] **Step 1: 编写固定负载脚本**

脚本创建 20 个模拟桩连接，每 5 秒心跳、每 2 秒遥测，持续 10 分钟；第 4 分钟断开 3 个桩，第 5 分钟恢复；每分钟创建并完成 2 笔订单。

- [ ] **Step 2: 先运行 1 分钟预检**

Run: `python3 charge_platform_project/tests/load/run_20_piles.py --duration 60`
Expected: 20 个连接建立，脚本退出码 0，无协议解析错误。

- [ ] **Step 3: 添加必要索引并定义数据规模路线**

为订单状态/时间、遥测桩号/时间、告警状态/时间建立索引；大屏列表查询必须带时间范围和数量上限；不引入 Redis 或缓存层。

`benchmark_pipeline.py` 记录真实/公开批次的行数、文件大小、raw→staging→curated 耗时、峰值内存和拒绝数量。小于 100 万行优先 pandas 分块处理；达到 100 万行或本机内存不足时可换 DuckDB 扫描 CSV/Parquet；只有教师明确要求分布式框架或数据规模足以支撑时才引入 Spark。报告按实测规模使用“真实数据分析”或“大数据处理”表述。

- [ ] **Step 4: 运行正式稳定性测试**

Run: `python3 charge_platform_project/tests/load/run_20_piles.py --duration 600 && python3 charge_platform_project/tests/load/assert_database.py`
Expected: 服务端不崩溃；已确认订单无丢失；活动订单与桩占用一致；3 个断开桩产生离线告警并在恢复后更新状态。
Run: `cd charge_platform_project/analytics && .venv/bin/python tests/benchmark_pipeline.py --manifest manifests/accepted.json`
Expected: 输出行数、耗时、内存、拒绝数和数据模式；连续两次聚合行数一致。20 桩结果必须标为“软件模拟负载”，不得称为真实设备测试。

- [ ] **Step 5: 记录机器配置、峰值连接、错误数和限制并提交**

```bash
git add charge_platform_project/tests/load charge_platform_project/analytics/tests/benchmark_pipeline.py charge_platform_project/docs/test-results charge_platform_project/database
git commit -m "test: verify simulated load and real-data pipeline scale"
```

### Task 18：完成全量验收、缺陷修复和需求追踪

**Owner:** PRL 主持，全员参加。
**Files:**
- Create: `charge_platform_project/docs/requirements-traceability.md`
- Create: `charge_platform_project/docs/test-results/acceptance.md`
- Create: `charge_platform_project/docs/defects.md`
- Create: `charge_platform_project/docs/test-results/stage-2-acceptance.md`

- [ ] **Step 1: 建立需求对应表**

每一行包含：验收阶段、说明书要求、设计编号、实现文件、测试名称、执行结果、证据路径。P0 每项必须有实现和测试；P1 每项必须有端到端证据。阶段二数据结论必须能沿 `来源/许可 → raw → staging/rejected → curated → 特征目标 → 切分 → 基线/模型 → API → 大屏` 反查。

- [ ] **Step 2: 运行所有自动测试**

Run: `ctest --test-dir charge_platform_project/build --output-on-failure`
Expected: 全部 PASS。
Run: `cd charge_platform_project/analytics && .venv/bin/python -m pytest -q`
Expected: 全部 PASS。
Run: `cd charge_platform_project/dashboard && npm test -- --run`
Expected: 全部 PASS。

- [ ] **Step 3: 执行 12 个正式验收场景**

逐项执行主方案第 11.2 节；失败项记录复现步骤、期望、实际、责任人和严重程度。阻断/严重缺陷立即修复并重跑相关测试，一般缺陷进入已知限制。

- [ ] **Step 4: 审核模型表述**

只有同一测试集上的模型指标优于朴素基线时使用“模型预测”；否则统一写“基线估计”，不得出现“精准预测”等无证据表述。核对每个图表都显示数据模式、版本和覆盖时间；模拟、真实匿名与公开聚合数据不能共用模糊标签。

- [ ] **Step 5: 执行第二阶段独立验收**

`stage-2-acceptance.md` 记录来源允许用途、原始/清洗/拒绝/分析行数、字段和单位映射、训练/测试时间范围、基线与模型指标、大屏回算结果、分析故障降级和第一阶段回归。成员 1必须独立复跑成员 3的数据质量与模型评测命令，成员 2复核页面上的来源和降级说明。

- [ ] **Step 6: 提交验收证据**

```bash
git add charge_platform_project/docs
git commit -m "docs: record requirement coverage and acceptance results"
```

### Task 19：制作可重复启动与发布包

**Owner:** 成员 1负责公共启停和发布；成员 2、3分别打包界面与分析内容。
**Files:**
- Create: `charge_platform_project/scripts/init-demo.sh`
- Create: `charge_platform_project/scripts/start-all.sh`
- Create: `charge_platform_project/scripts/stop-all.sh`
- Create: `charge_platform_project/scripts/health-check.sh`
- Create: `charge_platform_project/scripts/prepare-analytics-data.sh`
- Create: `charge_platform_project/docs/deployment.md`
- Create: `charge_platform_project/docs/user-guide.md`

- [ ] **Step 1: 编写初始化脚本**

脚本检查 Qt、CMake、Python、Node 和 SQLite；创建运行目录；复制 `app.example.ini` 为本地配置；执行迁移和演示数据；不得覆盖已有正式数据库，除非显式传入 `--reset-demo-data`。`prepare-analytics-data.sh --manifest <file>` 依次执行来源准入、raw 导入、staging 清洗、质量门禁和 curated 聚合，任何一步失败都停止且不启动模型服务。

- [ ] **Step 2: 编写启动、健康检查和停止脚本**

`start-all.sh --stage 1` 只启动数据库、C++ 服务端和桩模拟器；`start-all.sh --stage 2 --manifest <file>` 在数据门禁通过后再启动分析服务和大屏。Qt 客户端由用户按需启动。失败时停止已启动进程并输出失败组件。

`start-all.sh` 在启动大屏前必须显式加载 `/home/bit/.nvm/nvm.sh`，因为 SSH 非交互会话不保证自动读取用户的交互式 Shell 配置：

```bash
export NVM_DIR=/home/bit/.nvm
test -s "$NVM_DIR/nvm.sh" || { echo "NVM is not installed" >&2; exit 1; }
. "$NVM_DIR/nvm.sh"
node --version
```

- [ ] **Step 3: 在新目录完成冷启动验证**

Run: `bash charge_platform_project/scripts/init-demo.sh && bash charge_platform_project/scripts/start-all.sh --stage 1 && bash charge_platform_project/scripts/health-check.sh --stage 1`
Expected: 输出 `SERVER=OK`、`PILE_SIMULATOR=OK`，分析服务未启动但基础闭环可运行。
Run: `bash charge_platform_project/scripts/prepare-analytics-data.sh --manifest charge_platform_project/analytics/manifests/accepted.json && bash charge_platform_project/scripts/start-all.sh --stage 2 --manifest charge_platform_project/analytics/manifests/accepted.json && bash charge_platform_project/scripts/health-check.sh --stage 2`
Expected: 额外输出 `DATA_QUALITY=OK`、`ANALYTICS=OK`、`DASHBOARD=OK` 和数据模式/版本。

- [ ] **Step 4: 验证停止和再次启动**

Run: `bash charge_platform_project/scripts/stop-all.sh`
Expected: 所有组件停止，数据库仍保留。再次启动后最近订单可查询。

- [ ] **Step 5: 提交**

```bash
git add charge_platform_project/scripts charge_platform_project/docs/deployment.md charge_platform_project/docs/user-guide.md charge_platform_project/config
git commit -m "chore: package repeatable local demo deployment"
```

### Task 20：答辩演练、最终冻结与交付

**Owner:** TL 主持，全员参加。
**Files:**
- Create: `charge_platform_project/docs/demo-script.md`
- Create: `charge_platform_project/docs/release-checklist.md`
- Create: `charge_platform_project/CHANGELOG.md`

- [ ] **Step 1: 写 6～8 分钟演示脚本**

脚本分成两个清晰段落。第一阶段固定演示用户、站点和桩，展示不依赖分析服务的基础闭环；第二阶段展示来源登记、数据模式、覆盖时间、清洗质量、基线比较、推荐、大屏和异常工单。每一步写出操作者、界面、预期数据变化、讲解重点和地图/数据/模型不可用时的备用动作。

- [ ] **Step 2: 连续演练两遍**

Run: `bash charge_platform_project/scripts/init-demo.sh --reset-demo-data` 后按 `docs/demo-script.md` 操作。
Expected: 两遍均先完成第一阶段注册、导航、预约、模拟充电、支付和营收核对，再完成第二阶段真实匿名/公开数据分析、推荐、大屏、异常告警和工单；关闭分析服务后基础流程仍通过。

- [ ] **Step 3: 完成发布检查**

确认源码、构建说明、迁移、演示数据、配置模板、两阶段测试结果、数据来源/许可、字段映射、质量报告、模型评测、需求对应表、用户指南、实训报告和答辩材料齐全；确认仓库无地图密钥、完整手机号、明文密码、原始真实数据或临时数据库。只有来源许可明确允许分发时，真实数据才可单独进入交付介质。

- [ ] **Step 4: 标记最终版本**

```bash
git add charge_platform_project
git commit -m "release: complete charging platform training project"
git tag -a v1.0.0-training -m "two-stage compressed training release"
```

- [ ] **Step 5: 保存最终证据**

把演练时间、参加人员、两遍结果、已知限制和标签名写入 `docs/release-checklist.md`；阻断/严重缺陷必须为 0。

---

## 2. 两阶段准入与裁剪规则

| 检查点 | 必须满足 | 未满足时的动作 |
| --- | --- | --- |
| 9月4日12:00 | 协议、数据库、三类连接、心跳通过 | 冻结已通过字段；不开发 UI 动画和 AI，先修复基础连接 |
| 9月9日14:00 | 一笔订单完整结算、后台一致、分析契约和模拟导出通过，analytics/dashboard 可关闭 | 停止新增功能；不进入正式数据导入，集中修复基础业务和契约 |
| 9月11日结束前 | 数据来源、允许用途、模式、版本、覆盖时间和隐私边界明确 | 放弃该数据集，改用来源明确的公开匿名/聚合数据 |
| 9月16日12:00 | 大屏来源标签、推荐降级、数据回算、三类告警工单和第一阶段回归通过 | 停止新增功能；保留基线、静态排行和来源说明，集中修复验收问题 |
| 9月17日正式验收 | 两阶段自动测试、数据可追溯链和 12 个验收场景通过 | 不冻结阶段版本，只修阻断/严重缺陷并重新验收 |
| 9月16日全量内验 | 发布候选版的全部测试、冷启动和两遍演练通过 | 不再改变协议、数据库和业务规则，只修阻断/严重缺陷 |
| 9月17日全量验收与最终交付 | 两遍演示成功，交付物齐全，无敏感信息 | 不冻结版本、不上传交付包，回到对应责任任务修复并重新验收 |

## 3. 责任分配汇总

| 成员 | 工作量 | 主任务 | 必须参加的交叉复核 |
| --- | ---: | --- | --- |
| 成员 1 / TL | 40% | 服务端、协议、业务库、订单支付、隔离、集成和发布 | 共享契约、数据只读出口、恢复与发布脚本 |
| 成员 2 / Qt 两端 | 30% | 车主端、运营端、共用连接层、界面回归和用户指南 | 来源提示、降级、订单恢复、营收与工单口径 |
| 成员 3 / 数据与模拟 | 30% | 桩模拟器、raw/staging/curated、模型、FastAPI 和大屏 | 来源准入、时间切分、基线比较、数据血缘和大屏降级 |

总体比例按4:3:3控制。9月9～10日和9月16～17日，成员1可短时承担约50%的集成工作，但成员2、3仍要完成各自模块修复、测试和交付说明。三人版本只保留一个主预测目标、一个昨日同小时基线和一个树模型；P2、深度学习和WebSocket强实时不进入本轮计划。前端允许实现设计规格中短时、可关闭的颜色渐变与动效，但9月15日后停止视觉微调。

## 4. 最终验收命令总表

```bash
cmake -S charge_platform_project -B charge_platform_project/build -G Ninja -DBUILD_TESTING=ON
cmake --build charge_platform_project/build
ctest --test-dir charge_platform_project/build --output-on-failure
cd charge_platform_project/analytics && .venv/bin/python -m pytest -q
.venv/bin/python tests/benchmark_pipeline.py --manifest manifests/accepted.json
cd ../dashboard && npm test -- --run
cd .. && bash tests/e2e/basic_charge_flow.sh
bash tests/e2e/cross_client_flow.sh
python3 tests/load/run_20_piles.py --duration 600
python3 tests/load/assert_database.py
bash scripts/health-check.sh
```

预期最终结果：第一阶段在 analytics/dashboard 关闭时独立通过；C++、Python、Web 测试全部通过；两个端到端流程输出 `PASS`；20 个软件模拟桩无崩溃且数据库一致；真实/公开数据管线输出来源、行数、耗时和拒绝数；大屏指标可回算；健康检查四个组件均为 `OK`；发布检查中阻断和严重缺陷为 0。
