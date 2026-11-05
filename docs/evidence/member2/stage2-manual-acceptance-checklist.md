# Stage2 Member2 人工验收与非作者复跑清单

更新日期：2026-09-15

依据：S2-T22“两种分辨率、关闭动效、断网恢复”和 S2-T24“非作者按说明操作、单人 10—11 分钟答辩演练”。

状态只允许填写 `PASS`、`FAIL`、`BLOCKED`、`NOT_RUN`。只有实际执行并观察到结果才能填 `PASS`。每一行都必须记录候选提交、执行人和日期；截图不能替代操作结论。

## 1. 执行记录头

```text
candidate commit: TO_VERIFY
branch/tag: TO_VERIFY
OS / desktop session: TO_VERIFY
Qt / CMake / compiler: TO_VERIFY
Node / npm: TO_VERIFY
business database/version: TO_VERIFY
datasetVersion/runId: BLOCKED BY MEMBER3
executor: TO_VERIFY
date: TO_VERIFY
start/end time: TO_VERIFY
```

## 2. S2-T22：车主端

每个检查项分别在 360×780 和 480×1040 执行。

| 分辨率 | 检查项 | 预期 | 状态 | 结果 | 截图 | 备注 | 执行人 | 日期 |
|---|---|---|---|---|---|---|---|---|
| 360×780 | 登录页 | 手机号、错误提示和登录按钮无遮挡 | NOT_RUN |  | U01 |  |  |  |
| 360×780 | 首页和底部导航 | 五个入口可见，导航不遮挡正文 | NOT_RUN |  | U02 |  |  |  |
| 360×780 | 找桩/站点详情 | 列表可滚动，长地址换行，操作按钮可见 | NOT_RUN |  | U03/U04 |  |  |  |
| 360×780 | 导航/预约/活动订单 | 返回路径正确，按钮不溢出 | NOT_RUN |  | U05 |  |  |  |
| 360×780 | 充电/结算/历史 | 状态、金额和列表可读，页面可滚动 | NOT_RUN |  | U05/U06 |  |  |  |
| 360×780 | 用户分析 | 到达时刻和 1/6/24h 可选；不可用提示清楚 | NOT_RUN |  | U07 | 正式结果仍 BLOCKED |  |  |
| 360×780 | Remote 断线/恢复 | 不回退 Mock；显示 offline/stale；恢复后刷新 | NOT_RUN |  | 录屏 R01 | 需真实业务服务 |  |  |
| 360×780 | OFFLINE_DEMO | 标题/页面明确标出演示模式 | NOT_RUN |  | U08 | 不作为正式数据证据 |  |  |
| 480×1040 | 全部上述页面 | 放大后无遮挡、无异常空白、滚动正常 | NOT_RUN |  | U09 | 按同一路径逐页检查 |  |  |
| 480×1040 | 返回和再次进入 | 旧错误/stale 不污染新上下文 | NOT_RUN |  | 录屏 R02 |  |  |  |

## 3. S2-T22：运营端

| 分辨率 | 检查项 | 预期 | 状态 | 结果 | 截图 | 备注 | 执行人 | 日期 |
|---|---|---|---|---|---|---|---|---|
| 1180×720 | 登录/侧栏 | 登录控件和全部侧栏入口可见 | NOT_RUN |  | A00/A01 |  |  |  |
| 1180×720 | Overview/Revenue | 卡片不重叠；Revenue 轴标签可读 | NOT_RUN |  | A01/A02 |  |  |  |
| 1180×720 | Stations/Piles | 表格可滚动，操作按钮可见 | NOT_RUN |  | A03 |  |  |  |
| 1180×720 | Orders/Users | 表格不截断，冻结操作文案完整 | NOT_RUN |  | A04 |  |  |  |
| 1180×720 | Alarm/Ticket/Logs | 告警、工单、日志语义和列内容清楚 | NOT_RUN |  | A05 |  |  |  |
| 1180×720 | Analysis | 四个预测区域可切换；风险与故障明确区分 | NOT_RUN |  | A06 | 正式结果仍 BLOCKED |  |  |
| 1366×768 | 全页面复核 | 侧栏、表格、图表和操作按钮无遮挡 | NOT_RUN |  | A07 |  |  |  |
| 1920×1080 | 全页面复核 | 放大布局合理，无过度拉伸或重叠 | NOT_RUN |  | A08 |  |  |  |
| 1366×768 | Remote 断线/恢复 | 保留允许的旧行并标 stale；恢复后刷新 | NOT_RUN |  | 录屏 R03 | 需真实业务服务 |  |  |
| 1366×768 | OFFLINE_DEMO | 标题、身份、连接状态均有模拟标识 | NOT_RUN |  | A09 |  |  |  |

