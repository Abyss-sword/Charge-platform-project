# 成员3数据、模拟器与大屏独立执行手册

> **给执行成员或 Agent：** 按本文从 D1 到 D11 顺序执行。每完成一个复选框就立刻记录实际输出；实现阶段采用“先写失败测试、确认失败、最小实现、相关测试、受影响回归”的顺序。不得越过当日门禁，也不得修改成员1或成员2的文件。

**目标：** 让零上下文的成员3在 2026 年 9 月 3 日至 9 月 17 日内，独立完成其 30% 责任范围，并留下可由非作者复现的证据。

**架构：** 桩模拟器经冻结的 JSON/TCP 协议提供可复现心跳、遥测和异常；分析管线按来源准入、raw、staging/rejected、curated 顺序形成可追溯数据，再以昨日同小时基线和一个树模型提供 FastAPI 服务。Vue/ECharts 浅色大屏只通过唯一适配器读取模拟或正式数据，不反写 SQLite 业务状态。

**技术栈：** C++17、Qt 6.2+、Python 3.10+、pandas、scikit-learn、FastAPI、Vue 3、Vite、ECharts、Vitest、Git。

---

## 0. 教师带做式使用方法

不要把本手册当成一串命令。每天开始时先完成下面的理解检查：

1. 用一句话说明今天的数据从哪里来、经过什么处理、最终由谁使用。
2. 指出今天的输入是否来自已冻结契约；未冻结时只准备样例，不猜测正式字段。
3. 先运行测试观察预期失败，确认失败是“今天的能力尚未实现”，不是环境或旧路径损坏。
4. 按“保留原始输入→显式转换→拒绝坏数据→生成可追溯结果→接入唯一适配器→展示状态”的顺序实现。
5. 每完成一个复选框，立刻记录输入批次、命令、输出数字和证据路径。
6. 当天结束时让成员1复跑数据或服务测试，让成员2操作界面核对文字；不能用自己的截图替代交叉复核。

### 每日成果地图

| 日期 | 你今天只需抓住的核心成果 | 必须停下检查的点 |
| --- | --- | --- |
| 9月3日 | 冻结异常场景、数据契约、候选来源和大屏区域 | 时间、单位、模式、许可是否明确 |
| 9月4日 | 心跳探针、协议坏帧、大屏骨架和六种状态 | 1366×768是否裁切，关闭动效是否仍可用 |
| 9月5～6日 | 准备可重复夹具 | 不引入新功能，不修改冻结协议 |
| 9月7日 | 固定心跳、模拟站点与模拟大屏 | 所有指标是否来自同一数据集 |
| 9月8日 | 递增遥测、竞争输入和模拟告警 | 不在模拟器或大屏修补业务状态 |
| 9月9日 | 四种异常、模拟导出、全大屏和阶段一证据 | 14:00后不新增；统一显示 `SIMULATED` |
| 9月10日 | 阶段一正式演示并冻结契约 | 导出与大屏是否使用同一字段含义 |
| 9月11日 | 来源准入、raw批次和正式适配入口 | 许可或隐私不明的数据必须拒绝 |
| 9月12～13日 | 六类质量样例和预期计数 | 只准备夹具，不训练正式模型 |
| 9月14日 | staging/rejected、质量、curated、基线和60秒预检 | 输入是否等于通过加拒绝，raw是否未改 |
| 9月15日 | 一个负荷目标、三时域、树模型、API和600秒测试 | 时间泄漏、不可复现或写业务库立即停线 |
| 9月16日 | 回算、两尺寸、降级、冷启动和两遍演练 | 12:00后只修阻断或严重缺陷 |
| 9月17日 | 全量验收、冻结和交付 | 发布包不得含无许可raw、隐私或密钥 |

### 数据工作的停线原则

- 来源许可、允许用途或隐私边界不明确：停止导入，改用来源明确的公开聚合数据。
- `输入行数 != 通过行数 + 拒绝行数`：停止进入curated，先修质量统计。
- raw内容或修改时间被处理程序改变：停止后续流程，从只读导入边界修复。
- 训练记录时间晚于预测起点，或训练集时间与测试集交叉：视为数据泄漏，模型结果作废。
- 模型未优于基线：不是系统失败，发布基线并如实保留模型评测。
- 分析服务停止导致预约、充电或支付失败：立即交给成员1修隔离，不在大屏伪造成功。

---

## 1. 先读这一页：30%责任边界

### 1.1 成员3必须完成

成员3只单写以下目录；工作量按整个项目总量约占 30%，不要求每天平均：

- `charge_platform_project/simulator/`：桩心跳、递增遥测、固定异常、命令回复和软件模拟负载配合。
- `charge_platform_project/analytics/`：`charge_event_v1`、来源准入、批次导入、清洗、拒绝、质量、小时聚合、基线、单一树模型、FastAPI 与分析隔离测试。
- `charge_platform_project/dashboard/`：浅色 Web 大屏、唯一数据适配器、统一样式、六种状态、响应式和动效关闭。
- 本模块测试与说明：成员3可以创建技术总计划明确分配给自己的 `analytics/tests/`、`dashboard/tests/`、`docs/data-source-register.md`、`docs/data-lineage.md`、`docs/test-results/data-quality.md`、`docs/test-results/data-pipeline-performance.md`；负载脚本由任务板明确交给成员3运行，若脚本文件归属成员1，则只运行、不直接修改。

成员3每天还必须做两项交叉复核：检查协议中的时间、时区、金额、电量、时长和数据模式；操作两个 Qt 端检查来源文字、分辨率、动效开关和多端口径，但不得直接改 Qt 文件。

### 1.2 成员3禁止直接修改

- `charge_platform_project/server/`、`shared/`、`database/`、根 `CMakeLists.txt` 和公共脚本归成员1。
- `charge_platform_project/clients/` 归成员2。
- 共享协议、数据库迁移、订单状态机、配置模板和发布脚本出现问题时，在任务板登记“文件、字段、原因、影响、复现命令、期望结果”，由对应负责人修改。
- 不新增或删除既定功能；不引入深度学习、WebSocket 强实时、默认深色主题、缓存层或分布式框架。
- SQLite 是订单、余额、设备和工单的唯一业务事实来源；分析代码只能读只读出口，不能写业务表。

### 1.3 两阶段固定边界

| 阶段 | 开发与验收 | 成员3交付边界 |
| --- | --- | --- |
| 第一阶段 | 9月3～9日开发，9月10日正式验收 | 完整模拟器、冻结的 `charge_event_v1` 模拟样例、浅色大屏全部区域与六种状态；统一标记 `SIMULATED`。分析服务和大屏停止时，基础充电闭环仍能独立运行。 |
| 第二阶段 | 9月11～16日开发，9月17日正式验收 | 在第一阶段界面上切换 `REAL_ANONYMIZED` 或 `PUBLIC_AGGREGATED`；完成来源、四层数据、质量、基线、一个树模型、三个接口、回算、性能与降级。不得重做或删除第一阶段页面。 |

固定门禁：9月4日 12:00 冻结协议第一版；9月9日 14:00 停止第一阶段新增；9月11日结束前必须确定合法来源；9月16日 12:00 停止第二阶段新增；9月17日只做最终验收、责任模块修复、复测和交付冻结。

### 1.4 统一日节奏与证据

每天 09:00 检查主线，09:15 确认目标/文件/依赖/验收命令，09:30 建立 `feature/dNN-short-name` 分支，15:30 进入集成，16:30 由非作者复跑，17:15 更新证据，17:30 冻结。成员3把证据交给当日主负责人写入 `charge_platform_project/docs/evidence/day-N.md`，至少包含提交号、命令、通过数、失败项、接口或数据变化、人工检查、回退办法和次日依赖。

命令默认在 Ubuntu 项目环境执行，仓库根路径固定为：

