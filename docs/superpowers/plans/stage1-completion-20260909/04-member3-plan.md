# 成员3：模拟器、只读分析服务和浅色大屏实施计划

**Goal:** 让设备上报驱动真实模拟业务库，并让大屏准确显示该库的变化，保留二阶段数据治理和训练代码。

**Architecture:** 模拟器→C++服务端→SQLite；FastAPI以只读连接查询同一SQLite→Web。所有业务写入仅由成员1服务端执行。

**Tech Stack:** 现有C++17模拟器、Python/FastAPI/sqlite3/Decimal、Vue3/TypeScript/ECharts/Vitest。不为本轮换技术栈。

> 执行前阅读01统一契约。本文新增文件名为建议；发现等价实现时复用并记录映射。每项修复先增加能重现问题的失败测试，再修改代码、运行针对性测试。

## 1. 审核结论：不要重复开发已有部分

| 实际文件 | 已有能力 | 本轮应修正 |
| --- | --- | --- |
| `analytics/src/sqlite_dashboard.py` | 已用SQLite `mode=ro`和query_only；告警读取真实表 | 不是重新建立数据库接入，而是修正路径、统计、时间、预测和质量来源 |
| 同上 | 营收/用量查询、桩排名 | CAST AS REAL损失十进制口径；非OFFLINE当在线；不可用全算利用率；SUM瞬时功率不是小时电量 |
| `analytics/src/api.py` | 已有health/dashboard/forecast/recommend | health仅检查文件存在；forecast为固定日期数列；recommend硬编码且允许请求参数决定trained标签 |
| `dashboard/src/adapters/dashboardDataAdapter.ts` | 已有真实fetch及网络失败保留旧数据 | 仅检查HTTP，不验证响应业务状态；HTTP200错误体可能覆盖正常数据；显式state路径可返回样例 |
| `simulator/pile/main.cpp` | 已有TCP发送、场景和返回码 | 有限帧默认结束；requestId跨运行重复；字符串匹配JSON成功响应不可靠 |
| `analytics/src/ingest_raw.py`、`build_curated.py`、`model.py`等 | 已有二阶段基础 | 保留，不把与当前运行库无关的质量报告或模型文件冒充实时结果 |

以上为本轮定向源码审核，不是已运行新验收。`PROJECT_DIR=parents[2]`在analytics/src文件中指向analytics目录，默认库路径可能与仓库runtime不同；正式模式强制显式绝对数据库路径，缺失即报告配置错误。

## 2. 所有权和交付依赖

可写 `simulator/`、`analytics/`、`dashboard/`及各自测试/说明。禁止修改server/shared/database、成员2目录和根CMake；向成员1提交所需根目标、环境配置、迁移依赖清单。

先收到成员1冻结的PILE鉴别、基准、异常、状态及数据库字段样例，再完成真实服务联通。可提前独立做纯函数统计测试、响应解析、页面错误态，但不能自行猜字段并宣布联调通过。

## 3. M3-01：可靠、持续且可控的设备模拟

**文件：** `simulator/pile/main.cpp`、`pile_simulator.*`、`protocol_frame.*`、`scenario.*`；`simulator/tests/scenario_test.cpp`、`tcp_loopback_test.py`。

1. [ ] 测试响应JSON含空白、字段重排、分包、两帧粘连、错误code、错误requestId、连接关闭。当前字符串查找应被测试击中。复用已有合格JSON库；若新增依赖，优先仓库已有Qt Core的QJsonDocument并向成员1说明构建依赖，不自行联网拉入不明库。
2. [ ] 严格解析4字节大端长度与完整JSON，限制帧长。只接受匹配请求且ok=true的响应；错误输出code和requestId，不输出令牌。
3. [ ] 每次进程运行生成新会话标识，requestId由会话标识+序号组成。一次网络结果未知的重试复用同一请求ID、设备时间、读数；不改时间后当成同一请求。
4. [ ] deviceToken从非跟踪配置/环境读取，无配置拒绝正式连接。模拟器不登录用户、不调用ORDER_TELEMETRY、不直写业务库。
5. [ ] 保留有限场景用于测试；新增明确的持续运行选项，默认演示由启动器显式选择持续正常场景。固定间隔发送，收到停止信号正常退出；连续累计读数，不因场景切换归零。
6. [ ] 正常累计增长用十进制或整数最小计量单位生成后格式化字符串，禁止二进制浮点形成账单输入。单位和字段服从冻结协议；生成变化率应与功率、时间相容，快速测试可注入逻辑时钟，不能把不合理样例当真实设备性能。
7. [ ] 提供可重复触发的normal、overheat、timeout、command-failure和recovery操作；timeout真的停止发送，不仅更改界面文字；recovery恢复正常读数且继续累计。场景命令只能影响本模拟器拥有的桩。
8. [ ] 对无定义的command-failure先核对现有协议：若只是本地发送失败，按通信错误展示，不伪造服务端已执行重启失败；需要设备命令回执时向成员1提交明确接口差异并由其实现。