## 4. S2-T22：Web 大屏

| 分辨率 | 检查项 | 预期 | 状态 | 结果 | 截图 | 备注 | 执行人 | 日期 |
|---|---|---|---|---|---|---|---|---|
| 1366×768 | 整体布局 | 标题、状态、来源台账不截断 | NOT_RUN |  | W01 |  |  |  |
| 1366×768 | 三个区域 | 在线、历史、质量/批次区域边界清楚 | NOT_RUN |  | W02–W04 |  |  |  |
| 1366×768 | 六类容器 | 折线、柱状、环形、热力、仪表、散点均存在 | NOT_RUN |  | W02/W03 | 当前必须为空态 |  |  |
| 1366×768 | DataV | 边框和顶部装饰真实渲染，不以静态图替代 | NOT_RUN |  | W01 |  |  |  |
| 1366×768 | 关闭动效 | 装饰停止变化，布局和可见文字不改变 | NOT_RUN |  | W05 | 正式数字仍 BLOCKED |  |  |
| 1366×768 | 无数据/不可用 | 状态明确，无固定预测、demo 或 fixture 数值 | NOT_RUN |  | W01 |  |  |  |
| 1920×1080 | 整体与三区域 | 卡片不重叠，标题完整，空态位置合理 | NOT_RUN |  | W06 |  |  |  |
| 1920×1080 | 浏览器缩放 | 80%、100%、125% 下无关键区域遮挡 | NOT_RUN |  | W07 | 分别记录缩放比例 |  |  |
| 两种分辨率 | 真实断网恢复 | 保留最后成功数值且动效开关不改数字 | BLOCKED |  | 录屏 R04 | 等待 Member3 API/正式数据 |  |  |

## 5. 截图与录屏清单

截图应包含窗口标题、状态和必要的数据来源；不得包含令牌、完整手机号、密码或未授权数据库内容。建议放入最终候选的 `deliverables/stage2/<commit>/screenshots/member2/`，本轮不创建或伪造截图。

| 编号 | 页面 | 分辨率 | 必须包含 | 文件名建议 | 状态 |
|---|---|---:|---|---|---|
| U01 | 车主登录 | 360×780 | Remote 登录页和正式模式文案 | `U01-user-login-360x780.png` | NOT_RUN |
| U02 | 车主首页 | 360×780 | 资料摘要、底部导航 | `U02-user-home-360x780.png` | NOT_RUN |
| U03 | 找桩 | 360×780 | 列表/地图入口、可滚动区域 | `U03-user-nearby-360x780.png` | NOT_RUN |
| U04 | 站点详情 | 360×780 | 地址、电桩和导航/预约入口 | `U04-user-station-detail-360x780.png` | NOT_RUN |
| U05 | 预约/订单 | 360×780 | 预约确认或活动订单状态 | `U05-user-order-360x780.png` | NOT_RUN |
| U06 | 历史记录 | 480×1040 | 历史列表和滚动区域 | `U06-user-history-480x1040.png` | NOT_RUN |
| U07 | 用户分析 | 480×1040 | 到达时间、1/6/24h、正式不可用状态 | `U07-user-analysis-unavailable-480x1040.png` | NOT_RUN |
| A01 | Overview | 1366×768 | 侧栏、总览卡片、来源 | `A01-admin-overview-1366x768.png` | NOT_RUN |
| A02 | Revenue | 1366×768 | 汇总和趋势轴标签 | `A02-admin-revenue-1366x768.png` | NOT_RUN |
| A03 | Piles | 1180×720 | 电桩表格、状态和操作 | `A03-admin-piles-1180x720.png` | NOT_RUN |
| A04 | Orders | 1180×720 | 订单表格和筛选 | `A04-admin-orders-1180x720.png` | NOT_RUN |
| A05 | Alarm/Ticket | 1366×768 | 告警、工单语义和状态 | `A05-admin-alert-ticket-1366x768.png` | NOT_RUN |
| A06 | Analysis | 1366×768 | 四区域选择、风险/故障区分、不可用态 | `A06-admin-analysis-unavailable-1366x768.png` | NOT_RUN |
| W01 | Web 整体 | 1920×1080 | 浅色主题、三区域、来源台账 | `W01-web-overview-1920x1080.png` | NOT_RUN |
| W02 | 在线业务 | 1366×768 | 环形/仪表容器、来源 | `W02-web-online-1366x768.png` | NOT_RUN |
| W03 | 历史分析 | 1366×768 | 折线/柱状/热力/散点空态 | `W03-web-history-1366x768.png` | NOT_RUN |
| W04 | 质量/批次 | 1366×768 | Hadoop、质量、模型、排行占位 | `W04-web-quality-1366x768.png` | NOT_RUN |
| W05 | 动效关闭 | 1920×1080 | 按钮显示“开启动效”，页面内容不变 | `W05-web-motion-off-1920x1080.png` | NOT_RUN |

