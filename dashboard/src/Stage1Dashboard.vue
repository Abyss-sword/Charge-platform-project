<!-- 大屏根页面：正常路径只拉 /api/dashboard；顶部按钮是 UI 预览；场景入口在 ScenarioSwitcher。 -->
<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue' // 组合式 API：计算属性、生命周期和响应式引用。
import type { DashboardState, DashboardViewModel } from './adapters/dashboardDataAdapter' // 大屏状态字面量和视图模型类型。
import { getDashboardView, loadDashboard } from './adapters/dashboardDataAdapter' // 预览态用本地模板，实时态用接口。
import DataSourceBar from './components/common/DataSourceBar.vue' // 顶栏：数据集、刷新时间和连接状态。
import PageState from './components/common/PageState.vue' // 非正常态横幅：加载中、空数据、接口失败等。
import MetricStrip from './components/MetricStrip.vue' // 站点/电桩/在线率等 KPI 卡片。
import LoadChart from './components/LoadChart.vue' // 24 小时用电趋势折线图。
import StationRanking from './components/StationRanking.vue' // 站点利用率排行。
import AlarmFeed from './components/AlarmFeed.vue' // 最新告警列表。
import DataQualityPanel from './components/DataQualityPanel.vue' // 数据质量与校验计数。
import PileStatusPanel from './components/PileStatusPanel.vue' // 电桩状态分布。
import RecommendationPanel from './components/RecommendationPanel.vue' // 可解释站点推荐，只展示接口 reasons。
import TicketPanel from './components/TicketPanel.vue' // 运维工单列表。
import TelemetryPanel from './components/TelemetryPanel.vue' // 电桩实时遥测表，数据来自接口 telemetry。
import ScenarioSwitcher from './components/ScenarioSwitcher.vue' // 答辩场景入口，不是顶部「实时/加载中」。

const jumps = [ // 顶栏只跳四大分区，细项放在各区标题里，避免十个按钮平铺。
  { id: 'overview', label: '总览' }, // KPI。
  { id: 'insight', label: '态势' }, // 趋势、排行、告警。
  { id: 'device', label: '设备' }, // 场景、遥测、状态。
  { id: 'ops', label: '运维' }, // 工单、推荐、质量。
] // 结束功能跳转。
const zoneChildren: Record<string, string[]> = { // 滚到细项时，顶栏仍要点亮所属分区。
  overview: [], // 总览没有子锚点。
  insight: ['trend', 'ranking', 'alarms'], // 态势。
  device: ['scenario', 'telemetry', 'status'], // 设备。
  ops: ['tickets', 'recommend', 'quality'], // 运维。
} // 结束分区细项。
const states: { value: DashboardState; label: string }[] = [ // 顶部预览按钮配置，只切 UI，不启动模拟器。
  { value: 'normal', label: '实时' }, // 退出预览，恢复 3 秒拉 /api/dashboard。
  { value: 'loading', label: '加载中' }, // 骨架屏预览，不代表正在切场景。
  { value: 'empty', label: '空数据' }, // 库里没有业务记录时的空态预览。
  { value: 'api-error', label: '接口失败' }, // 分析接口不可用时的失败预览。
  { value: 'model-error', label: '数据不足' }, // 预测样本不够、不造假预测的预览。
  { value: 'map-error', label: '地图失败' }, // 地图不可用、改用排行替代的预览。
] // 结束预览按钮配置。
const requested = new URLSearchParams(location.search).get('state') as DashboardState | null // 支持 ?state= 直达某预览态。
const initialPreview = states.some(item => item.value === requested) ? requested : null // 非法 query 忽略，避免乱切状态。
const previewState = ref<DashboardState | null>(initialPreview) // 非空表示停留在 UI 预览，暂停真实刷新。
const view = ref<DashboardViewModel>(getDashboardView(initialPreview || 'loading')) // 当前画面模型，初始先给加载态。
const isEmpty = computed(() => view.value.state === 'empty') // 空数据时走 empty-panel，不渲染业务卡片。
const activeJump = ref('overview') // 当前高亮：点击或滚动都会更新。
let refreshTimer: ReturnType<typeof setTimeout> | undefined // 下一次 3 秒刷新的定时器句柄。
let spyLocked = false // 点跳转后短暂锁住，避免平滑滚动途中闪过中间分区。
let spyFrame = 0 // requestAnimationFrame 句柄，用来节流 scroll。
let unlockTimer: ReturnType<typeof setTimeout> | undefined // 解锁滚动监听的定时器。

function isZoneActive(id: string) { // 顶栏分区：自身或任一细项在视口里都算高亮。
  return activeJump.value === id || (zoneChildren[id] || []).includes(activeJump.value) // 点「告警」时「态势」仍保持深色。
} // 结束 isZoneActive。