```bash
cd /home/bit/projects/charge_platform_project
```

每个命令块中的 `Expected` 是判定条件，不是可手工补写的输出。命令失败就按当日“失败处理”执行。

### 1.5 首次执行时的一次性环境准备

新虚拟机、全新检出或清理过依赖目录后，先执行本节一次。已有环境不得每天重复安装；安装失败要保留完整输出，不要跳过后继续测试。

```bash
cd /home/bit/projects/charge_platform_project/dashboard
npm install
npm test -- --run

cd /home/bit/projects/charge_platform_project/analytics
python3 -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install -e .
.venv/bin/python -m pytest -q
```

预期：`dashboard/node_modules/` 与 `analytics/.venv/` 生成，Web 和 Python 测试可以启动。二者都是本机依赖，不进入 Git；如 `pip install -e .` 报包结构错误，先修 `analytics/pyproject.toml` 的包声明，不用全局 `sudo pip` 绕过。

---

## 2. 不得漂移的数据和界面规则

### 2.1 `charge_event_v1` 最低字段语义

契约文件固定为 `charge_platform_project/analytics/contracts/charge_event_v1.schema.json`。字段名以成员1冻结的正式契约为准，但必须表达以下含义：来源记录号、来源站点号、内部站点号、事件发生时间、导入时间、明确时区、电量 Wh、时长秒、金额分、订单/事件状态、数据模式、数据集名称、数据集版本和导入批次。第二阶段只能增加可选分析字段，不能改变这些字段原意。

只允许三种数据模式：`SIMULATED`、`REAL_ANONYMIZED`、`PUBLIC_AGGREGATED`。三者不得在未显式选择时混入同一训练集或统一宣称为真实数据。

### 2.2 四层数据职责

| 层 | 准确路径 | 可变性与用途 |
| --- | --- | --- |
| raw | `charge_platform_project/runtime/analytics/raw/<ingest_batch>/` | 保存获准输入副本与批次清单；只读、不人工改写、不进 Git、不直接展示。 |
| staging | `charge_platform_project/runtime/analytics/staging/<batch>/` | 统一字段、时区、Wh、秒、分和状态；只含可解释的规范化结果。 |
| rejected | `charge_platform_project/runtime/analytics/rejected/<batch>/` | 保存重复、必填缺失、未映射、错误单位、非法状态或隐私命中的记录及明确原因。 |
| curated | `charge_platform_project/runtime/analytics/curated/<dataset_version>/` | 仅由质量门禁通过的 staging 生成，按站点和小时聚合，供基线、模型、API 和大屏只读使用。 |

对账恒等式：`输入数 = 通过数 + 拒绝数`；拒绝原因分类总和必须等于拒绝数；curated 每个指标能回算到通过记录和输入批次。

### 2.3 大屏统一视觉、状态和适配

大屏唯一切换点为 `charge_platform_project/dashboard/src/adapters/dashboardDataAdapter.ts`。第一阶段它返回 `SIMULATED` 视图模型；第二阶段仅在此处接入正式接口。组件不得自行伪造回退数据。

固定颜色：深海蓝 `#102F46`、操作蓝 `#3478C5`、充电绿 `#24AD7A`、能源青 `#32B9C8`、活力绿 `#82C75B`、琥珀色 `#EDA14B`、珊瑚红 `#DF6B65`、数据紫 `#756FC5`、页面浅灰 `#F3F7F9`、卡片白 `#FFFFFF`、正文深灰蓝 `#183044`、次要文字灰 `#6B7B88`、边框灰 `#DCE6EC`。

固定渐变：品牌 `#102F46 → #3478C5 → #24AD7A`；实际负荷面积 `rgba(36,173,122,.38) → rgba(50,185,200,.04)`；成功卡 `#EEF9F5 → #FFFFFF`；告警卡 `#FFF4ED → #FFFAFA`。不使用大面积高饱和渐变或默认深色背景。

固定动效：页面 220～350ms；卡片相邻延迟 60～80ms、总计不超过 500ms；数字 300～500ms；图表 400～700ms；按钮 150～200ms；在线点 2 秒轻缓呼吸；状态 180～250ms。`prefers-reduced-motion` 或测试模式关闭全部非必要动效，功能和数据不变。

必须覆盖六种大屏状态：加载中、正常数据、空数据、接口失败、模型失败、地图失败。正常数据内部再显示 `SIMULATED`、`REAL_ANONYMIZED` 或 `PUBLIC_AGGREGATED`；颜色不能作为唯一状态表达，必须同时带图标或文字。1366×768 与 1920×1080 都要检查；首屏必须包含五指标、负荷、排行和告警，允许纵向滚动但不允许核心指标横向裁切。

---

## 3. D1（9月3日）：冻结成员3输入和数据契约草案

**目标：** 四种模拟场景、契约语义、两份候选来源、四层目录和大屏区域都形成可复核文字。

**前置：** 主线能构建；21条需求矩阵、三人目录所有权和两阶段日期可打开。

**输入：** `docs/protocol.md` 草案、需求矩阵、前端设计规格、候选数据来源说明。

**路径：** `simulator/pile/`、`analytics/contracts/charge_event_v1.schema.json`、`analytics/manifests/source.example.json`、`docs/data-source-register.md`、`dashboard/src/`；本日只建立成员3范围内的草案或测试夹具。

**方法：** 场景采用固定编号、固定时间起点和固定数值序列；契约先统一语义与单位；来源按“许可先于下载”准入；大屏先冻结信息结构，不先做视觉扩展。

- [ ] 09:00～09:05：在仓库根运行 `git status --short`、构建和现有测试，记录提交号与失败数。
- [ ] 09:15～09:20：在任务板写明今日唯一目标、成员3允许路径、协议版本和验收命令。
- [ ] 09:30～09:35：建立 `feature/d01-data-contract` 分支，不夹带其他目录改动。
- [ ] 09:35～09:40：列出 `normal`、`overheat`、`timeout`、`command-failure` 的桩号、输入序列、触发点和期望状态。
- [ ] 09:40～09:45：逐字段对照 `charge_event_v1`：来源记录、两类站点号、两个时间、时区、Wh、秒、分、状态、模式、版本、批次。
- [ ] 09:45～09:50：为每个字段写一条有效示例和一条应拒绝示例，禁止使用隐含单位。
- [ ] 09:50～09:55：登记候选来源 A 的公开地址或提供者、许可、教学用途、覆盖时间、分发权限、隐私边界和保留期限。
- [ ] 09:55～10:00：用同一字段登记候选来源 B；许可或用途不明确时直接标记“不准入”。
- [ ] 10:00～10:05：分别写清 raw、staging、rejected、curated 的输入、输出、不可做事项和一条示例记录。
- [ ] 10:05～10:10：冻结大屏顺序：来源栏、五指标、负荷、排行、告警、推荐、质量；写明两种分辨率的首屏内容。
- [ ] 16:30～16:35：复核成员2页面字段是否缺少数据模式、版本、覆盖时间、更新时间或降级说明，只登记问题，不改 `clients/`。
- [ ] 17:15～17:20：把契约样例、来源表、场景表、命令输出和次日依赖交给主负责人。

**命令与预期：**

```bash
cmake -S /home/bit/projects/charge_platform_project -B /home/bit/projects/charge_platform_project/build -G Ninja -DBUILD_TESTING=ON
cmake --build /home/bit/projects/charge_platform_project/build
ctest --test-dir /home/bit/projects/charge_platform_project/build --output-on-failure
git diff --check
```

预期：构建与现有测试通过，`git diff --check` 无输出；契约至少有一组完整样例，两份来源均有明确准入结论，四种场景和四层目录职责没有含糊项。

