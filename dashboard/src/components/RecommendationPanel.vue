<!-- 推荐站点逻辑：展示 reason，不重算排序。 -->
<script setup lang="ts">
defineProps<{ items: { name: string; wait: number; distance: number; available: number; reason: string }[]; modelMode: 'trained' | 'baseline' }>() // 推荐列表 + 是否走训练模型。
</script> <!-- 结束脚本区。 -->

<template> <!-- 开始模板。 -->
  <!-- 推荐站点面板；排序在分析服务完成，这里只展示 name / wait / reason。 -->
  <article id="recommend" class="panel recommendation-panel">
    <!-- 标题与规则/预测徽章。 -->
    <header>
      <!-- 左列。 -->
      <div>
        <!-- 眉题强调可解释。 -->
        <p class="eyebrow">可解释分流</p>
        <!-- 主标题。 -->
        <h2>推荐站点</h2>
      </div> <!-- 结束容器。 -->
      <!-- trained 显示预测参与排序，否则基础规则。 -->
      <span class="status-chip">{{ modelMode === 'trained' ? '预测参与排序' : '基础规则' }}</span>
    </header> <!-- 结束标题行。 -->
    <!-- 推荐列表。 -->
    <ul>
      <!-- 名次圆点 + 站名 + 原因。 -->
      <li v-for="(item,index) in items" :key="item.name">
        <!-- 名次从 1 起。 -->
        <span>{{ index + 1 }}</span>
        <!-- 详情。 -->
        <div>
          <!-- 站名。 -->
          <strong>{{ item.name }}</strong>
          <!-- 距离、等待、空闲桩。 -->
          <small>{{ item.distance }} km · 等待 {{ item.wait }} 分钟 · 空闲 {{ item.available }} 桩</small>
          <!-- 可解释原因，来自后端字符串。 -->
          <p>{{ item.reason }}</p>
        </div> <!-- 结束容器。 -->
      </li> <!-- 结束列表项。 -->
    </ul> <!-- 结束列表。 -->
  </article> <!-- 结束卡片。 -->
</template> <!-- 结束模板。 -->
<!--
【文件讲解·答辩用】
功能作用：按等待、距离、空闲桩给出推荐站点，并展示 reason，避免黑盒排序。
技术原理：排序和文案在分析服务完成。modelMode 只决定徽章文字。根页从 /api/dashboard 的 recommendations 取值，不打 raw 目录。
代码思路：v-for 按站名当 key；名次 index+1；三行信息：名、量化指标、原因。
与其他文件关系：类型在 dashboardDataAdapter.ts；样式 .recommendation-panel 在 tokens.css；App.vue 次栅格挂载。
答辩时可以怎么讲：推荐理由是后端写好的中文，前端不重算。大屏实时推荐按库里空闲桩；/recommend 是公开聚合分析接口，两套不要混称。
-->