function syncActiveFromScroll() { // 按粘性条下方那条线，取最后一个已经越过的分区/细项。
  if (spyLocked) return // 程序化滚动期间不抢高亮。
  const overview = document.getElementById('overview') // 没有总览带就不必跟进，空态/骨架时保持原高亮。
  if (!overview || overview.getBoundingClientRect().height === 0) return // jsdom 或尚未布局时不要误亮最后一带。
  const bar = document.querySelector('.command-bar') // 吸顶命令条。
  const marker = bar ? bar.getBoundingClientRect().bottom + 8 : 88 // 条下沿再留一点，避免刚贴顶就切下一带。
  const zones = jumps.map(item => item.id) // 总览→态势→设备→运维。
  let zone = zones[0] // 默认总览。
  for (const id of zones) { // 从上往下，谁顶边已经越过标记，谁就是当前带。
    const el = document.getElementById(id) // 空态/加载中可能没有这些节点。
    if (el && el.getBoundingClientRect().top <= marker) zone = id // 后一个覆盖前一个。
  } // 结束分区循环。
  const scroller = document.scrollingElement || document.documentElement // 真正在滚的是文档根，不是某个内层盒子。
  if (scroller.scrollTop + window.innerHeight >= scroller.scrollHeight - 12) { // 滚到页底时强制最后一带。
    zone = zones[zones.length - 1] // 运维。
  } // 结束页底判断。
  let current = zone // 先落在分区，再看细项。
  for (const id of zoneChildren[zone] || []) { // 细项同样用“越过标记”规则。
    const el = document.getElementById(id) // 趋势/遥测/工单等。
    if (el && el.getBoundingClientRect().top <= marker + 24) current = id // 略放宽，避免刚进区就亮第一个细项。
  } // 结束细项循环。
  activeJump.value = current // 顶栏和区标题小按钮一起更新。
} // 结束 syncActiveFromScroll。

function onWindowScroll() { // 滚动用一帧合并，避免每像素都算。
  if (spyFrame) return // 已经排队。
  spyFrame = requestAnimationFrame(() => { // 下一帧再读布局。
    spyFrame = 0 // 清句柄。
    syncActiveFromScroll() // 更新高亮。
  }) // 结束 rAF。
} // 结束 onWindowScroll。

function jumpTo(id: string) { // 一键滚到对应面板；空态/加载中没有锚点时只高亮目录。
  activeJump.value = id // 立刻高亮点中的功能。
  spyLocked = true // 锁住滚动监听，防止途经中间带时乱跳。
  if (unlockTimer) clearTimeout(unlockTimer) // 连点时只保留最后一次解锁。
  document.getElementById(id)?.scrollIntoView({ behavior: 'smooth', block: 'start' }) // 有锚点才滚，避免空态报错。
  unlockTimer = setTimeout(() => { // 等平滑滚动大致结束。
    spyLocked = false // 重新允许滚动跟进。
    syncActiveFromScroll() // 以实际停住的位置校准一次。
  }, 520) // 略长于常见 smooth scroll。
} // 结束 jumpTo。

function scheduleRefresh() { // 按接口给出的间隔排队下一次拉数。
  if (previewState.value) return // 预览态不打接口，避免把演示状态冲掉。
  const seconds = Math.min(10, Math.max(2, view.value.refreshIntervalSeconds || 3)) // 间隔夹在 2～10 秒，默认 3 秒。
  refreshTimer = setTimeout(refresh, seconds * 1000) // 到期再调 refresh，形成轮询。
} // 结束 scheduleRefresh。

async function refresh() { // 正常路径：请求 /api/dashboard 并写回 view。
  if (previewState.value) return // 预览中直接返回，不覆盖本地模板。
  view.value = await loadDashboard(undefined, view.value) // 不传 state，适配器只会 fetch /api/dashboard。
  scheduleRefresh() // 成功或失败都继续排队，接口恢复后能自动回来。
} // 结束 refresh。

async function setState(state: DashboardState) { // 顶部按钮回调：预览 UI 或回到实时。
  if (refreshTimer) clearTimeout(refreshTimer) // 切状态前清掉未触发的刷新，防止串扰。
  if (state === 'normal') { // 「实时」不是场景名，而是退出预览、恢复拉数。
    previewState.value = null // 清空预览标记，允许 scheduleRefresh。
    view.value = { ...view.value, sourceStatus: 'refreshing' } // 顶栏先显示刷新中。
    await refresh() // 立刻拉一次真实接口。
    return // 实时分支结束，不走下面的本地模板。
  } // 结束「实时」分支。
  previewState.value = state // 记下当前预览态，后续 refresh 会被挡住。
  view.value = getDashboardView(state) // 用适配器里的固定演示对象，不把预览当生产数据。
} // 结束 setState。

