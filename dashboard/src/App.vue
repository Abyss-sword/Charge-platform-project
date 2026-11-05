<script setup lang="ts">
import { computed, onBeforeUnmount, ref } from 'vue'
import { BorderBox8, Decoration5 } from '@kjgl77/datav-vue3'
import AnalysisChart, { type AnalysisChartKind } from './components/AnalysisChart.vue'
import DashboardSection from './components/DashboardSection.vue'
import { AnalysisTransport } from './analysis/http'
import { parseForecast, parseHealth, parseHive, parsePeak, parseRecommendation, type ForecastEnvelope, type HealthEnvelope, type HiveReportEnvelope, type PeakEnvelope, type RecommendationEnvelope } from './analysis/contract'

type DashboardStatus = 'ok' | 'unavailable' | 'empty' | 'expired'
const props = withDefaults(defineProps<{ initialStatus?: DashboardStatus; autoLoad?: boolean }>(), { initialStatus: 'unavailable', autoLoad: false })
const systemPrefersReducedMotion = typeof matchMedia === 'function' && matchMedia('(prefers-reduced-motion: reduce)').matches
const motionEnabled = ref(!systemPrefersReducedMotion)
const status = ref<DashboardStatus>(props.initialStatus)
const forecast = ref<ForecastEnvelope | null>(null)
const previousForecast = ref<ForecastEnvelope | null>(null)
const recommendation = ref<RecommendationEnvelope | null>(null)
const peak = ref<PeakEnvelope | null>(null)
const health = ref<HealthEnvelope | null>(null)
const hiveReports = ref<HiveReportEnvelope[]>([])
const loading = ref(false)
const sourceLabel = ref('等待正式分析服务')
const transport = new AnalysisTransport()
let controller: AbortController | undefined

const stateCopy = computed(() => ({
  ok: { title: '正式分析数据已就绪', description: '当前页面仅展示分析服务返回的正式数据。' },
  unavailable: { title: '分析服务暂不可用', description: '正式分析接口不可用，当前不展示业务数值。' },
  empty: { title: '暂无分析数据', description: '当前筛选范围没有可展示的数据。' },
  expired: { title: '分析数据已过期', description: '结果可能不是最新版本，已保留最后一次成功结果。' },
}[status.value]))
const chartKinds: { kind: AnalysisChartKind; title: string; area: string }[] = [
  { kind: 'line', title: '未来负荷趋势', area: '历史分析区' }, { kind: 'bar', title: '站点负荷对比', area: '历史分析区' },
  { kind: 'donut', title: '电桩状态构成', area: '在线业务区' }, { kind: 'heatmap', title: '站点 × 小时热力', area: '历史分析区' },
  { kind: 'gauge', title: '在线服务健康度', area: '在线业务区' }, { kind: 'scatter', title: '负荷与空闲桩关系', area: '历史分析区' },
]
function toggleMotion() { motionEnabled.value = !motionEnabled.value; document.documentElement.dataset.motion = motionEnabled.value ? 'on' : 'off' }
function applyForecast(next: ForecastEnvelope) {
  if (next.status === 'OK') { previousForecast.value = next; forecast.value = next; status.value = 'ok' }
  else if (next.status === 'NO_DATA') { forecast.value = null; status.value = 'empty' }
  else if (next.status === 'EXPIRED') { forecast.value = previousForecast.value; status.value = 'expired' }
  else { forecast.value = null; status.value = 'unavailable' }
  sourceLabel.value = next.sourceMode
}
async function loadFormalData() {
  loading.value = true; controller?.abort(); controller = new AbortController(); const signal = controller.signal
  try {
    const [healthResult, forecastResult, recommendResult, peakResult] = await Promise.allSettled([
      transport.request('/health', {}, parseHealth, signal), transport.request('/forecast', { horizon: '24' }, parseForecast, signal),
      transport.request('/recommend', {}, parseRecommendation, signal), transport.request('/analysis/peak', { horizon: '24' }, parsePeak, signal),
    ])
    if (signal.aborted) return
    if (healthResult.status === 'fulfilled') health.value = healthResult.value
    if (forecastResult.status === 'fulfilled') applyForecast(forecastResult.value)
    else if (!previousForecast.value) { status.value = 'unavailable'; sourceLabel.value = '正式分析服务不可用' }
    if (recommendResult.status === 'fulfilled') recommendation.value = recommendResult.value
    if (peakResult.status === 'fulfilled') peak.value = peakResult.value
    const reports = await Promise.allSettled(['station_daily_energy', 'station_hour', 'weekday_hour'].map(kind => transport.request('/analysis/hive', { kind }, parseHive, signal)))
    if (!signal.aborted) hiveReports.value = reports.filter((item): item is PromiseFulfilledResult<HiveReportEnvelope> => item.status === 'fulfilled').map(item => item.value)
  } finally { if (!signal.aborted) loading.value = false }
}
document.documentElement.dataset.motion = motionEnabled.value ? 'on' : 'off'
if (props.autoLoad) void loadFormalData()
onBeforeUnmount(() => { controller?.abort(); delete document.documentElement.dataset.motion })
</script>

