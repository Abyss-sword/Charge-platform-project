# Member2 Stage2 分析闭环状态

更新时间：2026-09-17。本文只记录 Member2 代码侧接入状态；真实运行产物和人工验收不以契约测试代替。

## 当前结果

| 验收项 | 状态 | 证据与剩余条件 |
| --- | --- | --- |
| S2-T11 | PARTIAL | Qt/Web 已接入 `forecast_v1` 的 1/6/24 小时路径；三端真实数值对账待 VM 运行。 |
| S2-T12 | BLOCKED_BY_DATA | Member3 正式响应仍明确 `freePilesExpected=null` / `NO_CAPACITY_DATA`。 |
| S2-T13 | BLOCKED_BY_ARTIFACT | `/analysis/peak` 已消费；缺少发布产物 `peakThresholds` 时 UI 显示 UNKNOWN，不推导 P90。 |
| S2-T14 | PARTIAL | `member3_recommendation_v1` 基础字段可消费；预测可用率和到达时刻评分仍由服务端标明不可用。 |
| S2-T15 | PARTIAL | User/Admin/Web 均保留 unavailable、empty、expired 边界；真实接口联调和三端签收待 VM。 |
| S2-T16 | PARTIAL | Web 已隔离 online/history/quality 并展示版本、runId、source；最终跨端一致性待真实产物。 |
| S2-T17 | PASS | Qt `AnalysisHttpClient` 支持超时、网络/HTTP错误、generation 丢弃晚到响应；Web 使用 AbortController 丢弃离开页面后的响应。 |
| S2-T18 | PARTIAL | Remote 不 fallback Mock，EXPIRED 保留最后成功数据；物理断网、重连人工验收尚未执行。 |
| S2-T22 | PARTIAL | 自动化覆盖状态、页面结构和分辨率 sanity；多分辨率、DPI、浏览器缩放需人工复核。 |
| S2-T24 | NOT_RUN | 非作者复跑、10~11 分钟演示和录屏仍待执行。 |

## 代码 / 契约 / 产物 / 运行分层

- CODE_COMPLETE：Qt `AnalysisHttpClient`、ForecastPanel、User/Admin/Web 接入容器和 JSON 错误边界已具备。
- CONTRACT_READY：`forecast_v1`、`member3_recommendation_v1`、Hive、peak 响应按最新代码解析。
- ARTIFACT_PENDING：P90 `peakThresholds`、容量/占用标签等发布产物缺失时保持 unavailable。
- DATA_PENDING：容量预测、预测推荐可用率和三端真实值对账等待正式数据。
- MANUAL_ACCEPTANCE_PENDING：断网、DPI、浏览器缩放、非作者复跑、演示计时。

## 可复现命令

```bash
cmake --build build-stage2 --parallel 2
ctest --test-dir build-stage2 -L '^member2$' --output-on-failure
cd dashboard && npm test -- --run && npm run build
cd .. && .venv/bin/python -m pytest analytics/tests
```
