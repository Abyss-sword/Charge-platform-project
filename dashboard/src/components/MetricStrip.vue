<!-- KPI 带逻辑：接收 metrics，不在前端重算。 -->
<script setup lang="ts">
const icons = ['站', '桩', '●', '充', '次', '¥', '%', '!'] // 八个 KPI 的左上角记号，按下标取，多出来用 •。
defineProps<{ metrics: { label: string; value: string; unit: string; tone: string }[] }>() // 指标数组来自视图模型，组件不计算口径。
</script> <!-- 结束脚本区。 -->

<template> <!-- 开始模板。 -->
  <!-- 核心指标带，朗读名给无障碍和测试用。 -->
  <section class="metric-grid" aria-label="核心指标">
    <!-- 一张卡片一个指标；tone 决定顶边和图标颜色。 -->
    <article v-for="(item, index) in metrics" :key="item.label" class="metric-card rise" :class="`tone-${item.tone}`" :style="{ '--delay': `${index * 50}ms` }">
      <!-- 左侧色块图标。 -->
      <div class="metric-icon">{{ icons[index] || '•' }}</div>
      <!-- 名称、数值、单位；口径统一写在顶栏，卡片上不再重复。 -->
      <div><p>{{ item.label }}</p><strong>{{ item.value }}</strong><span>{{ item.unit }}</span></div>
    </article> <!-- 结束卡片。 -->
  </section> <!-- 结束面板。 -->
</template> <!-- 结束模板。 -->
<!--
【文件讲解·答辩用】
功能作用：第一眼的运营 KPI：站点数、桩数、在线率、订单、服务量、营收等。
技术原理：展示层不做除法。value/unit/tone 全是接口算好的。rise + --delay 做错落入场；系统减少动效时 motion.css 会关掉动画。
代码思路：v-for 按 label 当 key；icons 只是装饰。预览态也会渲染，但那是 getDashboardView 的固定数字，不是库。
与其他文件关系：App.vue 传入 view.metrics。颜色 class 在 tokens.css 的 .tone-* 。实时数字只能来自 /api/dashboard。
答辩时可以怎么讲：先说“这些卡片是总览”。若刚点过顶部「加载中」，说明那是骨架预览；回到「实时」后数字才会跟库走。切场景不一定立刻改站点总数，要看遥测和告警。
-->