watch(() => view.value.state, () => { // 预览切到空/加载/正常时，分区节点会增减。
  void nextTick(syncActiveFromScroll) // 等 DOM 更新后再对一次高亮。
}) // 结束 watch state。

onMounted(async () => { // 组件挂上后先拉数，再给自动化测试打 ready 标记。
  if (new URLSearchParams(location.search).get('motion') === 'off') { // D10：关闭动效验收，不改数据。
    document.documentElement.dataset.testMotion = 'off' // 与 motion.css 的 [data-test-motion=off] 对齐。
  } // 结束动效开关。
  addEventListener('scroll', onWindowScroll, { passive: true }) // 文档滚动（多数浏览器挂在 window）。
  document.addEventListener('scroll', onWindowScroll, { passive: true, capture: true }) // 捕获阶段再听一次，避免滚的是内层容器时收不到。
  await refresh() // 若 URL 带合法 ?state= 预览，refresh 内部会直接 return。
  document.documentElement.dataset.ready = 'true' // 给测试或截图脚本一个“首屏已尝试刷新”信号。
  await nextTick() // 等业务带挂上。
  syncActiveFromScroll() // 首屏校准一次，避免一直停在默认「总览」。
}) // 结束 onMounted。
onUnmounted(() => { // 卸定时器和滚动监听。
  if (refreshTimer) clearTimeout(refreshTimer) // 停刷新。
  if (unlockTimer) clearTimeout(unlockTimer) // 停跳转解锁。
  if (spyFrame) cancelAnimationFrame(spyFrame) // 停未执行的滚动帧。
  removeEventListener('scroll', onWindowScroll) // 离开页面不再跟高亮。
  document.removeEventListener('scroll', onWindowScroll, { capture: true }) // 卸捕获监听。
}) // 结束 onUnmounted。
</script> <!-- 结束根组件脚本。 -->

