<!-- 工单列表逻辑：只接收 items。 -->
<script setup lang="ts">
defineProps<{ items: { // 工单数组，字段与后端工单表投影一致。
  id: string; alarmId: string; pileSn: string; type: string // 工单号、告警号、桩号、类型。
  assignee: string; status: string; updatedAt: string // 处理人、状态、更新时间。
}[] }>() // 结束工单 props 声明。
</script> <!-- 结束脚本区。 -->

<template> <!-- 开始模板。 -->
  <!-- 最新运维工单。 -->
  <article id="tickets" class="panel ticket-panel">
    <!-- 标题与条数。 -->
    <header>
      <!-- 左列。 -->
      <div>
        <!-- 眉题强调告警→工单链路。 -->
        <p class="eyebrow">告警处置链路</p>
        <!-- 主标题。 -->
        <h2>最新运维工单</h2>
      </div> <!-- 结束容器。 -->
      <!-- 条数随 items.length 变化。 -->
      <span class="status-chip">{{ items.length }} 条</span>
    </header> <!-- 结束标题行。 -->
    <!-- 有工单才列出。 -->
    <ul class="tickets" v-if="items.length">
      <!-- 一行一张工单。 -->
      <li v-for="item in items" :key="item.id">
        <!-- 左：桩号、类型、编号。 -->
        <div><strong>{{ item.pileSn }} · {{ item.type }}</strong><small>{{ item.id }} / {{ item.alarmId }}</small></div>
        <!-- 右：状态、处理人、时间。 -->
        <div><b>{{ item.status }}</b><small>{{ item.assignee }} · {{ item.updatedAt }}</small></div>
      </li> <!-- 结束列表项。 -->
    </ul> <!-- 结束列表。 -->
    <!-- 空态，不造假工单。 -->
    <p v-else class="panel-empty">暂无运维工单</p>
  </article> <!-- 结束卡片。 -->
</template> <!-- 结束模板。 -->
<!--
【文件讲解·答辩用】
功能作用：展示告警转化成的运维工单，证明平台不只报警，还有处置记录。
技术原理：只读列表。id/alarmId 同时展示，答辩时可以对着 AlarmFeed 的编号讲链路。空数组走「暂无运维工单」。
代码思路：v-if/v-else 分有无数据；左列身份、右列状态，投影时好扫。
与其他文件关系：App.vue 传入 view.tickets。过热场景由 ScenarioSwitcher 触发后，后端落告警和工单，再经 /api/dashboard 进来。
答辩时可以怎么讲：指「PILE-xxx · OVERHEAT」和旁边 OPEN/未分配。强调工单不是前端生成，是统一库存量。
-->