建议录屏：`R01` 车主 Remote 断线恢复、`R03` 运营 Remote 断线恢复、`R04` Web 正式分析断线恢复、`R05` 10分30秒完整答辩。备份录屏必须在画面或文件说明中标注“备份录屏”，不能冒充现场实时操作。

## 6. S2-T24：非作者复跑流程

非作者不得接受作者口头补充。某一步缺信息时停止该分支并记 `FAIL` 或 `BLOCKED`，同时记录缺失项。

### 6.1 准备干净副本并固定提交

1. 由集成负责人提供仓库地址、候选提交和已授权验收数据库。
2. 在干净 clone 或独立 worktree 操作，不复用作者的脏工作树。
3. 执行并保存输出：

```bash
git status --short
git switch --detach <CANDIDATE_COMMIT>
git rev-parse HEAD
git status --short
```

`<CANDIDATE_COMMIT>` 当前为 `TO_VERIFY`，不得照抄占位符。若该提交尚未获得推送授权，记录 `BLOCKED: candidate unavailable to reviewer`。

### 6.2 依赖检查

```bash
cmake --version
c++ --version
qtpaths6 --qt-version
node --version
npm --version
```

期望 Node >=23、CMake >=3.22、Qt >=6.2。`qtpaths6` 不存在时记录实际 Qt 探测方法；不要让作者现场口头指定私有路径。

### 6.3 Qt 构建和测试

```bash
cmake -S . -B build-stage2 -DBUILD_TESTING=ON
cmake --build build-stage2 --parallel 2
ctest --test-dir build-stage2 -L '^member2$' --output-on-failure
```

记录 configure/build 总耗时、Member2 passed/total 和失败测试完整名称。最近记录的 17/17 只是参考，不是本次复跑结果。

### 6.4 启动并操作车主端

1. 按 [操作指南](stage2-operation-guide.md#4-运行前配置与业务服务) 获得服务地址和验收凭据。
2. Remote 启动 `./build-stage2/clients/user/charge_platform_user`。
3. 顺序执行：登录 → 首页 → 找桩 → 站点详情 → 导航 → 预约 → 活动订单 → 停止/支付 → 历史 → 用户分析 → 手动找桩 → 登出。
4. 再以 `OFFLINE_DEMO=1` 启动，只确认演示标识和界面路径，不把结果记作正式业务通过。
5. 记录每一步的实际结果和耗时。

### 6.5 启动并操作运营端

1. Remote 启动 `./build-stage2/clients/admin/charge_platform_admin`。
2. 顺序执行：登录 → Overview → Revenue → Stations → Piles → Orders → Users → Alarm/Ticket → Logs → Analysis。
3. 对写操作只使用负责人指定目标；核对日志，不触碰未授权用户或电桩。
4. 再以 `OFFLINE_DEMO=1` 启动，只确认模拟标识和导航。

### 6.6 安装、测试并启动 Web

```bash
cd dashboard
npm ci
npm run test:run
npm run build
npm run dev
```

打开终端实际输出的 URL，按 W01–W05 清单检查三区域、六类容器、DataV、空态和动效开关。保存 Node/npm 版本、9/9 只是最近参考值，本次必须记录实际 Vitest 数量和 build warning。

### 6.7 记录失败和耗时

```text
step:
command/action:
start/end/elapsed:
expected:
actual:
status: PASS | FAIL | BLOCKED | NOT_RUN
log/screenshot:
first failing message:
environment difference:
workaround supplied by author: NONE（若非 NONE，则 S2-T24 不通过）
```

### 6.8 复跑签收

- [ ] 候选提交可获取，工作树干净。
- [ ] 依赖版本满足要求，无现场首次下载风险。
- [ ] Qt configure/build 和 Member2 tests 可复现。
- [ ] 两套 Qt 客户端可按固定顺序操作。
- [ ] Web test/build/dev 可复现。
- [ ] S2-T22 分辨率、动效和断网项目有状态与证据。
- [ ] 使用 [演示脚本](stage2-demo-script.md) 完成单人计时演练。
- [ ] 演练总时长为 10–11 分钟。
- [ ] 没有作者口头补丁；材料中的 commit、状态和实际版本一致。
- [ ] 复跑人、日期、总耗时和所有失败均已记录。

只有上述项目实际完成，S2-T24 才能标 `PASS`。当前状态保持 `NOT_RUN`。
