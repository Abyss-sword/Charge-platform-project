<!-- 桩状态分布：英文码转中文。 -->
<script setup lang="ts">
const labels: Record<string, string> = { // 英文状态码转中文。
  IDLE: '空闲', RESERVED: '已预约', CHARGING: '充电中', FAULT: '故障', OFFLINE: '离线', // 五态与库枚举对齐。
} // 结束labels。
defineProps<{ items: { status: string; count: number }[] }>() // 各状态台数，由后端聚合。
</script> <!-- 结束脚本区。 -->

<template> <!-- 开始模板。 -->
  <!-- 充电桩状态分布。 -->
  <article id="status" class="panel status-panel">
    <!-- 标题。 -->
    <header>
      <!-- 左列。 -->
      <div>
        <!-- 眉题。 -->
        <p class="eyebrow">实时设备状态</p>
        <!-- 主标题。 -->
        <h2>充电桩状态分布</h2>
      </div> <!-- 结束容器。 -->
      <!-- 标明口径来自统一库。 -->
      <span class="status-chip">统一数据库</span>
    </header> <!-- 结束标题行。 -->
    <!-- 两列网格列表。 -->
    <ul class="status-list">
      <!-- class 用英文小写，供 CSS 画左边框颜色。 -->
      <li v-for="item in items" :key="item.status" :class="item.status.toLowerCase()">
        <!-- 中文名 + 台数；未知码显示原文。 -->
        <span>{{ labels[item.status] || item.status }}</span><strong>{{ item.count }}</strong>
      </li> <!-- 结束列表项。 -->
    </ul> <!-- 结束列表。 -->
  </article> <!-- 结束卡片。 -->
</template> <!-- 结束模板。 -->
<!--
【文件讲解·答辩用】
功能作用：按 IDLE/RESERVED/CHARGING/FAULT/OFFLINE 统计有多少台桩，给运维一个结构视图。
技术原理：计数在分析服务完成。前端只做字典翻译和按状态上色。未知状态不丢，直接显示英文码。
代码思路：labels 写死五态；v-for 输出 span+strong。App 传入 pileStatus || []，缺字段当空。
与其他文件关系：和 TelemetryPanel 互补——这里是汇总，那里是单桩物理量。样式 .status-list 在 tokens.css。数据来自 /api/dashboard。
答辩时可以怎么讲：切「离线」后 OFFLINE 应增加，CHARGING 应下降，但要等一轮刷新。顶部「实时」只是退出 UI 预览，不会把桩拉回在线。
-->
