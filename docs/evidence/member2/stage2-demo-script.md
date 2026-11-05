# Stage2 单人 10分30秒演示脚本

更新日期：2026-09-15

目标时长：10分30秒（允许范围 10–11 分钟）

形式：唯一讲解者连续展示，不拆成三份成员讲稿。

本脚本遵循 Stage2 文档的 10分30秒示例。现场不改配置、不首次安装依赖、不等待训练；备份录屏必须明确标注。讲解前根据 Member3 门禁选择且只选择分支 A 或 B，不能把契约 fixture 冒充真实预测。

## 1. 演示前 15 分钟预检（不计入答辩）

- [ ] `git rev-parse HEAD` 与材料候选提交一致：`TO_VERIFY`。
- [ ] `git status --short` 无输出。
- [ ] Qt 两端、Web、证据文件和必要终端已打开并按顺序排列。
- [ ] Member2 测试复跑结果已更新，不直接照抄旧数字。
- [ ] 车主/运营截图没有密码、令牌和完整手机号。
- [ ] Member1 的来源治理、HDFS/YARN/Hive 证据路径：`TO_VERIFY`。
- [ ] Member3 的 Spark、模型、Flask、评估和 datasetVersion 证据：`TO_VERIFY`。
- [ ] 若 Member3 门禁仍未解除，选择分支 B 并移除所有“真实预测成功”表述。
- [ ] 备份录屏文件可播放，画面注明“备份录屏”。

## 2. 10分30秒逐段脚本

| 时间 | 画面/操作 | 讲解词要点 | 证据 |
|---|---|---|---|
| 0:00–0:45 | 架构与分工页 | “Stage2 保留 Qt/TCP/SQLite 在线业务，并增加 HDFS、MapReduce、Hive、Spark、Flask 分析链。Member2 负责两套 Qt 交互和 Vue3/DataV 大屏；页面不直读数据库，也不固定预测值。” | 架构图、commit |
| 0:45–1:30 | 车主端 | 登录后快速展示首页、找桩、详情、导航、订单和历史；指出 Remote 断线不会回退 Mock，OFFLINE_DEMO 有显式标识。 | U01–U06 |
| 1:30–2:15 | 运营端 | 展示 Overview、Revenue、Piles、Orders、Alarm/Ticket、Logs；指出“预测风险”和“已发生设备故障”是两种语义。 | A01–A05 |
| 2:15–3:15 | 来源与质量 | “在线业务与历史分析严格隔离；来源、许可、拒绝原因和版本由治理链提供。未知或未授权数据保持 null/unavailable，不补假值。” | Member1 质量报告 `TO_VERIFY` |
| 3:15–4:30 | HDFS/YARN/Hive | 展示真实 HDFS 路径、Java YARN applicationId 和 Hive 查询结果；只陈述已实际跑过的状态。 | Member1 作业证据 `TO_VERIFY` |
| 4:30–5:30 | Spark 特征 | 展示 UTC 小时轴、缺失标记和防泄漏时间切分；强调 lag 不跨站，缺失不改成 0。 | Member3 Spark 证据 `TO_VERIFY` |
| 5:30–7:30 | 分析能力 | 按下文 A/B 分支演示 1/6/24h、负荷、空闲桩、未来高峰和状态降级。 | U07、A06、W03 或真实结果 |
| 7:30–8:45 | 推荐与 Qt/Web 一致性 | 按 A/B 分支展示推荐或不可用退路；Web 展示在线/历史/质量三区域、DataV 和来源/version/time 位置。 | U07、W01–W04 |
| 8:45–9:45 | 故障隔离与稳定性 | 展示关闭动效，说明开关不改数据；播放明确标注的断网/恢复或压测备份录屏。不可用证据就说 NOT_RUN/BLOCKED。 | W05、R01/R03/R04 `TO_VERIFY` |
| 9:45–10:30 | 测试、限制和总结 | 展示测试矩阵：“最近记录 Member2 17/17、User 11/11、Admin 4/4、Analysis 3/3、Responsive 5/5；最终数字以候选复跑为准。”随后列出仍 BLOCKED 项，不宣称全量完成。 | 测试矩阵、S2 状态表 |

## 3. 5:30–8:45 分支 A：Member3 API 已 READY

