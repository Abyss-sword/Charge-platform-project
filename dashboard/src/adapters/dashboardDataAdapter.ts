export type DashboardState = // 大屏画面状态：加载、正常、空、接口失败、预测不足、地图失败。
  | 'loading' | 'normal' | 'empty' | 'api-error' | 'model-error' | 'map-error' // 与顶部预览按钮和 PageState 文案一一对应。
export type DataMode = 'SIMULATED' | 'REAL_ANONYMIZED' | 'PUBLIC_AGGREGATED' // 数据来源模式，顶栏 mode-tag 直接展示。

export interface TelemetryReading { // 单桩最新入库遥测，字段与 FastAPI /dashboard 对齐。
  pileId: string // 桩主键，对应 pile_info.id。
  pileSn: string // 桩序列号，表格主标题。
  stationName: string // 所属站点名称。
  status: string // 英文状态码，如 CHARGING / FAULT。
  temperatureC: number // 温度，摄氏度。
  voltageV: number // 电压，伏特。
  currentA: number // 电流，安培。
  powerW: number // 功率，瓦。
  powerKw: number // 功率，千瓦，表格主展示。
  energyKwh: string // 生命周期累计电表，不是当前订单电量。
  recordedAt: string // 入库时间字符串。
  sourceMode: string // 来源标记，模拟器一般为 SIMULATED。
  fresh: boolean // 是否 15 秒内有上报。
  ageSeconds: number | null // 距上报的秒数；时间无效时为 null。
} // 结束类型 TelemetryReading。

export interface DashboardViewModel { // 前端唯一视图模型，实时和预览都长这样。
  state: DashboardState // 当前画面状态。
  region: string // 区域文案。
  dataMode: DataMode // 数据模式标签。
  datasetName: string // 数据集名称。
  datasetVersion: string // 数据集版本，顶栏展示。
  snapshotVersion: number // S1-D：联动写版本，旧响应按此丢弃。
  snapshotAtUtc: string // S1-D：快照读取时刻（UTC）。
  coverage: string // 覆盖时间说明。
  updatedAt: string // 接口刷新时间。
  dataTimestamp: string // 业务数据时间，和 updatedAt 分开以免混淆。
  stale: boolean // 是否陈旧（失败降级或预览）。
  sourceStatus: 'connected' | 'refreshing' | 'failed' | 'demo' // 顶栏连接文案用。
  refreshIntervalSeconds: number // 建议刷新间隔，App.vue 夹在 2～10 秒。
  metricDefinitions?: Record<string, string> // 可选指标口径说明。
  metrics: { label: string; value: string; unit: string; tone: string }[] // KPI 卡片。
  pileStatus: { status: string; count: number }[] // 桩状态计数。
  telemetry: TelemetryReading[] // 最新遥测行；接口缺字段时由 loadDashboard 补空数组。
  load: { hour: string; actual: number; forecast: number | null }[] // 24h 实际与预测。
  energyTrend: { hour: string; actual: number; forecast: number | null }[] // 电量趋势备份字段。
  revenueTrend: { period: string; revenueYuan: string }[] // 营收趋势。
  forecastMeta: { // 预测方法元信息，图表图例直接用 method。
    method: string // 方法名，如移动平均或「数据不足」。
    mode: 'trained' | 'baseline' | 'insufficient' // 训练 / 基线 / 不足。
    sampleCount: number // 样本数。
    updatedAt: string // 预测更新时间。
    maeKwh: string | null // 回测 MAE；不足时为 null，前端不得填假值。
  } // 结束forecastMeta。
  ranking: { id: string; name: string; utilization: number; available: number }[] // 站点排行。
  alarms: { // 告警列表。
    id: string; type?: string; severity: '严重' | '警告' | '提示' // 编号、类型、中文级别。
    status?: string; message: string; time: string // 处置状态、文案、时间。
  }[] // 结束 alarms 数组类型。
  tickets: { // 运维工单。
    id: string; alarmId: string; pileSn: string; type: string // 工单号、告警号、桩号、类型。
    assignee: string; status: string; updatedAt: string // 处理人、状态、更新时间。
  }[] // 结束 tickets 数组类型。
  recommendations: { // 推荐站点（本大屏根页暂未挂面板，类型仍保留以对齐接口）。
    name: string; wait: number; distance: number; available: number; reason: string // 站名、等待、距离、空闲桩、原因。
  }[] // 结束 recommendations 数组类型。
  quality: { // 数据质量汇总。
    raw: number; valid: number; rejected: number; rate: number // 输入、有效、异常、有效率。
    batch: string; license: string; checks?: Record<string, number> // 批次、来源说明、分项校验计数。
  } // 结束quality。
  modelMode: 'trained' | 'baseline' // 推荐/预测是否走训练模型。
  message?: string // 状态横幅或空态说明。
} // 结束类型 DashboardViewModel。

