<!-- 状态横幅逻辑：状态键映射中文标题。 -->
<script setup lang="ts">
import type { DashboardState } from '../../adapters/dashboardDataAdapter' // 六种画面状态的联合类型。
defineProps<{ state: DashboardState; message: string }>() // 状态键 + 适配器给出的说明。
const labels: Record<DashboardState, string> = { loading: '加载中', normal: '正常', empty: '空数据', 'api-error': '接口失败 · 已降级', 'model-error': '模型失败 · 使用基线', 'map-error': '地图失败 · 已切换排行' } // 横幅主标题，和顶部按钮含义对应。
</script> <!-- 结束脚本区。 -->

<template> <!-- 开始模板。 -->
  <!-- 非 normal 时由 App.vue 显示这条状态栏。 -->
  <aside class="page-state" role="status">
    <!-- 主标题。 -->
    <strong>{{ labels[state] }}</strong>
    <!-- 详细原因。 -->
    <span>{{ message }}</span>
  </aside> <!-- 结束状态栏。 -->
</template> <!-- 结束模板。 -->
<!--
【文件讲解·答辩用】
功能作用：用一条横幅解释当前为什么不是完整实时画面：加载、空库、接口失败、预测不足、地图失败。
技术原理：role=status 便于读屏。文案映射写在前端，具体原因 message 来自适配器或接口。normal 时父组件不渲染本组件。
代码思路：labels 覆盖全部 DashboardState，避免漏 key。不在这里发请求、不切场景。
与其他文件关系：App.vue 在 view.state !== 'normal' 时传入 state 和 message。预览按钮会触发这些横幅；ScenarioSwitcher 不会直接改 PageState。
答辩时可以怎么讲：点顶部「接口失败」出现横幅，是验收预览。真接口挂了也会走同类横幅，但数字策略在 loadDashboard：有上次成功就陈旧展示。
-->
