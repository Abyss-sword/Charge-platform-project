<!-- 告警列表逻辑：只接收 items。 -->
<script setup lang="ts">
defineProps<{ items: { id: string; severity: '严重' | '警告' | '提示'; message: string; time: string }[] }>() // 告警列表；级别用中文联合类型。
</script> <!-- 结束脚本区。 -->

<template> <!-- 开始模板。 -->
  <!-- 最新告警面板。 -->
  <article id="alarms" class="panel alarm-panel">
    <!-- 标题与待处理徽章。 -->
    <header>
      <!-- 左列。 -->
      <div>
        <!-- 英文眉题。 -->
        <p class="eyebrow">实时事件</p>
        <!-- 中文标题。 -->
        <h2>最新告警</h2>
      </div> <!-- 结束容器。 -->
      <!-- 原 UI 固定「2 条待处理」，不按 items.length 改写。 -->
      <span class="status-chip alert">2 条待处理</span>
    </header> <!-- 结束标题行。 -->
    <!-- 告警列表。 -->
    <ul class="alarms">
      <!-- 每条告警用 id 当 key，class 用中文级别上色。 -->
      <li v-for="item in items" :key="item.id" :class="item.severity">
        <!-- 严重×、警告△、提示 i。 -->
        <span class="alarm-dot">{{ item.severity === '严重' ? '×' : item.severity === '警告' ? '△' : 'i' }}</span>
        <!-- 级别 + 文案，辅以编号和时间。 -->
        <div>
          <!-- 主文案。 -->
          <strong>{{ item.severity }} · {{ item.message }}</strong>
          <!-- 编号与时间。 -->
          <small>{{ item.id }} · {{ item.time }}</small>
        </div> <!-- 结束容器。 -->
      </li> <!-- 结束列表项。 -->
    </ul> <!-- 结束列表。 -->
  </article> <!-- 结束卡片。 -->
</template> <!-- 结束模板。 -->
<!--
【文件讲解·答辩用】
功能作用：滚动展示最新告警，证明过热等场景已经写入统一库。
技术原理：展示层。li 的 class 就是「严重/警告/提示」，CSS 按中文类名上色。徽章文案保持原样「2 条待处理」。
代码思路：v-for 渲染 message/time；符号用三元表达严重程度，方便投影一眼区分。
与其他文件关系：App.vue 传入 view.alarms。过热场景由 ScenarioSwitcher 触发模拟器，再经 /api/dashboard 刷新进来。工单在 TicketPanel，靠 alarmId 对应。
答辩时可以怎么讲：点「高温告警」后看这里是否出现过热文案。不要点顶部「接口失败」来演示告警——那是 UI 预览，会清空或换成 demo 文案。
-->