// 仅供 ?state= 和顶部预览按钮使用，启动后的生产画面禁止把它当实时数据。
// 正常启动一律走 /api/dashboard，下面对象只用来演示加载/空/失败等 UI。
const demo: DashboardViewModel = { // 固定预览对象，数字不代表当前库。
  state: 'normal', // 预览底稿默认写成 normal，再由 getDashboardView 改写。
  region: '教学演示状态', // 明确告诉观众这是演示。
  dataMode: 'SIMULATED', // 预览也标模拟，避免被当成真实脱敏数据。
  datasetName: 'ui-state-preview', // 数据集名标明仅预览。
  datasetVersion: 'preview-only', // 版本号同样标明预览。
  snapshotVersion: 0, // 预览没有联动版本。
  snapshotAtUtc: '未连接', // 预览没有快照时间。
  coverage: '仅用于页面状态验收', // 覆盖说明防止误读。
  updatedAt: '未连接', // 预览未打接口。
  dataTimestamp: '未连接', // 业务时间同样未连接。
  stale: true, // 预览一律算陈旧。
  sourceStatus: 'demo', // 顶栏走「演示状态」文案。
  refreshIntervalSeconds: 3, // 与产品默认刷新一致，方便测试断言。
  metrics: [ // 预览 KPI，只在点顶部按钮时出现。
    { label: '站点总数', value: '3', unit: '座', tone: 'green' }, // 站点。
    { label: '电桩总数', value: '12', unit: '台', tone: 'blue' }, // 电桩。
    { label: '设备在线率', value: '91.7', unit: '%', tone: 'cyan' }, // 在线率。
    { label: '充电中订单', value: '2', unit: '单', tone: 'purple' }, // 充电订单。
    { label: '今日服务量', value: '8', unit: '次', tone: 'green' }, // 服务次数。
    { label: '今日营收', value: '86.40', unit: '元', tone: 'purple' }, // 营收。
  ], // 结束metrics。
  pileStatus: [ // 预览状态分布。
    { status: 'IDLE', count: 7 }, { status: 'RESERVED', count: 1 }, // 空闲与预约。
    { status: 'CHARGING', count: 2 }, { status: 'FAULT', count: 1 }, // 充电与故障。
    { status: 'OFFLINE', count: 1 }, // 离线。
  ], // 结束pileStatus。
  telemetry: [{ // 预览遥测一行，方便测试断言表格文案。
    pileId: '101', pileSn: 'PILE-DEMO-01', stationName: '演示站', status: 'CHARGING', // 演示桩身份。
    temperatureC: 26, voltageV: 231, currentA: 31.2, // 物理量。
    powerW: 7200, powerKw: 7.2, energyKwh: '100.50', // 功率与累计电表。
    recordedAt: '未连接', sourceMode: 'SIMULATED', fresh: false, ageSeconds: null, // 未连接故不算 fresh。
  }], // 结束telemetry。
  load: [ // 预览折线三个点。
    { hour: '08:00', actual: 5.2, forecast: null }, // 早八点无预测。
    { hour: '09:00', actual: 6.1, forecast: null }, // 早九点无预测。
    { hour: '10:00', actual: 7.0, forecast: 5.65 }, // 十点给出基线预测。
  ], // 结束load。
  energyTrend: [], // 预览不填电量趋势。
  revenueTrend: [], // 预览不填营收趋势。
  forecastMeta: { // 预览预测说明。
    method: '3点移动平均规则估算', mode: 'baseline', // 基线方法名。
    sampleCount: 3, updatedAt: '未连接', maeKwh: null, // 样本少，无 MAE。
  }, // 结束forecastMeta。
  ranking: [{ id: '1', name: '演示站', utilization: 25, available: 3 }], // 一个演示站。
  alarms: [{ // 一条过热预览告警。
    id: 'ALARM-DEMO', type: 'OVERHEAT', severity: '严重', // 编号与级别。
    status: 'OPEN', message: 'PILE-DEMO 温度达到阈值', time: '10:00', // 文案与时间。
  }], // 结束alarms。
  tickets: [{ // 对应一条预览工单。
    id: 'TICKET-DEMO', alarmId: 'ALARM-DEMO', pileSn: 'PILE-DEMO', // 工单关联告警。
    type: 'OVERHEAT', assignee: '未分配', status: 'OPEN', updatedAt: '10:00', // 未分配。
  }], // 结束tickets。
  recommendations: [{ // 预览推荐，根页 RecommendationPanel 会展示。
    name: '演示站', wait: 6, distance: 0, available: 3, // 等待与空闲。
    reason: '页面状态预览，不代表实时业务数据', // 写明预览口径。
  }], // 结束recommendations。
  quality: { // 预览质量。
    raw: 3, valid: 3, rejected: 0, rate: 100, // 全有效。
    batch: 'preview-only', license: '页面状态预览', // 批次标明预览。
  }, // 结束quality。
  modelMode: 'baseline', // 预览用基线，不冒充已训练。
} // 结束 demo 预览对象。