**证据与交接：** 保存契约样例、来源准入表、场景输入/期望表、大屏区域草图；交给成员1复核只读隔离，交给成员2复核界面字段。

**失败处理：** 构建失败先停止新工作并恢复主线；字段语义冲突时提交接口变更单，由成员1裁决；候选来源不合规则保留拒绝理由并继续评估另一来源，不能先下载后补许可。

---

## 4. D2（9月4日）：心跳探针、异常帧夹具和大屏骨架

**目标：** 12:00 前跟随冻结协议实现最小心跳；大屏全部区域与六种状态可导航和测试。

**前置：** D1证据齐全，协议未决项已有结论；成员1在 12:00 冻结协议第一版。

**输入：** 冻结的帧头、最大帧、请求/响应结构、`PILE_HEARTBEAT` 动作和统一错误码；D1大屏区域清单。

**路径：** `simulator/pile/main.cpp`、`simulator/pile/pile_simulator.*`、`dashboard/package.json`、`dashboard/src/App.vue`、`dashboard/src/styles/tokens.css`、`dashboard/src/styles/motion.css`、`dashboard/src/adapters/dashboardDataAdapter.ts`、`dashboard/src/components/`、`dashboard/tests/dashboard.test.ts`。

**方法：** 测试先固定帧输入；模拟器每 5 秒心跳；大屏先用唯一适配器提供固定视图模型，组件只负责展示。

- [ ] 09:00～09:05：复跑主线连接测试并确认 D1 契约文件无未提交漂移。
- [ ] 09:15～09:20：确认今日只写 `simulator/` 与 `dashboard/`，记录协议冻结提交号。
- [ ] 09:30～09:35：建立 `feature/d02-simulator-dashboard-shell` 分支。
- [ ] 09:35～09:40：先写正常、半包、粘包、非法 JSON、零长度和超长帧夹具及期望错误码。
- [ ] 09:40～09:45：运行帧相关测试，确认因心跳客户端或页面组件尚不存在而失败。
- [ ] 09:45～09:50：实现 `--pile PILE-001` 参数和每 5 秒一次的 `PILE_HEARTBEAT`，保留请求编号与明确时间戳。
- [ ] 09:50～09:55：运行多客户端测试，确认模拟器能与两个 Qt 代理同时连接并保持心跳。
- [ ] 10:00～10:05：在 `tokens.css` 写入固定颜色、间距、圆角、字号；在 `motion.css` 写入动效与关闭规则。
- [ ] 10:05～10:10：在 `dashboardDataAdapter.ts` 定义包含来源、五指标、负荷、排行、告警、推荐、质量的单一视图模型。
- [ ] 10:10～10:15：先写六种状态和来源栏测试，运行并确认组件缺失导致失败。
- [ ] 10:15～10:20：创建来源栏、指标、负荷、排行、告警、推荐、质量和页面状态组件的最小可见骨架。
- [ ] 10:20～10:25：实现加载、正常、空数据、接口失败、模型失败、地图失败切换；失败态不得白屏。
- [ ] 10:25～10:30：在 1366×768 检查首屏无横向裁切；关闭动效后再检查一次。
- [ ] 16:30～16:35：请非作者复跑连接与大屏测试；把协议解释差异登记给成员1。

**命令与预期：**

```bash
cmake --build /home/bit/projects/charge_platform_project/build
ctest --test-dir /home/bit/projects/charge_platform_project/build -R "frame_codec_test|server_health_test|multi_client_test" --output-on-failure
cd /home/bit/projects/charge_platform_project/dashboard
npm install
npm test -- --run
```

预期：半包/粘包还原完整帧，非法输入返回稳定错误，三类客户端保持心跳；大屏各区域存在，六种状态都有非空内容，1366×768 首屏核心区域可见，关闭动效后布局不变。

**证据与交接：** 连接日志、六类帧测试输出、1366×768 正常/失败截图、动效关闭截图；向成员1交付心跳行为，向成员2交付来源栏字段清单。

**失败处理：** 12:00 后不得私改协议；协议不匹配就停止 D3 工作并提交变更单。布局或主题失败先修 `tokens.css`、适配器或组件骨架，不加第二套数据字段。

---

## 5. 周末预检一（9月5～6日）：只准备夹具

**目标：** 每天不超过 4 小时，准备可在 D3 重跑的心跳与异常报文，验证时间、温度和累计电量可解析。

**前置：** D2 已冻结；周末结果不能标为正式通过。

**输入：** D1四场景表、D2帧格式、冻结的时间与单位定义。

**路径：** `simulator/pile/` 内测试夹具、`analytics/tests/fixtures/`；不修改协议和业务代码。

**方法：** 每个夹具固定桩号、时间起点、温度序列和累计 Wh 序列；通过“解析结果等于固定值”验证，不凭日志目测。

### 9月5日

- [ ] 09:00～09:05：确认主线提交号和冻结协议版本，不创建功能分支。
- [ ] 09:05～09:10：制作 3 条正常心跳，时间间隔固定 5 秒。
- [ ] 09:10～09:15：制作温度正常到越界的固定序列，写明阈值前后期望。
- [ ] 09:15～09:20：制作累计电量单调递增序列和一条倒退记录。
- [ ] 09:20～09:25：运行解析测试并记录精确数值与错误原因。

### 9月6日

- [ ] 09:00～09:05：重新从主线读取夹具，确认没有本机临时路径。
- [ ] 09:05～09:10：制作心跳中断 15 秒以上的超时序列。
- [ ] 09:10～09:15：制作重启成功与 `command-failure` 回复夹具。
- [ ] 09:15～09:20：重复运行解析测试两次，确认结果完全一致。
- [ ] 09:20～09:25：登记问题、期望通过数和期望拒绝数，注明“D3 主线重跑”。

**命令与预期：**

```bash
cmake --build /home/bit/projects/charge_platform_project/build
ctest --test-dir /home/bit/projects/charge_platform_project/build -R telemetry_service_test --output-on-failure
git diff --check
```

预期：合法时间、温度和累计电量被精确解析，累计电量倒退和非法时间被稳定拒绝；两次结果一致，冻结协议没有变化。

**证据与交接：** 夹具文件、测试输出、问题登记；周一交给成员1复核遥测校验。

**失败处理：** 只登记解析失败和对应字段，不在周末修改冻结协议，也不把预检截图当正式验收证据。

---

## 6. D3（9月7日）：固定心跳、站点样例和模拟大屏

**目标：** 同一批模拟站点同时驱动心跳、大屏五指标、负荷曲线和站点排行，并为第二阶段推荐保留足够字段。

**前置：** D2协议、连接和大屏骨架通过；周末夹具已回到主线重跑。

**输入：** 成员1提供的 3 站、12 桩、20 用户、30 天固定种子；登录、冻结、坐标边界和同距离排序用例。

**路径：** `simulator/pile/`、`dashboard/src/adapters/dashboardDataAdapter.ts`、`dashboard/src/components/MetricStrip.vue`、`LoadChart.vue`、`StationRanking.vue`、`dashboard/tests/dashboard.test.ts`。

**方法：** 所有模拟指标从同一适配器数据集派生；排序先按业务分数，再以距离和站点号稳定破同分；卡片背景白色、文字深色、辅助色不降低对比度。