<!-- 根模板：顶栏、预览条、场景入口和各业务面板。 -->
<template>
  <!-- 根布局：data-state 方便测试和 CSS 按状态换样式。 -->
  <main class="dashboard-shell" :data-state="view.state">
    <!-- 顶栏只展示数据源元信息，不承担场景切换。 -->
    <DataSourceBar :view="view" />
    <!-- 粘性命令条：左侧功能跳转，右侧画面预览。 -->
    <div class="command-bar">
      <!-- 功能目录：列出全部业务块，点击滚到锚点。 -->
      <nav class="section-nav" aria-label="功能跳转">
        <button v-for="item in jumps" :key="item.id" type="button" :class="{ active: isZoneActive(item.id) }" @click="jumpTo(item.id)">{{ item.label }}</button>
      </nav>
      <!-- 验收用 UI 预览：实时/加载中/空数据等，不是模拟器场景入口。 -->
      <nav class="state-switcher" aria-label="验收状态切换">
        <!-- 六枚按钮对应六种画面状态；「实时」在非预览时高亮。 -->
        <button v-for="state in states" :key="state.value" :class="{ active: view.state === state.value && (state.value !== 'normal' || !previewState) }" @click="setState(state.value)">{{ state.label }}</button>
      </nav>
    </div> <!-- 结束命令条。 -->
    <!-- 非 normal 时显示原因横幅，normal 时隐藏以免挡数据。 -->
    <PageState v-if="view.state !== 'normal'" :state="view.state" :message="view.message || ''" />
    <!-- 加载中只出骨架，不拿旧指标冒充正在刷新。 -->
    <template v-if="view.state === 'loading'">
      <!-- 八张 KPI 骨架，数量对齐指标带。 -->
      <div id="overview" class="metric-grid skeleton-row">
        <!-- 占位卡片，没有数字。 -->
        <span v-for="n in 8" :key="n" class="skeleton-card" />
      </div> <!-- 结束 KPI 骨架行。 -->
      <!-- 主区三块高骨架，对应图/排行/告警位置。 -->
      <div class="content-grid">
        <!-- 两块高占位。 -->
        <span class="skeleton-card tall" /><span class="skeleton-card tall" />
        <!-- 第三块高占位。 -->
        <span class="skeleton-card tall" />
      </div> <!-- 结束主区骨架。 -->
    </template> <!-- 结束加载中分支。 -->
    <!-- 非空且非加载：按总览 / 态势 / 设备 / 运维四条带排布。 -->
    <template v-else-if="!isEmpty">
      <!-- 总览：只放 KPI，先看数字再下钻。 -->
      <section id="overview" class="zone">
        <header class="zone-head">
          <div>
            <p class="zone-kicker">01 总览</p>
            <h3>关键运营指标</h3>
          </div>
          <p class="zone-desc">站点、电桩、在线率、营收和告警，全部来自统一库聚合。</p>
        </header>
        <MetricStrip :metrics="view.metrics" />
      </section>
      <!-- 态势：负荷、忙站、告警放同一带，看“现在怎么样”。 -->
      <section id="insight" class="zone">
        <header class="zone-head">
          <div>
            <p class="zone-kicker">02 态势</p>
            <h3>用电、站点与告警</h3>
          </div>
          <nav class="zone-links" aria-label="态势细项">
            <button type="button" :class="{ active: activeJump === 'trend' }" @click="jumpTo('trend')">趋势</button>
            <button type="button" :class="{ active: activeJump === 'ranking' }" @click="jumpTo('ranking')">排行</button>
            <button type="button" :class="{ active: activeJump === 'alarms' }" @click="jumpTo('alarms')">告警</button>
          </nav>
        </header>
        <div class="content-grid primary-grid">
          <LoadChart :items="view.load" :model-mode="view.modelMode" :forecast-meta="view.forecastMeta" />
          <StationRanking :items="view.ranking" />
          <AlarmFeed :items="view.alarms" />
        </div>
      </section>
      <!-- 设备：场景、遥测、状态放同一带，切场景后就在这里对表。 -->
      <section id="device" class="zone">
        <header class="zone-head">
          <div>
            <p class="zone-kicker">03 设备</p>
            <h3>场景切换与实时监测</h3>
          </div>
          <nav class="zone-links" aria-label="设备细项">
            <button type="button" :class="{ active: activeJump === 'scenario' }" @click="jumpTo('scenario')">场景</button>
            <button type="button" :class="{ active: activeJump === 'telemetry' }" @click="jumpTo('telemetry')">遥测</button>
            <button type="button" :class="{ active: activeJump === 'status' }" @click="jumpTo('status')">状态</button>
          </nav>
        </header>
        <ScenarioSwitcher />
        <div class="content-grid device-grid">
          <TelemetryPanel :items="view.telemetry || []" />
          <PileStatusPanel :items="view.pileStatus || []" />
        </div>
      </section>
      <!-- 运维：工单、推荐、质量放同一带，看“接下来怎么处置”。 -->
      <section id="ops" class="zone">
        <header class="zone-head">
          <div>
            <p class="zone-kicker">04 运维</p>
            <h3>工单、分流与数据质量</h3>
          </div>
          <nav class="zone-links" aria-label="运维细项">
            <button type="button" :class="{ active: activeJump === 'tickets' }" @click="jumpTo('tickets')">工单</button>
            <button type="button" :class="{ active: activeJump === 'recommend' }" @click="jumpTo('recommend')">推荐</button>
            <button type="button" :class="{ active: activeJump === 'quality' }" @click="jumpTo('quality')">质量</button>
          </nav>
        </header>
        <div class="content-grid action-grid">
          <TicketPanel :items="view.tickets || []" />
          <RecommendationPanel :items="view.recommendations || []" :model-mode="view.modelMode" />
          <DataQualityPanel :quality="view.quality" />
        </div>
      </section>
    </template> <!-- 结束非空业务区。 -->
    <!-- 空数据专用面板，避免空白让人以为页面坏了。 -->
    <section v-else class="empty-panel">
      <!-- 文案来自适配器 message。 -->
      <strong>暂无可展示数据</strong><p>{{ view.message }}</p>
    </section> <!-- 结束空数据面板。 -->
  </main> <!-- 结束大屏根布局。 -->
</template> <!-- 结束根模板。 -->
<!--
【文件讲解·答辩用】
功能作用：运营大屏总控。负责 3 秒刷新、六种验收画面、把接口字段分发给各面板，并挂上场景切换和遥测表。
技术原理：previewState 把“UI 预览”和“实时拉数”分开。预览走 getDashboardView 的固定对象；实时走 loadDashboard → GET /api/dashboard。顶部「实时/加载中」只改画面状态，不会 POST 模拟器场景。
代码思路：挂载后 refresh；scheduleRefresh 用接口 refreshIntervalSeconds（默认 3 秒）排队；点「实时」清预览再拉数；点其他按钮暂停轮询并套本地模板。页面按四条带排布：总览 KPI、态势（图/排行/告警）、设备（场景/遥测/状态）、运维（工单/推荐/质量）。顶栏跳分区，滚动用 syncActiveFromScroll 跟进高亮。
与其他文件关系：数据形状和请求在 dashboardDataAdapter.ts；场景按钮在 ScenarioSwitcher.vue；遥测在 TelemetryPanel.vue；样式在 tokens.css。本文件不直接改 SQLite，也不自己造遥测数字。
答辩时可以怎么讲：先指顶部「这是验收预览，不是切桩场景」；再指中间 ScenarioSwitcher「这才是统一场景入口」；最后指 KPI/遥测「数字只来自 /api/dashboard，切场景后等一轮刷新就能看见库里的新数据」。
-->