function copy(): DashboardViewModel { // 深拷贝预览底稿，避免多状态互相改同一对象。
  return JSON.parse(JSON.stringify(demo)) as DashboardViewModel // JSON 往返足够复制本结构。
} // 结束函数 copy。

export function getDashboardView(state: DashboardState = 'normal'): DashboardViewModel { // 按状态裁剪预览对象。
  const vm = copy() // 先拿一份干净底稿。
  vm.state = state // 写入目标状态。
  if (state === 'empty') { // 空数据：清业务数组，只留说明。
    vm.metrics = [] // 无 KPI。
    vm.load = [] // 无折线。
    vm.telemetry = [] // 无遥测。
    vm.ranking = [] // 无排行。
    vm.alarms = [] // 无告警。
    vm.tickets = [] // 无工单。
    vm.recommendations = [] // 无推荐。
    vm.message = '统一数据库当前没有业务记录。' // 空态文案。
  } // 结束条件分支。
  if (state === 'api-error') { // 接口失败且没有可复用的上次成功数据时用。
    vm.stale = true // 标陈旧。
    vm.sourceStatus = 'failed' // 顶栏连接失败。
    vm.metrics = [] // 不展示预览 KPI，避免当成库数据。
    vm.load = [] // 清空折线。
    vm.telemetry = [] // 清空遥测。
    vm.ranking = [] // 清空排行。
    vm.alarms = [] // 清空告警。
    vm.tickets = [] // 清空工单。
    vm.recommendations = [] // 清空推荐。
    vm.message = '分析接口暂不可用，当前没有可确认的成功数据。' // 失败文案。
  } // 结束条件分支。
  if (state === 'model-error') { // 预测不足：保留实际点，预测全置空。
    vm.modelMode = 'baseline' // 退回基线标记。
    vm.load.forEach(item => { item.forecast = null }) // 不伪造预测值。
    vm.forecastMeta = { // 方法改成「数据不足」。
      method: '数据不足', mode: 'insufficient', // 图表图例会显示这句话。
      sampleCount: vm.load.length, updatedAt: '未连接', maeKwh: null, // 无 MAE。
    } // 结束forecastMeta。
    vm.message = '预测数据不足，页面不展示伪造预测值。' // 横幅说明原则。
  } // 结束条件分支。
  if (state === 'map-error') { // 地图失败仍用排行，只改文案。
    vm.message = '地图资源不可用，已使用数据库站点排行替代。' // 告诉观众降级策略。
  } // 结束条件分支。
  if (state === 'loading') vm.message = '正在读取统一业务数据库…' // 加载文案。
  return vm // 交给 App.vue 渲染。
} // 结束函数 getDashboardView。

function isDashboardPayload(value: unknown): value is DashboardViewModel { // 运行时收窄：至少要有状态、数组和时间。
  if (!value || typeof value !== 'object') return false // 非对象直接拒绝。
  const candidate = value as Partial<DashboardViewModel> // 按部分字段检查。
  return typeof candidate.state === 'string' // 必须有状态字符串。
    && Array.isArray(candidate.metrics) // 必须有指标数组。
    && Array.isArray(candidate.load) // 必须有负荷数组。
    && Array.isArray(candidate.alarms) // 必须有告警数组。
    && typeof candidate.updatedAt === 'string' // 必须有更新时间。
} // 结束函数 isDashboardPayload。

export const OFFICIAL_DASHBOARD_ENDPOINT = '/api/dashboard' // 第二阶段唯一切换点：只打正式 FastAPI，不读 raw 目录。

