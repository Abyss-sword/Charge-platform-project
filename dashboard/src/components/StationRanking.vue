<!-- 站点排行逻辑：只接收 items。 -->
<script setup lang="ts">
defineProps<{ items: { id: string; name: string; utilization: number; available: number }[] }>() // 排行数据来自视图模型，组件只负责展示。
</script> <!-- 结束脚本区。 -->

<template> <!-- 开始模板。 -->
  <!-- 站点利用率排行，地图失败时也用这一块替代。 -->
  <article id="ranking" class="panel">
    <!-- 标题与 Top 3 徽章。 -->
    <header>
      <!-- 左列。 -->
      <div>
        <!-- 英文眉题。 -->
        <p class="eyebrow">利用率</p>
        <!-- 中文标题。 -->
        <h2>站点排行</h2>
      </div> <!-- 结束容器。 -->
      <!-- 固定文案 Top 3，与接口条数无关。 -->
      <span class="status-chip">Top 3</span>
    </header> <!-- 结束标题行。 -->
    <!-- 有序列表：名次、站名、进度条、利用率。 -->
    <ol class="ranking">
      <!-- 按下标 +1 显示名次。 -->
      <li v-for="(item, index) in items" :key="item.id">
        <!-- 名次徽章。 -->
        <span class="rank">{{ index + 1 }}</span>
        <!-- 站名、空闲桩、利用率条。 -->
        <div>
          <!-- 站点名称。 -->
          <strong>{{ item.name }}</strong>
          <!-- 站点 ID 与空闲桩数。 -->
          <small>{{ item.id }} · 空闲 {{ item.available }} 桩</small>
          <!-- 灰底条，宽度等于利用率百分比。 -->
          <div class="bar"><i :style="{ width: `${item.utilization}%` }" /></div>
        </div> <!-- 结束容器。 -->
        <!-- 右侧百分比数字。 -->
        <b>{{ item.utilization }}%</b>
      </li> <!-- 结束列表项。 -->
    </ol> <!-- 结束有序列表。 -->
  </article> <!-- 结束卡片。 -->
</template> <!-- 结束模板。 -->
<!--
【文件讲解·答辩用】
功能作用：按利用率列出站点，并显示空闲桩数。地图资源不可用时，大屏用这块代替地图。
技术原理：纯列表。进度条宽度绑 utilization，不做前端再排序，排序在分析服务完成。
代码思路：ol + v-for；key 用站点 id；名次用 index+1。徽章写死 Top 3 是原 UI 文案，逻辑未改。
与其他文件关系：App.vue 传入 view.ranking。样式在 tokens.css 的 .ranking。点顶部「地图失败」只改 PageState 文案，列表仍渲染。
答辩时可以怎么讲：这是站点对比，不是桩遥测。要看单桩温度请看 TelemetryPanel。场景切换不会在这块凭空加站，站点来自统一库聚合。
-->