- [ ] 09:00～09:05：主线复跑连接、登录、地理和站点测试。
- [ ] 09:15～09:20：确认种子版本、模拟模式和站点编号不会重复生成。
- [ ] 09:30～09:35：建立 `feature/d03-simulated-dashboard-data` 分支。
- [ ] 09:35～09:40：把周末正常心跳夹具写成正式测试，先确认失败点只在数据适配或运行器。
- [ ] 09:40～09:45：启动固定心跳并确认每个桩号、时间和状态可查询。
- [ ] 09:45～09:50：加入登录、新用户、冻结用户、坐标边界和相同距离的固定输入，不写业务判断。
- [ ] 09:50～09:55：让适配器从同一批站点生成累计电量、车次、营收、在线率、告警数。
- [ ] 09:55～10:00：生成 24 小时实际负荷数组和站点利用率排行，固定排序破同分规则。
- [ ] 10:00～10:05：在来源栏显示 `SIMULATED`、数据集版本、覆盖时间和更新时间。
- [ ] 10:05～10:10：检查五张指标卡依次使用绿、蓝、紫、青、珊瑚红辅助色，文字和单位可读。
- [ ] 10:10～10:15：触发空数据与接口失败，确认来源栏和筛选条件仍保留。
- [ ] 16:30～16:35：用两个 Qt 端执行登录、冻结与地图降级；问题只登记给对应负责人。

**命令与预期：**

```bash
cmake --build /home/bit/projects/charge_platform_project/build
ctest --test-dir /home/bit/projects/charge_platform_project/build -R "user_service_test|geo_test|station_service_test|multi_client_test" --output-on-failure
cd /home/bit/projects/charge_platform_project/dashboard
npm test -- --run
```

预期：固定心跳持续；重复种子不重复数据；相同输入产生稳定排行；大屏来源、五指标、24小时负荷和站点排行均显示 `SIMULATED`，空数据与失败态不白屏。

**证据与交接：** 心跳日志、固定输入清单、大屏模拟截图、稳定排序测试；向成员1交付异常输入边界，向成员2交付推荐所需字段。

**失败处理：** 指标不一致时只回到唯一适配器和种子输入排查，不在各组件补数；协议字段不足则提交变更单，不自行增加必填字段。

---

## 7. D4（9月8日）：递增遥测、竞争输入和模拟告警

**目标：** 固定递增电量、温度和心跳可支持预约/充电演示；竞争、过期和倒退输入可重复；告警、推荐和质量区域使用同一事件批次。

**前置：** D3固定心跳与模拟大屏通过；成员1订单状态机接口已可测试。

**输入：** 两用户抢同一桩、预约过期、累计电量倒退、正常充电事件。

**路径：** `simulator/pile/pile_simulator.*`、模拟场景测试、`dashboard/src/components/AlarmFeed.vue`、`RecommendationPanel.vue`、`DataQualityPanel.vue`、`dashboard/tests/dashboard.test.ts`。

**方法：** 累计 Wh 只能单调递增；竞争与过期由服务端裁决，模拟器只发固定输入；状态用颜色、图标、文字三者共同表达。

- [ ] 09:00～09:05：复跑订单状态、预约竞争和遥测解析测试。
- [ ] 09:15～09:20：确认模拟器不决定订单最终状态、不直接写 SQLite。
- [ ] 09:30～09:35：建立 `feature/d04-telemetry-scenarios` 分支。
- [ ] 09:35～09:40：写递增 Wh、温度、心跳序列测试，先确认实现缺口导致失败。
- [ ] 09:40～09:45：实现固定间隔遥测；同一场景重复运行使用相同输入序列。
- [ ] 09:45～09:50：准备两用户同桩预约输入，断言只能由服务端返回一个成功。
- [ ] 09:50～09:55：准备预约过期输入，断言过期后桩位释放。
- [ ] 09:55～10:00：准备累计 Wh 倒退输入，断言服务端拒绝且不覆盖最后有效值。
- [ ] 10:00～10:05：将同一模拟事件批次映射到告警流、推荐和质量面板。
- [ ] 10:05～10:10：为严重告警使用珊瑚红＋图标＋“严重”，为待确认使用琥珀色＋图标＋“待确认”。
- [ ] 10:10～10:15：触发断线，确认大屏保留最近成功数据并显示更新时间与过期提示。
- [ ] 16:30～16:35：请成员1复跑遥测和竞争测试；成员3操作 Qt 恢复页核对权威状态。

**命令与预期：**

```bash
cmake --build /home/bit/projects/charge_platform_project/build
ctest --test-dir /home/bit/projects/charge_platform_project/build -R "order_state_test|reservation_race_test|telemetry_service_test" --output-on-failure
cd /home/bit/projects/charge_platform_project/dashboard
npm test -- --run
```

预期：并发预约只有一个成功；非法状态和倒退电量被拒绝；重连恢复服务端状态；告警、推荐、质量面板对同一批次口径一致。

**证据与交接：** 竞争、过期、倒退输入文件；充电中、断线、告警截图；向成员1交付服务端复现步骤，向成员2交付状态文字。

**失败处理：** 订单或桩状态不一致时停止在模拟器或大屏补偿，由成员1修服务端事务；大屏口径不一致时修唯一适配器。

---

## 8. D5（9月9日）：四种异常、模拟导出和第一阶段内验

**目标：** 14:00 前完成模拟停止、四种固定场景、`charge_event_v1` 的 `SIMULATED` 样例和大屏全部验收状态。

**前置：** D4订单到待支付可运行；成员1提供只读导出；成员2完成 11＋9 页面。

**输入：** `normal`、`overheat`、`timeout`、`command-failure`，完成订单及告警模拟数据。

**路径：** `simulator/pile/scenario.*`、`analytics/contracts/charge_event_v1.schema.json`、`runtime/analytics/staging/stage1-sample.csv`（运行产物，不进 Git）、`dashboard/` 全部既定文件。

**方法：** 四场景固定可复现；模拟导出按契约校验；大屏状态与动效均由统一适配层和样式层控制。

- [ ] 09:00～09:05：复跑全部现有 C++ 与 Web 测试，阻断时不领取新工作。
- [ ] 09:15～09:20：确认 14:00 后只测试和修阻断/严重缺陷。
- [ ] 09:30～09:35：建立 `feature/d05-stage1-data-dashboard` 分支。
- [ ] 09:35～09:40：为四场景分别写“输入→期望状态→告警/回复”测试并先确认失败。
- [ ] 09:40～09:45：完成 `normal` 正常停止并保留最后有效 Wh。
- [ ] 09:45～09:50：完成 `overheat` 固定越界并生成可查询高温事件。
- [ ] 09:50～09:55：完成 `timeout` 心跳停止；不发送伪造恢复消息。
- [ ] 09:55～10:00：完成 `command-failure` 固定失败回复并保留告警。
- [ ] 10:00～10:05：运行模拟样例导出，核对契约、模式、版本、批次、覆盖时间和无效行数。
- [ ] 10:05～10:10：逐一触发加载、正常、空数据、接口失败、模型失败、地图失败。
- [ ] 10:10～10:15：检查卡片进入、数字变化、图表绘制、在线呼吸点；再以 reduced-motion 关闭并复跑。
- [ ] 10:15～10:20：在 1366×768 和 1920×1080 各检查一次首屏与第二屏。
- [ ] 14:00～14:05：冻结新增；删除仅供开发的临时入口，但保留验收用固定状态切换方式。
- [ ] 16:30～16:35：非作者复跑模拟导出和大屏测试；成员3操作完整基础充电演示。

**命令与预期：**

```bash
cmake --build /home/bit/projects/charge_platform_project/build
ctest --test-dir /home/bit/projects/charge_platform_project/build --output-on-failure
bash /home/bit/projects/charge_platform_project/tests/e2e/basic_charge_flow.sh
bash /home/bit/projects/charge_platform_project/scripts/export-analysis-sample.sh
cd /home/bit/projects/charge_platform_project/dashboard
npm test -- --run
```

预期：基础流程完成，支付只记一次；模拟样例输出 `CONTRACT=charge_event_v1`、`DATA_MODE=SIMULATED`、`INVALID_ROWS=0`；大屏两种尺寸可读，六种状态完整，关闭动效后业务和数据不变；阻断/严重缺陷为 0。