export async function loadDashboard( // 实时入口：无 state 时只请求正式 /api/dashboard。
  state?: DashboardState, // 若传入状态，说明调用方只要预览，不打网。
  previous?: DashboardViewModel, // 上次成功画面，失败时用于陈旧降级。
): Promise<DashboardViewModel> { // 始终返回可渲染的视图模型。
  if (state) return getDashboardView(state) // 预览短路，保证演示数据不会混进 fetch。
  try { // 正常产品路径。
    const response = await fetch(OFFICIAL_DASHBOARD_ENDPOINT, { signal: AbortSignal.timeout(2500) }) // 2.5 秒超时，同源由 Vite 代理。
    if (!response.ok) throw new Error(`API HTTP ${response.status}`) // 非 2xx 当失败。
    const payload: unknown = await response.json() // 先当 unknown，再用守卫检查。
    if (!isDashboardPayload(payload)) throw new Error('API payload invalid') // 缺关键字段则失败。
    payload.telemetry = Array.isArray(payload.telemetry) ? payload.telemetry : [] // 旧接口没遥测时补空数组，页面仍保持 normal。
    if (!payload.dataMode) payload.dataMode = 'SIMULATED' // 接口没给模式时保持教学模拟，不把 raw 或空字段说成真实脱敏。
    payload.snapshotVersion = typeof payload.snapshotVersion === 'number' ? payload.snapshotVersion : 0 // 旧接口无联动版本时按 0 处理。
    payload.snapshotAtUtc = typeof payload.snapshotAtUtc === 'string' ? payload.snapshotAtUtc : payload.updatedAt // 旧接口回退 updatedAt。
    // S1-D：版本比较——收到的快照版本小于已展示版本即为旧响应，直接丢弃，保留当前画面。
    if (previous && previous.sourceStatus !== 'demo'
        && payload.snapshotVersion < previous.snapshotVersion) {
      return {
        ...previous, // 维持上次成功画面，绝不回退旧数据覆盖新数据。
        stale: true, // 标记陈旧，让顶栏提示。
        message: `收到旧快照 v${payload.snapshotVersion}（当前 v${previous.snapshotVersion}），已丢弃该响应。`,
      } // 结束返回对象。
    } // 结束旧快照判断。
    return payload // 把后端 JSON 原样交给画面，前端不改口径。
  } catch { // 超时、HTTP 错误、JSON 非法都走这里。
    if (previous && previous.metrics.length > 0 && previous.sourceStatus !== 'demo') { // 有过真实成功数据才降级展示。
      return { // 保留上次指标，但标明失败和陈旧。
        ...previous, // 展开上次字段。
        state: 'api-error', // 画面进入接口失败。
        stale: true, // 顶栏陈旧。
        sourceStatus: 'failed', // 连接失败。
        message: `分析接口暂不可用，展示最近成功数据（${previous.updatedAt}）。`, // 说明数字时间戳。
      } // 结束返回对象。
    } // 结束条件分支。
    return getDashboardView('api-error') // 从未成功过则清空业务数组，绝不拿 preview 数字顶上。
  } // 结束try。
} // 结束函数 loadDashboard。
/*
【文件讲解·答辩用】
功能作用：大屏唯一数据适配层。定义视图模型，提供预览模板 getDashboardView，以及实时加载 loadDashboard。
技术原理：生产和预览分流。App.vue 实时刷新调用 loadDashboard(undefined, previous)，内部只 fetch('/api/dashboard')。顶部「实时/加载中」传 state 时走 getDashboardView，用固定 demo，避免把 UI 验收数据说成库里的数。失败时若有非 demo 的上次成功，则陈旧展示；否则清空。
代码思路：类型先锁字段 → demo 只服务预览 → copy 防串改 → getDashboardView 按状态裁剪 → isDashboardPayload 做最小校验 → loadDashboard 补 telemetry 空数组以兼容旧后端。预测不足时把 forecast 置 null，不补假点。
与其他文件关系：App.vue 每 3 秒调用本文件；MetricStrip、LoadChart、TelemetryPanel 等只消费返回对象。场景切换不走这里，而走 ScenarioSwitcher → /api/simulator/scenario。Vite 把 /api 转到 FastAPI。
答辩时可以怎么讲：强调“前端不造生产数据”。点顶部按钮看到的 3 座站、12 台桩是 preview-only；点场景按钮后要等 /api/dashboard 刷新，遥测和告警才会变。老师若问接口挂了怎么办，回答：有上次成功就标陈旧继续显示，没有就空着，绝不用 demo 冒充实时。
*/
