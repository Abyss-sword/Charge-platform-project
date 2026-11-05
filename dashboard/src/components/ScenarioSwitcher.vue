<!-- 场景入口逻辑：只 POST 场景名，不改业务表。 -->
<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue' // 忙闲、高亮场景和提示文案都只存在本组件。

const scenarios = [ // 按钮文案用中文，id 必须与 pile_simulator --scenario 和 FastAPI 白名单一致。
  { id: 'normal', label: '正常在线' }, // 桩在线空闲。
  { id: 'charging', label: '正常充电' }, // 进入充电，遥测功率应上升。
  { id: 'overheat', label: '高温告警' }, // 温度越限，库中应出现告警/工单。
  { id: 'offline', label: '离线' }, // 停止心跳。
  { id: 'recover', label: '恢复' }, // 从异常回到正常。
  { id: 'duplicate', label: '重复上报' }, // 触发重复记录校验。
  { id: 'time-backward', label: '时间倒退' }, // 触发时间倒退校验。
  { id: 'energy-backward', label: '电量倒退' }, // 触发电量倒退校验。
  { id: 'unknown-pile', label: '未知电桩' }, // 未知关联，质量面板应计数。
] // 结束scenarios。

const busy = ref(false) // true 时禁用全部按钮，防止连点启动多个模拟器。
const active = ref('') // 当前高亮的场景 id，来自后端 status。
const message = ref('点击按钮即可切换模拟器场景，不改源码。') // 底部提示，展示受理结果或失败原因。
let timer: ReturnType<typeof setInterval> | undefined // 2 秒轮询一次执行状态。

async function refreshStatus() { // 只读查询后台是否仍在跑场景。
  try { // 失败时保留旧提示，不把整块入口拆掉。
    const response = await fetch('/api/simulator/status', { signal: AbortSignal.timeout(2500) }) // 2.5 秒超时。
    if (!response.ok) return // 非 2xx 静默返回。
    const payload = await response.json() as { busy?: boolean; scenario?: string; message?: string } // 只取三个字段。
    busy.value = Boolean(payload.busy) // 同步禁用态。
    active.value = payload.scenario || '' // 同步高亮。
    if (payload.message) message.value = payload.message // 有新文案才覆盖。
  } catch { // 网络失败时什么都不改。
    /* 状态查询失败时保留上一次提示，避免把按钮入口打掉。 */
  } // 结束try。
} // 结束函数 refreshStatus。

async function runScenario(id: string) { // 浏览器不能启 .exe：只 POST 场景名，由 FastAPI 调 pile_simulator。
  if (busy.value) return // 执行中忽略点击。
  busy.value = true // 立刻禁用，防止连点。
  active.value = id // 先高亮点中的按钮。
  message.value = `正在切换到 ${id}…` // 即时反馈。
  try { // 把场景交给分析服务。
    const response = await fetch('/api/simulator/scenario', { // 场景切换接口，不是 /api/dashboard。
      method: 'POST', // 写操作走 POST。
      headers: { 'Content-Type': 'application/json' }, // JSON 体。
      body: JSON.stringify({ scenario: id, pileId: 101 }), // 默认演示桩 101，对应种子库 pile_info.id。
      signal: AbortSignal.timeout(4000), // 只等接口受理，不等模拟器跑完。
    }) // 结束response。
    const payload = await response.json() as { detail?: string; status?: { message?: string } } // FastAPI 错误或受理回执。
    if (!response.ok) { // 白名单拒绝或服务异常。
      message.value = payload.detail || '场景切换失败' // 优先展示后端 detail。
      busy.value = false // 失败立即解锁按钮。
      return // 结束本次点击。
    } // 结束条件分支。
    if (payload.status?.message) message.value = payload.status.message // 成功则显示后台一句状态。
  } catch { // 分析服务没起来。
    message.value = '无法联系分析服务，请确认 FastAPI 已启动。' // 给答辩排查提示。
    busy.value = false // 解锁按钮。
  } // 结束try。
} // 结束函数 runScenario。

onMounted(() => { // 进页先同步一次，再开始轮询。
  void refreshStatus() // 忽略 Promise，错误已在函数内吞掉。
  timer = setInterval(() => { void refreshStatus() }, 2000) // 2 秒刷新忙闲和高亮。
}) // 结束onMounted。
onUnmounted(() => { if (timer) clearInterval(timer) }) // 离开页面停轮询，避免卸载后写 ref。
</script> <!-- 结束脚本区。 -->

<template> <!-- 开始模板。 -->
  <!-- 答辩统一场景入口：九个场景 + 演示轮播，与顶部「实时/加载中」无关。 -->
  <section id="scenario" class="panel scenario-panel">
    <!-- 标题、状态和忙闲徽章挤在一行，给首屏腾高度。 -->
    <header>
      <!-- 左列标题。 -->
      <div>
        <!-- 英文分类标签，风格与其他面板一致。 -->
        <p class="eyebrow">Simulator</p>
        <!-- 中文主标题。 -->
        <h2>模拟场景切换</h2>
      </div> <!-- 结束容器。 -->
      <!-- 右侧：最近一次受理说明 + 忙闲。 -->
      <div class="scenario-meta">
        <!-- 最近一次受理或失败说明。 -->
        <p class="scenario-status">{{ message }}</p>
        <!-- 执行中琥珀色，空闲绿色；颜色不是唯一状态，旁边有文字。 -->
        <span class="status-chip" :class="busy ? 'alert' : 'good'">
          <!-- 圆点 + 文字双通道表达忙闲。 -->
          <i />{{ busy ? '执行中' : '可切换' }}
        </span>
      </div>
    </header> <!-- 结束标题行。 -->
    <!-- 场景按钮网格。 -->
    <div class="scenario-grid">
      <!-- 九个白名单场景；busy 时全部禁用。 -->
      <button v-for="item in scenarios" :key="item.id" :class="{ active: active === item.id }" :disabled="busy" @click="runScenario(item.id)">{{ item.label }}</button>
      <!-- cycle 按 正常→充电→过热→离线(等18秒)→恢复 自动走一遍。 -->
      <button class="cycle" :disabled="busy" @click="runScenario('cycle')">演示轮播</button>
    </div> <!-- 结束容器。 -->
  </section> <!-- 结束面板。 -->
</template> <!-- 结束模板。 -->
<!--
【文件讲解·答辩用】
功能作用：大屏上唯一的模拟器场景入口。点击后让后台启动 pile_simulator，经 TCP 把遥测/告警写入统一 SQLite。
技术原理：前端不能启 Windows 进程。POST /api/simulator/scenario 只传场景 id 和 pileId=101；GET /api/simulator/status 每 2 秒同步 busy/高亮。按钮禁用靠 busy，避免并发拉起多个模拟器。
代码思路：scenarios 的 id 必须和后端白名单一致；runScenario 先本地置忙再请求；失败立即解锁；成功等 status 轮询把 busy 拉回来。cycle 也走同一 POST，只是 id 不同。
与其他文件关系：挂在 App.vue 业务区，位于 KPI 和遥测之间。它不改 view.metrics。大屏数字仍只来自 /api/dashboard 的 3 秒刷新。样式在 tokens.css 的 .scenario-* 。
答辩时可以怎么讲：先对比顶部「实时/加载中」——那是 UI 预览。再指这块：“老师要看过热，请点「高温告警」，不要点「加载中」。”切完等两三秒刷新，TelemetryPanel 和 AlarmFeed 会变。
-->