**证据与交接：** 四场景测试、导出控制台、大屏两尺寸截图、动效开关前后、基础闭环结果；交给成员1纳入阶段一验收，交给成员2核对来源与颜色含义。

**失败处理：** 任一阻断或严重缺陷不为 0，D6 不得标记通过；14:00 后不扩展功能，只修当前验收场景。基础闭环若需分析服务才能运行，立即报告成员1并修复隔离。

---

## 9. D6（9月10日）：第一阶段正式验收与冻结

**目标：** 成员3可独立演示模拟器、大屏和模拟导出，所有数据明确标记 `SIMULATED`；通过后冻结 `charge_event_v1`。

**前置：** D5内验通过，阻断/严重缺陷为 0，11＋9 页面和大屏全部可达。

**输入：** 第一阶段发布候选提交、固定演示脚本、阶段一证据清单。

**路径：** `simulator/`、`dashboard/`、`analytics/contracts/charge_event_v1.schema.json`、`docs/test-results/stage-1-acceptance.md`（由主负责人汇总）。

**方法：** 演示严格按固定顺序；先证明界面和模拟数据，再证明关闭分析组件仍能完成基础业务。

- [ ] 09:00～09:05：确认发布候选提交号、数据库初始状态和四种场景可重置。
- [ ] 09:05～09:10：启动 `normal` 桩并检查来源栏为 `SIMULATED`。
- [ ] 09:10～09:15：巡检大屏来源、五指标、负荷、排行、告警、推荐、质量。
- [ ] 09:15～09:20：配合演示注册、找站、导航降级、预约、充电和断线恢复。
- [ ] 09:20～09:25：停止充电并核对大屏车次、电量与营收更新。
- [ ] 09:25～09:30：注入固定异常，核对告警事件和工单出现。
- [ ] 09:30～09:35：运行模拟导出，口头说明其为教学模拟数据而非真实运营数据。
- [ ] 09:35～09:40：停止 analytics/dashboard 后重跑基础流程，确认不退化。
- [ ] 09:40～09:45：恢复大屏并验证最近成功数据或明确加载状态。
- [ ] 16:30～16:35：由非作者复跑关键命令；将实际输出交给成员1。

**命令与预期：**

```bash
cmake --build /home/bit/projects/charge_platform_project/build
ctest --test-dir /home/bit/projects/charge_platform_project/build --output-on-failure
bash /home/bit/projects/charge_platform_project/tests/e2e/basic_charge_flow.sh
bash /home/bit/projects/charge_platform_project/scripts/export-analysis-sample.sh
cd /home/bit/projects/charge_platform_project/dashboard
npm test -- --run
```

预期：全部通过；基础系统在分析组件未启动时仍完成订单；大屏与导出只显示 `SIMULATED`；阶段一记录完整后由成员1标记 `v0.1.0-stage1`。

**证据与交接：** 演示录屏、命令输出、模拟导出摘要、停止分析后的基础流程结果；把冻结契约提交号交给 D7 使用。

**失败处理：** 验收未通过时，当天只处理阶段一责任模块，正式数据导入顺延；不得绕过冻结契约或把模拟数据改名为正式数据。

---

## 10. D7（9月11日）：固定异常、来源准入和 raw 批次

**目标：** 四种固定异常正式可用；至少一个来源通过准入并形成不可改写的 raw 批次；大屏仅通过适配器切换正式数据。

**前置：** 阶段一正式验收通过并冻结；`charge_event_v1` 版本已记录。

**输入：** 候选来源文件、来源许可或提供者说明、版本、覆盖时间、隐私字段清单；第一阶段模拟适配器。

**路径：** `simulator/pile/scenario.*`、`analytics/src/source_registry.py`、`analytics/src/ingest_raw.py`、`analytics/pyproject.toml`、`analytics/tests/test_source_registry.py`、`analytics/tests/test_ingest_raw.py`、`analytics/manifests/source.example.json`、`docs/data-source-register.md`、`runtime/analytics/raw/<ingest_batch>/`、`dashboard/src/adapters/dashboardDataAdapter.ts`。

**方法：** 准入先于复制；真实匿名数据必须拒绝手机号、姓名、支付账号、车辆标识和连续轨迹标识；raw 只复制并生成清单，不修改源文件。

- [ ] 09:00～09:05：确认阶段一标签、契约和导航没有漂移。
- [ ] 09:15～09:20：确认今日只改场景、准入、raw 和唯一大屏适配器。
- [ ] 09:30～09:35：建立 `feature/d07-source-raw-ingest` 分支。
- [ ] 09:35～09:40：先写缺来源名、用途、模式、版本、覆盖时间时拒绝的测试。
- [ ] 09:40～09:45：写隐私字段命中与 `SIMULATED` 冒充真实模式的拒绝测试。
- [ ] 09:45～09:50：运行测试，确认因准入器未实现而失败。
- [ ] 09:50～09:55：实现来源登记字段：提供者/地址、允许/禁止用途、取得日期、模式、版本、覆盖时间、分发许可、保留期限、隐私边界。
- [ ] 09:55～10:00：实现 raw 导入：复制至批次目录并生成批次清单，不改源文件。
- [ ] 10:00～10:05：对获准输入运行一次导入，记录批次号、文件数、行数和模式。
- [ ] 10:05～10:10：对不合规来源运行一次，确认没有生成可用 raw 批次。
- [ ] 10:10～10:15：正式实现四种场景命令参数并复跑异常测试。
- [ ] 10:15～10:20：仅在 `dashboardDataAdapter.ts` 增加正式接口入口；接口不可用时明确保留 `SIMULATED`。
- [ ] 16:30～16:35：请成员1复跑准入与导入；请成员2核对大屏来源文字。

**命令与预期：**

```bash
cmake --build /home/bit/projects/charge_platform_project/build
ctest --test-dir /home/bit/projects/charge_platform_project/build -R telemetry_service_test --output-on-failure
cd /home/bit/projects/charge_platform_project/analytics
.venv/bin/python -m pytest -q tests/test_source_registry.py tests/test_ingest_raw.py
```

预期：固定异常可查询；合法来源生成 raw 批次及清单，源文件未变；非法来源、隐私字段和模式冒充被拒绝；大屏不会从 raw 直接显示指标。

**证据与交接：** 来源登记、准入测试、批次清单、源文件前后元数据、四场景测试；交给成员1复核隐私和只读边界。

**失败处理：** 9月11日结束前仍无合法来源，立即选用来源清楚且允许教学分析的公开聚合数据；不得把模拟数据改名为真实数据，也不得降低许可或隐私门槛。

---

## 11. 周末预检二（9月12～13日）：六类质量样例

**目标：** 每天不超过 4 小时，准备 D8 重跑的正常、重复、缺失、未映射、错误单位和隐私字段六类输入及精确通过/拒绝数量。

**前置：** D7 至少一个来源通过准入；冻结契约不变。

**输入：** 获准 raw 批次的最小脱敏样例、站点人工映射表、契约必填字段和单位规则。

**路径：** `analytics/tests/fixtures/`、`analytics/manifests/`；周末不训练模型、不改契约、不改第一阶段界面。

**方法：** 每类至少一条唯一记录；重复按来源记录号和业务键判定；站点映射必须人工确认，不能按名称猜测；隐私字段整行拒绝并记录原因。

### 9月12日

- [ ] 09:00～09:05：确认 D7 获准批次和数据模式。
- [ ] 09:05～09:10：制作 2 条完全合法记录并写出规范化期望。
- [ ] 09:10～09:15：复制一条形成确定重复，预期进入 rejected。
- [ ] 09:15～09:20：删除一个必填字段，预期以“必填缺失”拒绝。
- [ ] 09:20～09:25：使用无人工映射的来源站点号，预期以“未映射”拒绝。

### 9月13日

