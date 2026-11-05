<script setup lang="ts">
import { BarChart, GaugeChart, HeatmapChart, LineChart, PieChart, ScatterChart } from 'echarts/charts'
import { GridComponent, LegendComponent, TooltipComponent, VisualMapComponent } from 'echarts/components'
import { use } from 'echarts/core'
import { CanvasRenderer } from 'echarts/renderers'

export type AnalysisChartKind = 'line' | 'bar' | 'donut' | 'heatmap' | 'gauge' | 'scatter'

// Register only the six Stage2 chart families. Rendering starts after a frozen
// API supplies typed series; an empty container must never invent values.
use([
  LineChart, BarChart, PieChart, HeatmapChart, GaugeChart, ScatterChart,
  GridComponent, LegendComponent, TooltipComponent, VisualMapComponent,
  CanvasRenderer,
])

defineProps<{
  kind: AnalysisChartKind
  title: string
  source: string
}>()

const labels: Record<AnalysisChartKind, string> = {
  line: '折线图',
  bar: '柱状图',
  donut: '环形图',
  heatmap: '热力图',
  gauge: '仪表图',
  scatter: '散点图',
}
</script>

<template>
  <article class="panel analysis-chart" :data-chart-kind="kind">
    <header>
      <div><span class="eyebrow">{{ labels[kind] }}</span><h2>{{ title }}</h2></div>
      <span class="status-chip">无正式数据</span>
    </header>
    <div class="chart-placeholder" role="img" :aria-label="`${title}，等待正式数据`">
      <span>等待正式分析数据</span>
    </div>
    <small class="chart-source">数据来源：{{ source }} · 版本：-- · 时间：--</small>
  </article>
</template>
