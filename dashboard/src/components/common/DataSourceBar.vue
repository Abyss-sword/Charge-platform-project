<!-- 顶栏逻辑：只读 view 元数据，并切换白昼/黑夜。 -->
<script setup lang="ts">
import type { DashboardViewModel } from '../../adapters/dashboardDataAdapter' // 只要类型，顶栏不自己请求接口。
import { useUiTheme } from '../../theme' // 界面模式与图表共用。
defineProps<{ view: DashboardViewModel }>() // 整份视图模型传入，读取元数据字段。
const { theme, setTheme } = useUiTheme() // 白昼/黑夜开关。
</script> <!-- 结束脚本区。 -->

<template> <!-- 开始模板。 -->
  <!-- 大屏顶栏：品牌 + 数据源元信息。 -->
  <header class="topbar">
    <!-- 左侧品牌：E 标与中文标题。 -->
    <div class="brand"><span class="brand-mark">E</span><div><p>NEUSOFT · EV CLOUD</p><h1>充电桩运营数据大屏</h1></div></div>
    <!-- 右侧：界面模式、数据模式、连接和刷新时间。 -->
    <div class="source-meta">
      <!-- 白昼浅色 / 黑夜暗色，只改外观，不改接口数据。 -->
      <div class="theme-switch" role="group" aria-label="界面模式">
        <button type="button" :class="{ active: theme === 'day' }" @click="setTheme('day')">白昼</button>
        <button type="button" :class="{ active: theme === 'night' }" @click="setTheme('night')">黑夜</button>
      </div>
      <!-- 数据模式胶囊；stale 时改琥珀色，表示不是最新成功帧。 -->
      <span class="mode-tag" :class="{ stale: view.stale }"><i />{{ view.dataMode }}</span>
      <!-- S1-D：联动快照版本，旧响应按此丢弃。 -->
      <div><small>快照版本</small><strong>v{{ view.snapshotVersion }}</strong></div>
      <!-- 分析服务刷新时间。 -->
      <div><small>更新时间</small><strong>{{ view.updatedAt }}</strong></div>
      <!-- 连接文案：已连接 / 失败 / 演示；refreshing 走最后的演示分支原文逻辑。 -->
      <div><small>数据连接</small><strong>{{ view.sourceStatus === 'connected' ? 'SQLite已连接 · 3秒刷新' : view.sourceStatus === 'failed' ? '连接失败' : '演示状态' }}</strong></div>
      <!-- 版本、覆盖、业务时间和区域不占首屏，需要时再展开。 -->
      <details class="source-more">
        <summary>详情</summary>
        <dl>
          <div><dt>数据集版本</dt><dd>{{ view.datasetVersion }}</dd></div>
          <div><dt>快照时间</dt><dd>{{ view.snapshotAtUtc }}</dd></div>
          <div><dt>覆盖时间</dt><dd>{{ view.coverage }}</dd></div>
          <div><dt>业务数据时间</dt><dd>{{ view.dataTimestamp }}</dd></div>
          <div><dt>区域</dt><dd>{{ view.region }}</dd></div>
        </dl>
      </details>
    </div> <!-- 结束容器。 -->
  </header> <!-- 结束标题行。 -->
</template> <!-- 结束模板。 -->
<!--
【文件讲解·答辩用】
功能作用：告诉观众现在连的是哪份数据、是否陈旧、多久刷新一次。它不是场景切换条。
技术原理：纯展示。sourceStatus 三分支保持原逻辑：connected 显示 SQLite 已连接；failed 显示连接失败；其余（demo/refreshing）显示「演示状态」。
代码思路：品牌固定；右侧全部绑 view 字段。stale 只改 mode-tag 颜色，不改数字。
与其他文件关系：App.vue 最上方引用。字段由 dashboardDataAdapter.ts 填充。实时时来自 /api/dashboard；顶部预览时 datasetVersion 会变成 preview-only。
答辩时可以怎么讲：先让老师看「SQLite已连接 · 3秒刷新」。若刚点了「加载中」，这里会变成演示状态——说明那是 UI 预览。真正切桩请用下方 ScenarioSwitcher。
-->
