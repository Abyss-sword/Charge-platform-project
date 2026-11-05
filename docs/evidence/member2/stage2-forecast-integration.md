# Member2 分层门禁复核与 Qt forecast 接入

日期：2026-09-16。分支：`feature/member2-stage2-ui`。
HTTP 基础提交：`a98c4ec`；页面实现见本文件所在提交。

## 1. 远端证据及判断口径

本轮已直接查询 `git ls-remote origin refs/heads/feature/member3-simulator-analytics-dashboard`，
并显式更新跟踪引用，两者均为 `442e9e06a542c56a84fe481f23d74e9d3109514f`。
默认 fetch refspec 只覆盖 integration 分支，不能把旧跟踪引用当成远端最新提交。

```bash
git fetch --prune origin +refs/heads/feature/member3-simulator-analytics-dashboard:refs/remotes/origin/feature/member3-simulator-analytics-dashboard
git rev-parse origin/feature/member3-simulator-analytics-dashboard
git log --oneline 714cbaf..origin/feature/member3-simulator-analytics-dashboard
```

新增提交：

| 提交 | 作用 |
|---|---|
| c600b10 | 提交预测窗口对齐 ICR |
| c78aec5 | 未覆盖站点预测降级处理 |
| ff1a97e | 真实 VM 验收证据 |
| 1f16c72 | 发布 Hive 业务报表接口 |
| 442e9e0 | 用 VM 结果更新验收记录 |

Member3 的 MR/Spark 成功、Hive 6715 行/19 站、90213 行时间轴、29898 训练样本、
24 个模型、MAE、五接口 HTTP 200、106 tests 和 4 regression 证据已存在。
这些是 Member3 提交的真实运行证据，不是本轮在当前机器重新执行的结果；不再使用“Member3 工作未完成”作为整体结论。
后加的 Hive 发布改动与先前 VM 测试版本也不能混为同一次验证。

依据：Stage2 文档 worktree 的 `plans/00-总规格与决策.md`（预测与推荐定义）、
`plans/01-技术实施与任务包.md`（T07、800ms 总等待上限）、
`plans/03-验收与Agent交接.md`（T11～T18）、`plans/07-新规格成员分工与交接补充-20260914.md`。
具体服务核查对象为远端 `analytics/src/flask_api.py`、`forecast_service.py`、推荐实现及相应契约/测试/VM 证据。

## 2. 分层解除阻塞矩阵

READY 表示可消费相应能力，不表示整个 Stage2 验收已经签收。

| 能力 | 当前状态 | 具体缺口 / 当前可做事项 |
|---|---|---|
| AnalysisHttpClient 基础 | READY | 已实现 Qt Network 异步客户端，不等待推荐 schema |
| /health | READY | GET，Flask/service/status/degraded/reasons 明确；已实现解析，不把健康降级当成全部路由禁用 |
| /forecast 正常路径 | READY | GET horizon=1/6/24、可选 stationId；原有 ForecastParser 可解析正常 forecast_v1 |
| UserAnalysisPage 1/6/24h | READY | 已接发布产物预测；分析站点输入与业务站点不做无依据映射 |
| 用户到达时间 | BLOCKED | API 未提供真正参与计算的 arrivedAt/originTime 参数；正式接入模式禁用控件并说明 |
| /recommend | BLOCKED | 虽已有动态业务快照候选，仍缺冻结 recommendation 版本/schema、null/时间/错误规则；不兼容旧返回 |
| Admin 未来负荷 | READY | 已接 1/6/24h 发布窗口及真实返回点，不声称服务器历史原点等于当前时刻 |
| 空闲桩预测 | BLOCKED | 无正式容量/占用标签，返回 null/NO_CAPACITY_DATA；不显示 0 或客户端推算值 |
| Top3 | BLOCKED | /analysis/peak 使用均值×1.20/最大值，不是正式未来24h稳定 Top3 + 训练历史P90；不自行派生替代 |
| 预测预警 | BLOCKED | 正式级别、原因、时间段、建议及阈值契约尚不齐全 |
| Web forecast | PARTIAL | /forecast 正常数据可消费；本轮仅完成 Qt，Web 绑定仍是 Member2 可继续工作，不是整体上游阻塞 |
| late-response | READY | 客户端 generation + 取消旧请求；切期限、站点、分区、离页重入隔离，local HTTP 测试通过 |
| timeout | READY | 默认最大 800ms，无自动重试；超时/拒绝连接/HTTP 错误可独立测试 |
| EXPIRED UI | READY | 同上下文保留最后成功正文并提示过期；跨上下文清理。真实服务过期触发仍需联调，不阻塞 UI |
| 三端一致性 | PARTIAL | 待窗口 ICR 最终签收、Web 绑定、三端同站/原点/版本对比；并非 transport 阻塞 |