**通过：** 连续运行超过两个离线阈值仍在线；真实服务端仅由PILE上报增加活动订单用量；同帧重发不增加记录/费用；停止发送后产生离线状态；恢复不复活已结束充电单。旧scenario与loopback测试通过。

## 4. M3-02：同库只读服务和可信健康检查

**文件：** `analytics/src/sqlite_dashboard.py`、`api.py`、`analytics/tests/test_api.py`、`test_service_isolation.py`；新增建议 `analytics/tests/test_live_dashboard.py`。

1. [ ] 正式启动要求CHARGE_PLATFORM_DB为存在的绝对路径，连接必须mode=ro、query_only。不能自动新建空库、初始化seed或回退CSV。
2. [ ] health执行实际只读查询并检查必要迁移/表列，返回ready或非2xx及稳定错误码；路径存在但不是SQLite、schema不全、无权限均不可ready。公开响应不泄露绝对路径；详细本机日志脱敏。
3. [ ] 为一张看板使用短只读事务获得一致快照，结束即关闭连接，不长时间占锁。配置有限busy_timeout，与请求预算一致；不让同步SQLite查询阻塞异步事件循环中的所有请求，复用FastAPI同步端点/线程执行方式。
4. [ ] 正常HTTP响应提供state、source、业务数据时间、查询时间、统计窗口和版本。数据时间不能每次查询都伪装成“刚更新”。
5. [ ] 无法查询返回HTTP503和稳定错误体；Web据此保留上次成功快照并标过期。空库有效查询属于empty，不混同故障。
6. [ ] 用临时库证明API不能INSERT/UPDATE；启动和全部查询后业务表未改变。测试缺库、损坏库、迁移缺失和写锁时的限时失败，不对旧演示库制造故障。

**通过：** C++与API配置指向同一候选数据库；新支付能从API查到；错误清楚、无静默备用数据、无业务写入口。

## 5. M3-03：逐指标修正算法

**文件：** sqlite_dashboard；建议新增 `analytics/src/live_metrics.py`集中纯计算，新增 `analytics/tests/test_live_metrics.py`。小数据阶段可读取必要列用Decimal汇总；不得SQL CAST AS REAL后再转Decimal。

| 指标 | 实现和筛选 | 测试断言 |
| --- | --- | --- |
| 今日营收 | FINISHED，成功支付时间落在UTC日；Decimal金额和 | 0.10+0.20=0.30；充值不计入；跨UTC午夜归属正确 |
| 用量 | 明确累计订单用量或时段设备用量，标题和来源一致 | 两种指标不能共用一个含糊“总用电量”字段 |
| 在线率 | 最近15秒有效接收心跳/全部设备，UNKNOWN单列 | 从未心跳不在线；FAULT但心跳正常可在线 |
| 充电/占用 | 订单CHARGING为正在充电，PENDING_PAYMENT另列 | 待支付不增加正在充电数，但不能预约 |
| 可服务 | 无占用、健康、在线、无管理禁用 | FAULT/OFFLINE/UNKNOWN均不能算空闲可服务 |
| 小时电量 | 有效相邻累计差按小时重叠时间分摊，Decimal守恒 | 09:59:55为100.00、10:00:05为100.02：相邻两小时各0.01 |
| 平均功率 | 有效时间区间加权，不求瞬时样本和 | 恒定1000W加密采样后仍约1000W，而非样本越多越大 |
| 质量 | 当前库样本的缺失、非法、重复、覆盖与延迟；注明窗口 | 不用CSV报告给SQLite贴“高质量”标签；无样本为未知 |

补充测试：完全重复读数不重复累计；同时间冲突/倒退拒绝计入；超过15秒缺口标未知，不跨缺口插值；时区不同但同一时刻排序一致；量化余差分配后各段之和等于有效总增量；空数据不除零。统计窗口必须包括边界之前一条有效读数，才能计算首个区间，不能直接截断后丢失边界用量。

