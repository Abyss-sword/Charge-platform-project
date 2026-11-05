<!-- 质量面板逻辑：翻译校验键名。 -->
<script setup lang="ts">
defineProps<{ quality: { // 质量汇总对象。
  raw: number; valid: number; rejected: number; rate: number // 输入、有效、异常、有效率。
  batch: string; license: string; checks?: Record<string, number> // 批次、来源、分项计数。
} }>() // 结束质量 props 声明。
const labels: Record<string, string> = { // 校验项英文键转中文。
  missing: '必填缺失', invalid_time: '时间/时区异常', // 缺失与时间异常。
  time_backward: '时间倒退', energy_backward: '电量倒退', // 与场景按钮同名的两类倒退。
  duplicate: '重复记录', unknown_relation: '关联不存在', simulated: '模拟来源', // 重复、未知桩、模拟标记。
} // 结束labels。
</script> <!-- 结束脚本区。 -->

<template> <!-- 开始模板。 -->
  <!-- 数据来源与质量。 -->
  <article id="quality" class="panel quality-panel">
    <!-- 标题与合格徽章。 -->
    <header>
      <!-- 左列。 -->
      <div>
        <!-- 眉题。 -->
        <p class="eyebrow">逐项数据校验</p>
        <!-- 主标题。 -->
        <h2>数据来源与质量</h2>
      </div> <!-- 结束容器。 -->
      <!-- 无异常为绿「合格」，否则显示异常条数。 -->
      <span class="status-chip" :class="{ good: quality.rejected === 0 }">
        <!-- 三元文案。 -->
        {{ quality.rejected === 0 ? '✓ 合格' : `${quality.rejected} 项异常` }}
      </span> <!-- 结束行内块。 -->
    </header> <!-- 结束标题行。 -->
    <!-- 右侧圆锥渐变环，--rate 来自有效率。 -->
    <div class="quality-ring" :style="{ '--rate': `${quality.rate}%` }">
      <!-- 环心数字。 -->
      <strong>{{ quality.rate }}%</strong><span>有效率</span>
    </div> <!-- 结束容器。 -->
    <!-- 三项计数。 -->
    <dl>
      <!-- raw 输入。 -->
      <div><dt>raw输入</dt><dd>{{ quality.raw }}</dd></div>
      <!-- 有效。 -->
      <div><dt>有效记录</dt><dd>{{ quality.valid }}</dd></div>
      <!-- 异常。 -->
      <div><dt>异常记录</dt><dd>{{ quality.rejected }}</dd></div>
    </dl> <!-- 结束定义列表。 -->
    <!-- 分项校验芯片，有 checks 才渲染。 -->
    <div class="quality-checks" v-if="quality.checks">
      <!-- 未知键显示英文原名。 -->
      <span v-for="(count, name) in quality.checks" :key="name">
        <!-- 中文标签：计数。 -->
        {{ labels[name] || name }}：{{ count }}
      </span> <!-- 结束行内块。 -->
    </div> <!-- 结束容器。 -->
    <!-- 批次号。 -->
    <p><b>批次：</b>{{ quality.batch }}</p>
    <!-- 来源/许可证说明。 -->
    <p><b>来源：</b>{{ quality.license }}</p>
  </article> <!-- 结束卡片。 -->
</template> <!-- 结束模板。 -->
<!--
【文件讲解·答辩用】
功能作用：把数据校验结果可视化：有效率环、三项计数、分项芯片、批次和来源。
技术原理：环用 conic-gradient 和 CSS 变量 --rate，不用再引入图表库。checks 的键与模拟器脏数据场景对应（重复、时间倒退、电量倒退、未知桩）。
代码思路：组件不重跑校验规则，只翻译键名。rejected===0 才点亮「合格」。
与其他文件关系：App.vue 传入 view.quality。ScenarioSwitcher 的 duplicate/time-backward/energy-backward/unknown-pile 应能在刷新后增加对应计数。
答辩时可以怎么讲：先指有效率，再点一个脏数据场景，等刷新后指芯片计数变化。说明质量规则在分析侧，前端只展示。
-->
