<!-- 折线图逻辑：按需注册 ECharts 并随 props 重绘。 -->
<script setup lang="ts">
import { LineChart } from 'echarts/charts' // 只要折线，不引入整包 ECharts。
import { GridComponent, LegendComponent, TooltipComponent } from 'echarts/components' // 网格、图例、轴提示。
import { graphic, init, use, type ECharts } from 'echarts/core' // 核心：渐变、初始化、按需注册。
import { CanvasRenderer } from 'echarts/renderers' // Canvas 渲染，适合大屏刷新。
import { onBeforeUnmount, onMounted, ref, watch } from 'vue' // 挂载初始化、卸载销毁、监听数据。
import { useUiTheme } from '../theme' // 白昼/黑夜切换后重绘坐标轴和折线色。

use([LineChart, GridComponent, LegendComponent, TooltipComponent, CanvasRenderer]) // 按需注册，减小打包体积。
const props = defineProps<{ // 父组件传入序列和预测元信息。
  items: { hour: string; actual: number; forecast: number | null }[] // 每小时实际增量；预测不足为 null。
  modelMode: 'trained' | 'baseline' // 保留与视图模型对齐，图例主要用 forecastMeta.method。
  forecastMeta: { // 预测方法说明。
    method: string; mode: 'trained' | 'baseline' | 'insufficient' // 方法名和模式。
    sampleCount: number; updatedAt: string; maeKwh: string | null // 样本、时间和 MAE。
  } // 结束forecastMeta。
}>() // 结束 defineProps。
const el = ref<HTMLDivElement>() // 图表挂载的 DOM。
const { theme } = useUiTheme() // 切黑夜后重读 CSS 变量。
let chart: ECharts | undefined // 保存实例，避免重复 init。

function cssVar(name: string, fallback: string) { // 从当前主题读色，读不到再用白昼兜底。
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || fallback // 去掉空格。
} // 结束 cssVar。

function render() { // 根据当前 props 和主题重绘，不在这里补假预测。
  if (!el.value) return // DOM 未就绪则退出。
  const muted = cssVar('--muted', '#6B7B88') // 图例和刻度。
  const border = cssVar('--border', '#DCE6EC') // 轴线。
  const line = cssVar('--line', '#EDF2F5') // 网格。
  const green = cssVar('--green', '#24AD7A') // 实际电量。
  const purple = cssVar('--purple', '#756FC5') // 估算线。
  const text = cssVar('--text', '#183044') // 提示文字。
  const card = cssVar('--card', '#ffffff') // 提示底。
  chart ||= init(el.value) // 首次创建实例。
  chart.setOption({ // 写入配置；null 预测点 ECharts 会断线，符合“不造假”。
    animationDuration: matchMedia('(prefers-reduced-motion: reduce)').matches ? 0 : 520, // 减少动效时关掉动画。
    tooltip: { trigger: 'axis', backgroundColor: card, borderColor: border, textStyle: { color: text } }, // 轴对齐提示，跟主题。
    legend: { top: 0, right: 0, textStyle: { color: muted } }, // 右上图例。
    grid: { left: 52, right: 16, top: 42, bottom: 28 }, // 给 Y 轴 kWh 留空。
    xAxis: { // 分类轴：整点小时。
      type: 'category', // 离散时间标签。
      data: props.items.map(value => value.hour), // 用 hour 做 X。
      axisLine: { lineStyle: { color: border } }, // 轴线跟描边。
      axisLabel: { color: muted, interval: 2 }, // 隔两个点标一个，避免挤。
    }, // 结束xAxis。
    yAxis: { // 数值轴，单位 kWh。
      type: 'value', name: 'kWh', nameTextStyle: { color: muted }, // 名称写在轴旁。
      splitLine: { lineStyle: { color: line } }, // 网格跟分隔线。
      axisLabel: { color: muted }, // 刻度灰字。
    }, // 结束yAxis。
    series: [ // 两条线：实际实线、预测虚线。
      { // 实际电量增量。
        name: '实际电量增量', type: 'line', smooth: true, showSymbol: false, // 平滑且不画圆点。
        data: props.items.map(value => value.actual), // 实际序列。
        lineStyle: { color: green, width: 3 }, // 绿色主线。
        areaStyle: { // 绿色到透明的面积，透明度跟主题走，避免黑夜一块实心绿。
          opacity: 0.32, // 压低填充，折线才是主角。
          color: new graphic.LinearGradient(0, 0, 0, 1, [ // 自上而下渐变。
            { offset: 0, color: green }, // 顶部用主题绿。
            { offset: 1, color: 'transparent' }, // 底部透明。
          ]), // 结束面积渐变色标。
        }, // 结束areaStyle。
      }, // 结束实际电量系列。
      { // 预测/规则估算。
        name: props.forecastMeta.method, // 图例跟后端方法名，不足时显示「数据不足」。
        type: 'line', smooth: true, showSymbol: false, // 同样平滑。
        data: props.items.map(value => value.forecast), // 可能含 null。
        lineStyle: { color: purple, width: 2, type: 'dashed' }, // 紫色虚线，和实际区分。
      }, // 结束预测系列。
    ], // 结束series。
  }) // 结束setOption。
} // 结束函数 render。