- [ ] 09:00～09:05：制作错误电量、时长或金额单位记录，预期拒绝而非猜测转换。
- [ ] 09:05～09:10：加入一个明确隐私字段，预期准入/质量门禁拒绝。
- [ ] 09:10～09:15：手工计算输入、通过、拒绝和各拒绝原因数量。
- [ ] 09:15～09:20：连续执行夹具校验两次，确认数量一致。
- [ ] 09:20～09:25：记录“D8需在主线正式重跑”，不生成正式模型。

**命令与预期：**

```bash
cd /home/bit/projects/charge_platform_project/analytics
.venv/bin/python -m pytest -q tests/test_source_registry.py tests/test_ingest_raw.py
git diff --check
```

预期：来源准入仍通过；六类样例的期望数明确；冻结契约无变化，未生成模型文件。

**证据与交接：** 六类夹具、手工对账表、两次相同输出；D8交给成员1复跑。

**失败处理：** 只登记哪一类与契约不一致；不在周末放宽质量规则、不修改 raw、不训练正式模型。

---

## 12. D8（9月14日）：staging/rejected、质量、curated 和基线

**目标：** raw 保持不变；完成显式清洗、拒绝清单、可对账质量报告、小时 curated 和昨日同小时基线，并通过 20 桩 60 秒预检。

**前置：** D7合法 raw 批次存在；周末六类夹具已准备；成员1告警工单闭环可测。

**输入：** raw 批次、人工站点映射、`charge_event_v1`、数据集质量阈值。

**路径：** `analytics/src/normalize.py`、`station_mapping.py`、`quality.py`、`dataset.py`、`build_curated.py`、`train.py`、`model.py`、对应测试、`docs/test-results/data-quality.md`、`docs/data-lineage.md`、`runtime/analytics/staging/<batch>/`、`rejected/<batch>/`、`curated/<dataset_version>/hourly_station.csv`。

**方法：** 显式解析发生时间和导入时间并统一时区；电量转 Wh、时长转秒、金额转分；站点只查人工映射；小时聚合订单数、充电量、平均时长和空闲桩；基线算法为同站点昨日同小时 `load_wh`，即按站点向后移 24 小时。

- [ ] 09:00～09:05：记录 raw 文件数、行数和修改时间，作为不可变基线。
- [ ] 09:15～09:20：确认质量阈值来自数据集清单，不在代码中对所有数据集写同一比例。
- [ ] 09:30～09:35：建立 `feature/d08-quality-curated-baseline` 分支。
- [ ] 09:35～09:40：把六类夹具写成规范化和质量失败测试。
- [ ] 09:40～09:45：写时间切分不得使用未来行、模式不得隐式混合的失败测试。
- [ ] 09:45～09:50：运行测试并确认函数缺失或行为不符导致失败。
- [ ] 09:50～09:55：实现时区、Wh、秒、分、状态的显式转换；非法值进入 rejected。
- [ ] 09:55～10:00：实现人工站点映射；未映射记录拒绝，不按站名猜测。
- [ ] 10:00～10:05：输出 staging 和带记录号、原因码、说明的 rejected。
- [ ] 10:05～10:10：实现质量统计：输入、通过、拒绝、重复、缺失、未映射、单位错误、隐私命中。
- [ ] 10:10～10:15：验证 `输入=通过+拒绝` 且拒绝分类之和等于拒绝数。
- [ ] 10:15～10:20：从通过记录按站点/小时生成 curated 与批次元数据。
- [ ] 10:20～10:25：实现昨日同小时基线；首日无历史值时明确标记不可用，不随机补值。
- [ ] 10:25～10:30：记录完整血缘链和回算一小时样例。
- [ ] 14:00～14:05：运行 20 桩 60 秒预检，记录连接、错误、告警和数据库一致性。
- [ ] 16:30～16:35：由成员1独立复跑质量命令，核对 raw 修改时间未变。

**命令与预期：**

```bash
cd /home/bit/projects/charge_platform_project/analytics
.venv/bin/python -m pytest -q tests/test_normalize.py tests/test_quality.py tests/test_dataset.py tests/test_model.py
cd /home/bit/projects/charge_platform_project
python3 tests/load/run_20_piles.py --duration 60
```

预期：六类数据按预期通过或拒绝；质量数量可对账；同一批次重复运行结果相同；raw 未改变；curated 可回算；昨日同小时基线有明确可用性；20个软件模拟桩运行 60 秒无崩溃和协议解析错误。

**证据与交接：** 质量报告、rejected 样例、curated 元数据、血缘图、基线指标、60秒负载输出；交给成员1独立复跑并确认分析未写业务表。

**失败处理：** 对账不平、raw 被改、模式混合或时间泄漏均为阻断，禁止进入 D9；回到具体转换、映射或过滤规则修正，不能静默补值。

---

## 13. D9（9月15日）：单一树模型、三个 FastAPI 接口和 600 秒负载

**目标：** 用时间先后切分训练一个树模型，与昨日同小时基线比较；实现 `/health`、`/forecast`、`/recommend`；大屏切正式适配器；完成 20 桩 600 秒与数据管线基准。

**前置：** D8质量、curated、血缘和60秒负载全部通过；成员1可提供只读业务出口。

**输入：** 一个明确数据模式的 curated 版本、昨日同小时基线、当前站点距离/空闲/价格数据。

**路径：** `analytics/src/train.py`、`model.py`、`api.py`、`recommend.py`、`analytics/tests/test_api.py`、`test_recommend.py`、`run_reproducibility_check.py`、`artifacts/`、`dashboard/src/adapters/dashboardDataAdapter.ts`、`analytics/tests/benchmark_pipeline.py`。

**方法：** 只预测“站点负荷”这一种目标变量，分别构造 `target_load_h1`、`target_load_h6`、`target_load_h24` 三个标签，表示从各预测起点观察未来1小时、6小时和24小时的站点负荷；三个标签是同一目标变量的三个预测时域，不是三个业务目标。为降低实现错误，使用三个同配置的 `HistGradientBoostingRegressor(max_depth=6, learning_rate=0.08, random_state=42)`，每个时域一个模型；这仍是一种树模型算法。训练集时间严格早于测试集，并分别输出三个时域的 MAE、RMSE、样本量及相对基线差值。

每个预测起点的基线都取“前一天相同站点、相同目标窗口”的实际负荷：1小时预测对比前一天对应1小时窗口，6小时与24小时同理。三个时域分别比较模型与各自基线；某一时域没有稳定优于基线时，只把该时域发布为基线，不得用其他时域的好结果替代。大屏首屏主要显示1小时结果，6小时和24小时通过切换或详情查看；接口字段、自动测试和评测报告必须同时覆盖三者。

“预计空闲桩”不得训练成第二目标，而是由已发布的负荷结果做教学估算：先令 `safe_load_kw = max(0, predicted_load_kw)`，再计算 `estimated_free = min(total_piles, max(0, total_piles - ceil(safe_load_kw / nominal_kw_per_pile)))`；计算前必须验证 `total_piles >= 0` 且 `nominal_kw_per_pile > 0`，否则返回“容量参数不可用”而不是给出数字。必须在接口和说明中记录两个容量参数的来源，并注明这是容量换算估算，不是精确排队承诺。推荐权重为距离30%、等待30%、价格20%、预测负荷20%，全部归一化到 `[0,1]`；缺预测时改为距离40%、当前等待35%、价格25%；同分按距离、站点号排序。