<template>
  <main class="dashboard-shell stage2-dashboard" data-responsive="dashboard" :data-status="status">
    <header class="topbar stage2-topbar"><div class="brand"><span class="brand-mark">E</span><div><p>EV FLOW · STAGE2</p><h1>充电运营分析大屏</h1></div></div><div class="dashboard-actions"><span class="mode-tag stale"><i />{{ sourceLabel }}</span><button class="motion-toggle" type="button" :aria-pressed="!motionEnabled" @click="toggleMotion">{{ motionEnabled ? '关闭动效' : '开启动效' }}</button></div></header>
    <Decoration5 class="datav-heading" :color="['#3478c5', '#24ad7a']" :dur="motionEnabled ? 3 : 0" />
    <section class="formal-state" role="status" aria-live="polite"><strong>{{ stateCopy.title }}</strong><span>{{ stateCopy.description }}</span><span v-if="loading">正在读取正式分析数据…</span></section>
    <section class="source-ledger" aria-label="数据来源与版本"><div><small>在线业务来源</small><strong>正式只读快照</strong></div><div><small>历史分析来源</small><strong>{{ hiveReports.length ? 'HIVE 发布产物' : '不可用' }}</strong></div><div><small>数据版本</small><strong>{{ forecast?.datasetVersion || health?.artifacts?.datasetVersion || '--' }}</strong></div><div><small>覆盖时间</small><strong>{{ forecast?.targetStart || '--' }}</strong></div><div><small>更新时间</small><strong>{{ forecast?.generatedAt || '--' }}</strong></div></section>
    <BorderBox8 class="datav-frame" :color="['#3478c5', '#24ad7a']" background-color="#ffffff" :dur="motionEnabled ? 3 : 0"><div class="datav-frame-content">
      <DashboardSection id="online-business" title="在线业务区" source="SQLite 正式只读快照"><p class="section-note">在线业务与历史分析产物隔离；仅展示正式接口返回内容。</p><div class="chart-grid two-columns"><AnalysisChart v-for="chart in chartKinds.filter(item => item.area === '在线业务区')" :key="chart.kind" :kind="chart.kind" :title="chart.title" source="正式在线数据 · 未接入时显示空态" /></div></DashboardSection>
      <DashboardSection id="historical-analysis" title="历史分析区" source="版本化分析产物"><p class="section-note">预测、基线和历史观测携带 datasetVersion，并与在线业务口径分开。</p><div v-if="forecast?.points.length" class="forecast-data-panel"><div class="data-meta">forecast_v1 · {{ forecast.modelMode }} · {{ forecast.datasetVersion || 'unavailable' }}</div><div v-for="point in forecast.points" :key="point.targetStart" class="forecast-row"><span>{{ point.targetStart }}</span><strong>{{ point.energyKwh === null ? '不可用' : `${point.energyKwh} kWh` }}</strong><small>{{ point.nullReason || '正式预测' }}</small></div></div><div v-else class="chart-grid"><AnalysisChart v-for="chart in chartKinds.filter(item => item.area === '历史分析区')" :key="chart.kind" :kind="chart.kind" :title="chart.title" source="正式历史数据 · 未接入时显示空态" /></div><div class="hive-reports"><article v-for="report in hiveReports" :key="report.kind"><small>{{ report.kind }} · {{ report.source }}</small><strong>{{ report.rowCount ?? report.rows.length }} 行</strong><span>{{ report.datasetVersion || '版本不可用' }} · {{ report.runId || 'runId 不可用' }}</span></article></div></DashboardSection>
      <DashboardSection id="quality-batch" title="质量与批次区" source="Hadoop/Hive/Spark 发布状态"><div class="quality-placeholders"><article><small>服务状态</small><strong>{{ health?.status || 'unavailable' }}</strong></article><article><small>模型与特征</small><strong>{{ health?.artifacts?.modelsAvailable ? 'READY' : 'unavailable' }}</strong></article><article><small>运行批次</small><strong>{{ health?.artifacts?.runId || forecast?.datasetVersion || 'unavailable' }}</strong></article><article><small>预测预警</small><strong>{{ peak?.warning.level || 'unavailable' }}</strong></article></div><div v-if="recommendation" class="recommendation-data"><small>member3_recommendation_v1 · {{ recommendation.source }}</small><div v-for="item in recommendation.recommendations" :key="item.rank"><strong>#{{ item.rank }} {{ item.name }}</strong><span>score {{ item.score }} · {{ item.reasons.join('、') || '无原因' }}</span></div><em v-if="!recommendation.origin.arrivalTimeUsed">到达时刻当前未参与推荐评分</em></div></DashboardSection>
    </div></BorderBox8>
    <footer>第29组 · Stage2 Web 展示 · Asia/Shanghai</footer>
  </main>
</template>