onMounted(() => { render(); addEventListener('resize', render) }) // 进页绘制，窗口变化重绘。
onBeforeUnmount(() => { removeEventListener('resize', render); chart?.dispose() }) // 卸监听并释放 Canvas。
watch(() => [props.items, props.forecastMeta, theme.value], render, { deep: true }) // 数据或主题变了再画。
</script> <!-- 结束脚本区。 -->

<template> <!-- 开始模板。 -->
  <!-- 过去 24 小时用电趋势面板。 -->
  <article id="trend" class="panel load-panel">
    <!-- 标题与方法徽章。 -->
    <header>
      <!-- 口径：生命周期电表相邻差分，不是订单瞬时功率。 -->
      <div><p class="eyebrow">生命周期电表相邻差分</p><h2>过去24小时用电趋势</h2></div>
      <!-- 方法名同步到徽章。 -->
      <span class="status-chip"><i />{{ forecastMeta.method }}</span>
    </header> <!-- 结束标题行。 -->
    <!-- ECharts 容器，role=img 方便测试和读屏。 -->
    <div ref="el" class="chart" role="img" aria-label="实际用电量与规则估算折线图" />
    <!-- 样本数和 MAE；没有 MAE 就明确说不生成预测。 -->
    <small class="chart-note">
      <!-- 样本计数。 -->
      样本 {{ forecastMeta.sampleCount }} ·
      <!-- 有回测误差才显示，否则声明数据不足。 -->
      {{ forecastMeta.maeKwh ? `回测MAE ${forecastMeta.maeKwh} kWh` : '数据不足时不生成预测' }}
    </small> <!-- 结束辅文。 -->
  </article> <!-- 结束卡片。 -->
</template> <!-- 结束模板。 -->
<!--
【文件讲解·答辩用】
功能作用：用折线展示过去 24 小时实际用电增量，并在有方法时叠加估算线。
技术原理：ECharts 按需引入。预测为 null 时断开，不把实际值复制成预测。prefers-reduced-motion 时动画为 0。resize 时重绘，卸载 dispose。
代码思路：init 一次，之后 setOption。图例名称用 forecastMeta.method，答辩时能直接念“这是三点移动平均还是数据不足”。
与其他文件关系：App.vue 传入 view.load / modelMode / forecastMeta。打包分包在 vite.config.ts 的 charts chunk。数据仍只来自 /api/dashboard。
答辩时可以怎么讲：指绿线是库里电表差分，紫虚线是规则估算。点顶部「数据不足」预览时虚线消失，用来证明我们不造假预测；那只是 UI 预览，不是场景切换。
-->