- [ ] 09:00～09:05：确认训练输入只含一个明确模式和一个 curated 版本。
- [ ] 09:15～09:20：确认本日只保留站点负荷一个目标变量、一种树模型算法，并为1/6/24小时分别建立标签、模型实例和昨日同窗口基线。
- [ ] 09:30～09:35：建立 `feature/d09-model-api-performance` 分支。
- [ ] 09:35～09:40：写训练时间早于测试时间、特征不晚于预测起点的测试。
- [ ] 09:40～09:45：写同一输入两次指标相同、模型选择相同的测试。
- [ ] 09:45～09:50：写推荐第一名、理由、缺预测降级和同分稳定排序测试。
- [ ] 09:50～09:55：写三个接口的字段、模型缺失和数据缺失测试并确认失败。
- [ ] 09:55～10:00：实现时间切分、树模型训练和 MAE/RMSE 对比。
- [ ] 10:00～10:05：逐时域比较模型与基线；任何未稳定胜出的时域单独设为 `baseline`，保留三份评测证据。
- [ ] 10:05～10:10：实现推荐归一化、固定权重、降级权重、稳定破同分和中文理由。
- [ ] 10:10～10:15：实现 `/health`：状态、modelMode、dataMode、数据集、版本、覆盖时间、更新时间、样本量。
- [ ] 10:15～10:20：实现 `/forecast`：未来1/6/24小时站点负荷、由容量公式换算的预计空闲桩、参数来源、各自预测时间范围和逐时域降级标记。
- [ ] 10:20～10:25：实现 `/recommend`：排序、总分、理由、输入摘要；不返回原始路径或个人标识。
- [ ] 10:25～10:30：模型文件缺失时验证服务仍启动并返回基线；正式数据缺失时明确降级，不伪造数据。
- [ ] 10:30～10:35：在唯一适配器切换获准正式数据，并保留接口失败/模型失败状态。
- [ ] 13:30～13:35：启动 20 桩 600 秒测试，记录开始提交号与机器配置。
- [ ] 13:35～13:40：同时运行数据管线基准，记录行数、文件大小、耗时、峰值内存和拒绝数。
- [ ] 13:40～13:45：负载结束后运行数据库一致性检查并核对离线/恢复状态。
- [ ] 16:30～16:35：成员1独立复跑评测与隔离；成员2核对大屏和 Qt 的来源表述。

**命令与预期：**

```bash
cd /home/bit/projects/charge_platform_project/analytics
.venv/bin/python -m pytest -q
.venv/bin/python tests/run_reproducibility_check.py
.venv/bin/python tests/benchmark_pipeline.py --manifest manifests/accepted.json
cd /home/bit/projects/charge_platform_project/dashboard
npm test -- --run
cd /home/bit/projects/charge_platform_project
python3 tests/load/run_20_piles.py --duration 600
python3 tests/load/assert_database.py
```

另开一个终端启动 API；看到启动成功后保持该终端运行：

```bash
cd /home/bit/projects/charge_platform_project
analytics/.venv/bin/python -m uvicorn analytics.src.api:app --host 127.0.0.1 --port 48100
```

再开第三个终端轮询接口：

```bash
curl -s http://127.0.0.1:48100/health
curl -s http://127.0.0.1:48100/forecast
curl -s http://127.0.0.1:48100/recommend
```

预期 `/health` 返回可用状态，三个时域均出现在 `/forecast`，`/recommend` 返回稳定排序。测试结束回到 API 终端按 `Ctrl+C`，确认服务退出；不要用强制删除进程替代正常停止。

预期：Python 与 Web 测试通过；同一输入两次指标和排序一致；三个接口返回完整来源/版本/覆盖时间与模式；模型不优于基线时稳定发布基线；600秒后服务不崩溃、订单无丢失、桩占用一致、断开的3桩产生离线告警并恢复；管线两次聚合行数一致。

**证据与交接：** 时间切分边界、模型/基线指标、三个接口响应样例、两次复现输出、600秒负载和数据库检查、管线性能；交给成员1接入800ms超时与业务降级，交给成员2展示推荐理由。

**失败处理：** 时间泄漏、不可复现、业务库被写或600秒状态不一致均为阻断；模型未优于基线不算失败，演示基线。API不可用时大屏保留实际数据并显示模型/接口失败，基础业务不得受影响。

---

## 14. D10（9月16日）：全量内验、两尺寸、关闭动效、冷启动和材料

**目标：** 12:00 前完成大屏回算、两种分辨率、动效关闭、来源/质量/模型说明和发布内容检查；12:00 后只修阻断/严重缺陷并完成两遍演练。

**前置：** D9测试、API、复现、600秒和管线基准通过；发布候选配置可用。

**输入：** 获准批次、curated 版本、模型/基线评测、发布候选包、固定演示脚本。

**路径：** `dashboard/`、`docs/data-source-register.md`、`docs/data-lineage.md`、`docs/test-results/data-quality.md`、`docs/test-results/data-pipeline-performance.md`、成员3提供给发布清单的数据说明。

**方法：** 从大屏数值反查适配器响应、API、curated 小时行和输入批次；发布检查只允许清单、代码和可分发示例，排除 raw、密钥、个人信息、数据库、缓存和模型临时文件。

- [ ] 09:00～09:05：确认发布候选提交、数据模式、版本、覆盖时间和更新时间。
- [ ] 09:05～09:10：回算累计电量与服务车次，记录大屏值、curated 求和值和差异。
- [ ] 09:10～09:15：回算营收、在线率和未处理告警，记录口径来源。
- [ ] 09:15～09:20：回算24小时负荷与站点排行，确认筛选条件一致。
- [ ] 09:20～09:25：核对告警流、分流建议、质量面板和固定来源栏。
- [ ] 09:25～09:30：在 1366×768 截图，确认五指标、负荷、排行、告警无横向裁切。
- [ ] 09:30～09:35：在 1920×1080 截图，确认信息密度合理且无大片无意义空白。
- [ ] 09:35～09:40：关闭动效，重复同一筛选和交互，核对数据与操作结果不变。
- [ ] 09:40～09:45：先停止现有 API，把测试配置临时指向一个明确不存在的模型路径；重新启动 API 后检查 `/health`、`/forecast` 三个时域和大屏，确认分别切换到各自基线且实际数据保留；正常停止 API，恢复原配置，再次启动并复查 `/health`，最后正常停止。不得删除或移动模型文件。
- [ ] 09:45～09:50：停止 analytics，确认大屏显示明确失败/最近成功数据，基础充电流程仍成功。
- [ ] 09:50～09:55：检查数据来源、质量、模型和血缘说明可由零上下文人员读懂。
- [ ] 09:55～10:00：检查发布候选不含无许可 raw、隐私、密钥、数据库、缓存和本地模型临时产物。
- [ ] 10:00～10:05：由非脚本作者在新目录冷启动阶段一，成员3记录自己组件所需参数。
- [ ] 10:05～10:10：在新目录冷启动阶段二并检查健康、模式、版本与来源。
- [ ] 12:00～12:05：冻结新增，只允许修阻断和严重缺陷。
- [ ] 14:00～14:05：执行第一遍 6～8 分钟演练，逐项记录唯一缺陷和责任人。
- [ ] 15:30～15:35：修复并复测后执行第二遍完整演练，不跳过失败过的场景。
- [ ] 16:30～16:35：非作者复跑成员3测试与回算，签入实际结果。

**命令与预期：**

```bash
cmake --build /home/bit/projects/charge_platform_project/build
ctest --test-dir /home/bit/projects/charge_platform_project/build --output-on-failure
cd /home/bit/projects/charge_platform_project/analytics
.venv/bin/python -m pytest -q
cd /home/bit/projects/charge_platform_project/dashboard
npm test -- --run
cd /home/bit/projects/charge_platform_project
bash tests/e2e/cross_client_flow.sh
bash tests/e2e/basic_charge_flow.sh
bash scripts/health-check.sh
python3 tests/load/assert_database.py
```

由非脚本作者在发布包解压出的临时干净目录执行冷启动。该目录不得携带 `build/`、`.venv/`、`node_modules/`、数据库、缓存或运行期 raw 数据，也不得删除或覆盖原项目：