六类状态已由正式 contract/fixture 冻结，允许客户端解析和契约测试。
当前服务未覆盖所有真实触发路径，不以此阻塞正常 forecast。
非整数参数可能返回简化 JSON，内部异常可能返回 HTML 500：客户端保留 HTTP 状态并显示错误，
不将其转换成伪造 forecast_v1。正常路径与 HTTP/网络错误层分开。

## 3. 使用方法与边界

分析服务由 Member3 独立部署。本轮没有 merge/cherry-pick Member3，也未更改其文件。
Member3 正式模块为 `analytics.src.flask_api`，默认 `127.0.0.1:8001`；需在其已准备产物的环境启动。
Qt 在既有 Remote 模式下读取环境变量 `CHARGE_ANALYSIS_BASE_URL`，默认 `http://127.0.0.1:8001`。

```bash
# 在已有正式分析服务的机器上运行；不是临时/模拟 API。
export CHARGE_ANALYSIS_BASE_URL=http://127.0.0.1:8001
./build-stage2/clients/user/charge_platform_user
# 或另一终端运行运营端，沿用原有登录与 TCP 配置：
./build-stage2/clients/admin/charge_platform_admin
```

进入分析页，选择 1/6/24 小时，分析站点 ID 可留空使用服务端默认发布站点，
或输入真实 namespaced ID；点击“查询 / 刷新”。不猜业务站点与历史站点映射。
请求仅包含 horizon 和可选 stationId，不发送到达时刻，不调用 /recommend、旧 FastAPI 或临时 URL。
OFFLINE_DEMO 不启用此 HTTP 连接，也没有 fixture fallback。

显示服务端返回的 datasetVersion、dataMode、modelMode、originTime、targetStart/End、generatedAt，
表格保留原始十进制字符串，图表按每个点的目标区间绘制并保留 null 缺口。
不添加一小时来“修正”服务端时间，不伪造 runVersion（现有 forecast_v1 无该字段）。
Remote 拒绝 sourceMode=FIXTURE；生成测试响应的 helper 只编入测试。

OK 显示正文；NO_DATA 清空；EXPIRED 在同一上下文保留上次正文；模型/依赖/参数错误隐藏正文。
PARAM_INVALID 不自动重试，显式查询按钮允许用户修正参数后重新提交。
更换站点/期限/服务地址、离开页面都取消当前请求并清除旧上下文。

## 4. 自动与人工证据

```bash
cmake -S . -B build-stage2 -DBUILD_TESTING=ON
cmake --build build-stage2 --parallel 2
ctest --test-dir build-stage2 -R '^(analysis_http_client_test|forecast_pages_http_test)$' --output-on-failure
ctest --test-dir build-stage2 -L '^member2$' --output-on-failure
ctest --test-dir build-stage2 --output-on-failure
git diff --check
```

新增 HTTP 测试覆盖六状态、health、完整/简化 HTTP400、HTTP500/503、畸形 JSON、
拒绝连接、超时、旧请求晚到、1→6→24 最后请求获胜、取消、错误上下文及 FIXTURE 拒绝。
新增页面测试覆盖状态正文、EXPIRED 缓存、站点清理、离页/重入/运营分区、到达时刻禁用、
精确时间绘图、360×780/480×1040/1180×720/1366×768/1920×1080 有数据布局。

这些使用 local fake HTTP，不依赖真实 Member3 进程，不把测试响应当成正式数据。
本机 Python 缺 Flask（`ModuleNotFoundError: No module named 'flask'`），本轮没有重跑 Member3 Flask 或模型链路。
因此区分：客户端 HTTP 契约测试通过；真实 Qt→Member3 VM 联调 NOT_RUN；Member3 已提交 VM 证据存在。

S2-T11/T15/T17：新增自动覆盖，不据此宣称完整联合验收通过。
S2-T16：Web/窗口签收和真实三端逐字段核对仍待完成。
物理断网、DPI/视觉标签、真实过期触发、性能分位数和三端签字仍需人工/联合验收。

最终构建与 CTest 数量见测试矩阵顶部的本轮记录。