只有以下预检全部通过才能选择 A：正式 Flask 入口存在；`/forecast` 符合 forecast_v1；请求参数和六状态冻结；客户端共享 HTTP 已实现并测试；推荐 schema 已冻结后才演示推荐。

### 5:30–7:30

1. 车主端选择同一站点和预测原点，依次切换 1h、6h、24h。
2. 展示真实负荷、空闲桩、样本数、模型/基线误差、来源时间和 datasetVersion。
3. 运营端选择相同站点/原点/版本，展示未来负荷、空闲桩、Top3 高峰和预测预警。
4. 切到 Web 历史区，逐字段核对单位、数值、版本；不可用或 null 必须展示正式原因。
5. 若产物过期，保留最后成功内容并显示 EXPIRED；不得把过期结果说成最新。

讲解词：“这里展示的是同一发布版本的真实结果。预测风险只用于调度提示，不会控制设备，也不等于设备已经故障。”

### 7:30–8:45

1. 若 `/recommend` READY，展示动态候选、推荐理由、站点详情和导航入口。
2. 若 `/recommend` 未 READY，即使 `/forecast` 可用也不展示固定候选，转用分支 B 的推荐说明。
3. 展示 Qt 车主、Qt 运营、Web 三端的 station/origin/datasetVersion 记录。

## 4. 5:30–8:45 分支 B：Member3 API 仍 BLOCKED

这是当前默认分支。

### 5:30–7:30

1. 车主“智能推荐与预测”页展示到达时刻和 1/6/24h 控件。
2. 指出页面当前显示“分析服务暂不可用/等待正式数据”，点击“返回手动查找站点”证明基础业务入口仍可用。
3. 运营“预测预警”页切换未来负荷、空闲桩、Top3 高峰、预测预警四区；页面不出现固定数值。
4. 展示 `forecast_v1` 契约测试和六状态 presenter 测试结果；解释这些证明消费边界和 UI 状态，不证明真实模型/API 完成。

固定表述：“当前正式分析接口仍处于跨成员集成阶段。我们已经完成强类型契约、六状态、页面交互和无假数据边界，但不会用 fixture 或旧接口冒充真实预测。”

### 7:30–8:45

1. Web 展示在线业务、历史分析、质量/批次三区域和六类 ECharts 容器。
2. 展示 DataV 边框、浅色主题、来源/version/time 空位和动效开关。
3. 车主推荐保持不可用，并展示手动找桩/详情/导航退路。
4. 明确 S2-T16 三端真实数值一致性仍为 `BLOCKED BY MEMBER3`，不要展示 fixture 对比图。

## 5. 演示时禁止表述

- 不说“预测准确”而没有真实 MAE/RMSE、样本数和基线。
- 不把旧 FastAPI、fixture、固定站点、静态截图称为正式 Flask/DataV 运行结果。
- 不把空闲桩容器存在称为空闲桩模型完成。
- 不把离线单测通过称为三端联调通过。
- 不把备份录屏称为现场实时操作。
- 不说“全部 100% 完成”；按验收矩阵逐项报告 PASS/PARTIAL/BLOCKED/NOT_RUN。

## 6. 截图与录屏引用

完整编号和文件名见 [人工验收清单](stage2-manual-acceptance-checklist.md#5-截图与录屏清单)。答辩最少准备：

- U01、U02、U03、U04、U05、U06、U07。
- A01、A02、A03、A04、A05、A06。
- W01、W02、W03、W04、W05。
- 一段车主/运营断网恢复录屏；Web 正式断网录屏在 API 未接入时标 `BLOCKED`。
- 一段完整 10分30秒备份演练录屏，文件名建议 `R05-stage2-defense-10m30s.mp4`。

## 7. 演练记录

```text
candidate commit: TO_VERIFY
selected branch: A READY | B BLOCKED
speaker:
date:
start time:
end time:
elapsed:
over/under target:
verbal patch needed: YES / NO
screenshots complete: PASS / FAIL / BLOCKED
recording path:
incorrect or exaggerated claim found:
next correction:
final status: PASS | FAIL | BLOCKED | NOT_RUN
```

S2-T24 只有在非作者仅依靠仓库材料完成复跑和 10–11 分钟演练、且材料与候选版本一致后才能标 `PASS`。