```bash
bash scripts/init-demo.sh
bash scripts/start-all.sh --stage 1
bash scripts/health-check.sh --stage 1
bash scripts/stop-all.sh

bash scripts/prepare-analytics-data.sh --manifest analytics/manifests/accepted.json
bash scripts/start-all.sh --stage 2 --manifest analytics/manifests/accepted.json
bash scripts/health-check.sh --stage 2
bash scripts/stop-all.sh
```

记录临时目录绝对路径、依赖安装、每条启动/健康/停止命令输出；阶段一预期只报告基础组件正常，阶段二还必须报告数据质量、分析服务、大屏、数据模式和版本正常。

预期：全部通过；大屏两尺寸、六状态和关闭动效通过；所有指标可回算；停止 analytics 后基础流程仍成功；两次冷启动和两遍演练成功；阻断/严重缺陷为 0。

**证据与交接：** 两尺寸截图、回算表、动效关闭结果、分析停止录像、冷启动日志、来源/质量/模型说明、两遍演练记录、发布排除清单；交给成员1形成发布候选和最终验收包。

**失败处理：** 12:00 后不再视觉微调或加功能；回算、来源、隔离、冷启动或演练失败时只修对应责任模块并完整复测。发布包发现无许可或敏感数据时停止交付，移除后重新检查。

---

## 15. D11（9月17日）：第二阶段正式验收和最终交付

**目标：** 演示来源准入、raw、staging/rejected、curated、基线/模型、三个接口和大屏回算；通过后当日冻结最终版本。

**前置：** D10全量内验、冷启动、两遍演练和发布检查全部通过；阻断/严重缺陷为 0。

**输入：** 最终发布候选、获准来源清单、固定批次、回算表、模型/基线报告、性能报告。

**路径：** 成员3全部责任目录及最终证据；不再建立新的功能文件。

**方法：** 从来源到大屏沿血缘链逐层展示；明确区分实时业务与历史分析；树模型只在证据显示优于基线时作为当前结果，否则展示基线。

- [ ] 09:00～09:05：确认最终提交号、数据模式、批次、版本和发布文件清单。
- [ ] 09:05～09:10：展示来源登记与许可结论，说明为什么允许教学分析和是否允许分发。
- [ ] 09:10～09:15：展示 raw 批次清单和只读边界，不打开或投放敏感原始内容。
- [ ] 09:15～09:20：展示 staging 转换和 rejected 原因，核对质量恒等式。
- [ ] 09:20～09:25：展示 curated 小时数据、输入批次和一条回算链。
- [ ] 09:25～09:30：展示昨日同小时基线与树模型 MAE/RMSE；说明当前选择依据。
- [ ] 09:30～09:35：调用 `/health`、`/forecast`、`/recommend`，核对来源与理由。
- [ ] 09:35～09:40：展示大屏五指标、负荷、排行、告警、推荐和质量，并与回算表一致。
- [ ] 09:40～09:45：停止 analytics，重跑第一阶段基础流程并显示明确降级。
- [ ] 09:45～09:50：恢复 analytics，确认大屏重新查询并显示权威状态。
- [ ] 09:50～09:55：展示20桩600秒结果和“软件模拟负载”说明。
- [ ] 16:30～16:35：由非作者复跑最终命令；核对最终包无敏感或无许可数据。
- [ ] 17:15～17:20：把实际结果、上传记录和成员3模块说明交给成员1，当日冻结。

**命令与预期：**

```bash
cmake -S /home/bit/projects/charge_platform_project -B /home/bit/projects/charge_platform_project/build -G Ninja -DBUILD_TESTING=ON
cmake --build /home/bit/projects/charge_platform_project/build
ctest --test-dir /home/bit/projects/charge_platform_project/build --output-on-failure
cd /home/bit/projects/charge_platform_project/analytics
.venv/bin/python -m pytest -q
.venv/bin/python tests/benchmark_pipeline.py --manifest manifests/accepted.json
cd /home/bit/projects/charge_platform_project/dashboard
npm test -- --run
cd /home/bit/projects/charge_platform_project
bash tests/e2e/basic_charge_flow.sh
bash tests/e2e/cross_client_flow.sh
python3 tests/load/run_20_piles.py --duration 600
python3 tests/load/assert_database.py
bash scripts/health-check.sh
```

预期：C++、Python、Web、两个端到端流程、600秒负载、数据库检查和健康检查全部通过；关闭分析服务后基础流程成功；数据可按批次追溯；大屏可回算；阻断/严重缺陷为 0。

**证据与交接：** `stage-2-acceptance.md` 所需的成员3命令输出、来源/质量/血缘/评测/性能、大屏回算、停止分析后的基础流程、上传记录；由成员1汇总并冻结最终版本。

**失败处理：** 对应模块责任人先修，非作者完整复测后再申请全量验收；未全部通过时不得冻结、标记或上传最终版本。9月17日当天只允许修验收缺陷和复测，不安排新的开发内容。

---

## 16. 成员3最终自检清单

- [ ] 单写内容仅位于 `simulator/`、`analytics/`、`dashboard/` 和明确分配的成员3文档/测试路径。
- [ ] 四种场景 `normal`、`overheat`、`timeout`、`command-failure` 输入固定、结果可复现。
- [ ] `charge_event_v1` 的时间、时区、Wh、秒、分、状态、模式、版本和批次语义一致。
- [ ] 第一阶段大屏所有数据明确标记 `SIMULATED`，分析组件关闭时基础充电仍成功。
- [ ] 第二阶段只显示获准的 `REAL_ANONYMIZED` 或 `PUBLIC_AGGREGATED`，未把模拟数据改名。
- [ ] 来源缺许可、用途、版本、覆盖时间或隐私边界时会拒绝导入。
- [ ] raw 未改且不进 Git；staging、rejected、curated 可按批次追溯。
- [ ] 质量报告满足输入、通过、拒绝及分类对账。
- [ ] 只保留“站点负荷”一个目标变量，建立 `target_load_h1`、`target_load_h6`、`target_load_h24`，用同一种树模型算法分别训练并与各自昨日同窗口基线比较。
- [ ] 某时域模型不优于基线时只降级该时域；预计空闲桩仅由容量参数换算，未作为第二训练目标。
- [ ] 训练时间早于测试时间；同一输入两次评测和推荐排序一致。
- [ ] 模型未优于基线时发布基线；缺模型或缺数据时不伪造结果。
- [ ] `/health`、`/forecast`、`/recommend` 均返回来源、模式、版本、覆盖时间和降级信息。
- [ ] 大屏保持浅色，统一颜色、有限渐变和克制动效；状态同时有图标或文字。
- [ ] 加载、正常、空数据、接口失败、模型失败、地图失败六种状态均有证据。
- [ ] 1366×768 和 1920×1080 无核心内容横向裁切；关闭动效后数据与操作不变。
- [ ] 20桩60秒预检和600秒正式测试都有记录，且明确称为“软件模拟负载”。
- [ ] 大屏指标可回算到 curated、staging 和输入批次，实时业务与历史分析标签没有混淆。
- [ ] 发布包不含密钥、个人信息、数据库、缓存、无许可 raw 或不可分发数据。
- [ ] 每项成员3成果均由非作者执行命令复跑，而不是只看截图。
- [ ] 9月16日两遍演练成功，9月17日全量验收与最终冻结完成，阻断/严重缺陷为 0。

## 17. 每日证据记录模板

```text
需求编号：
日期：
主负责人/复核人：
提交号：
输入或数据批次：
执行命令：
通过数量/失败数量：
实际结果：
人工检查：
接口或数据变化：
截图/录屏/日志路径：
回退办法：
未解决问题：
次日依赖：
```