推荐先按桩排序、逐相邻区间扫描，复杂度O(n log n)排序+O(n)扫描；现有索引足够则用SQL有序读取。不要在小规模第一阶段引入Spark替代此逻辑。第二阶段保留相同输出契约，再按真实数据量替换执行引擎。

## 6. M3-04：诚实且可用的预测、推荐

**文件：** api、recommend/model相关适配、`test_recommend.py`、`test_api.py`。不删除既有训练实现。

1. [ ] 去除固定2026-09-04预测和复制actual作forecast。第一阶段使用最近完整有效小时的移动平均（建议最多3个，至少3个完整样本）；未来1/6/24小时返回带UTC时间的基线预测，明确不是训练模型。
2. [ ] 不足3个有效小时返回INSUFFICIENT_DATA，UI显示缺样本原因。预测点的observed值为空，不伪造未来实际值；缺口不当0。
3. [ ] trained标签只能由服务端确实加载、验证并使用相应模型后给出，不受model_available请求参数控制；若一阶段只启用baseline，所有相关端点一致。
4. [ ] 推荐读取真实站点、可服务桩及合法位置；无用户位置不显示0公里假距离。复用已有距离算法并测试经纬度边界；只推荐可服务站点，按距离/可服务数等透明规则排序，给出原因。
5. [ ] 无排队数据不写“等待3分钟”等未经定义的估计，可显示“忙碌/无可用桩”。全站不可服务时返回空推荐及原因，不推荐故障站凑数。
6. [ ] 原创新功能/模型入口保留；阶段1呈现规则/基线和来源限制，阶段2接入真实评测。无法按原必选需求落实的项目登记未完成，不擅自降级为可选。

**通过：** 相同输入可重复，输入变化影响推荐/预测；来源与标签相符；无数据、全不可用、损坏模型均有明确结果。

## 7. M3-05：浅色大屏联动与页面状态

**文件：** `dashboard/src/App.vue`、adapters、现有六个业务组件和common组件、styles；`dashboard/tests/dashboard.test.ts`。

1. [ ] 正式入口不允许query/state参数无提示切成fixture；样例模式单独明确入口，整页标OFFLINE_DEMO。审核App调用链后再修改，不删开发测试夹具。
2. [ ] 适配层验证必需字段和state/source，HTTP200但api-error也按失败处理，不能覆盖上次成功快照。账户/金额文本保持服务端字符串。
3. [ ] 每2秒轮询一次，无请求重叠；切页/卸载取消请求和定时器，旧响应不能覆盖较新响应。单次超时预算应与API查询预算匹配，目标正常更新不超过3秒；慢机实测不达标需修复或报告失败，不能改时间戳掩盖。
4. [ ] 组件覆盖loading/empty/error/stale/recovered；失败保留旧数据并显示“最后成功更新时间”，恢复自动清除错误。API停止时不要跳回漂亮假数据。
5. [ ] 告警数量从返回的总数/列表语义计算，不写固定数字；若分页，不能把当前页条数标为全量总数。图例、单位、时区、预测来源、未知状态要清楚。
6. [ ] 保持已确定的浅色背景、蓝色主视觉，可有适量渐变/动效；尊重减少动画设置。1920×1080和1366×768检查不遮挡、不横向溢出；不用设计重做代替接入工作。
7. [ ] 真API联动测试与Vitest夹具测试分开记录。支付、故障、处理之后截图，并记录事件时间和看板可见时间；截图只证明画面，数据正确性还需SQL/API断言。

## 8. 模块命令与交付

在候选工作树执行，先确认解释器和依赖已存在，缺依赖按正常授权安装到项目隔离环境，不改全局环境：

```bash
# analytics目录；python替换为已核对的项目虚拟环境解释器
python -m pytest -q
# dashboard目录；存在匹配依赖时无需反复安装
npm run test:run
npm run build
# simulator目录之外的候选根；只用于独立模块验证
cmake -S simulator -B build-simulator-stage1 -DBUILD_TESTING=ON
cmake --build build-simulator-stage1 -j2
ctest --test-dir build-simulator-stage1 --output-on-failure --no-tests=error
```

独立CMake若提示BUILD_TESTING未使用，向成员1提交测试注册方式，不据此声称根CTest已覆盖模拟器；现有loopback脚本未注册时需明确运行并交日志。

交付：改动文件清单、真实服务端往返样例、指标定义及算例、错误恢复截图、准确命令与退出码、剩余问题。提交模块通过不等于整组通过，必须参加05中的同库正常/故障验收。
