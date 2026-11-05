<!-- 遥测表逻辑：翻译状态并格式化物理量。 -->
<script setup lang="ts">
import { ref } from 'vue' // 展开全部列只影响展示，不改接口数据。
import type { TelemetryReading } from '../adapters/dashboardDataAdapter' // 字段来自 FastAPI /dashboard，不在前端造数。

defineProps<{ items: TelemetryReading[] }>() // 父组件 App.vue 每 3 秒把最新 telemetry 传进来。
const expanded = ref(false) // 默认隐藏电压、电流、来源，避免 1366 上整表横滚。

const statusLabels: Record<string, string> = { // 服务端英文状态转中文，未知值原样显示。
  IDLE: '空闲', // 空闲。
  RESERVED: '已预约', // 已预约。
  CHARGING: '充电中', // 充电中。
  FAULT: '故障', // 故障。
  OFFLINE: '离线', // 离线。
  UNKNOWN: '未知', // 未知。
} // 结束statusLabels。

function statusLabel(status: string) { // 缺映射时不丢原始码，方便对照数据库。
  return statusLabels[status] || status // 先查表，没有就返回英文。
} // 结束函数 statusLabel。

function formatNumber(value: number, digits = 1) { // 空值显示 --，不用 0 冒充有遥测。
  return Number.isFinite(value) ? value.toFixed(digits) : '--' // 非有限数字一律占位。
} // 结束函数 formatNumber。
</script> <!-- 结束脚本区。 -->

<template> <!-- 开始模板。 -->
  <!-- 电桩实时遥测：温度、电压、电流、功率、累计电量、上报时间、来源。 -->
  <section id="telemetry" class="panel telemetry-panel">
    <!-- 标题、列展开和新鲜度徽章。 -->
    <header>
      <!-- 左列标题。 -->
      <div>
        <!-- 英文眉题。 -->
        <p class="eyebrow">Live telemetry</p>
        <!-- 中文主标题。 -->
        <h2>电桩实时遥测</h2>
      </div> <!-- 结束容器。 -->
      <!-- 右侧：展开次要列 + 新鲜度。 -->
      <div class="telemetry-meta">
        <!-- 默认只留状态/温度/功率/电量，需要电气明细再展开。 -->
        <button type="button" class="link-button" @click="expanded = !expanded">{{ expanded ? '收起次要列' : '展开全部列' }}</button>
        <!-- fresh 表示 15 秒内有入库；切充电/过热后应在一轮刷新内看到变化。 -->
        <span class="status-chip" :class="items.some(item => item.fresh) ? 'good' : 'alert'">
          <!-- 有一行 fresh 则显示「15 秒内有上报」。 -->
          <i />{{ items.some(item => item.fresh) ? '15 秒内有上报' : '等待最新上报' }}
        </span>
      </div>
    </header> <!-- 结束标题行。 -->
    <!-- 有数据才出表，避免空表头。 -->
    <div v-if="items.length" class="telemetry-table-wrap">
      <!-- 最小宽度保证物理量不被挤扁，小屏横向滚动。 -->
      <table class="telemetry-table" :class="{ expanded }">
        <!-- 列名固定，与接口字段顺序对应。 -->
        <thead>
          <!-- 表头行。 -->
          <tr>
            <!-- 桩号与站点。 -->
            <th>电桩 / 站点</th>
            <!-- 运行状态。 -->
            <th>状态</th>
            <!-- 温度。 -->
            <th>温度</th>
            <!-- 电压。 -->
            <th>电压</th>
            <!-- 电流。 -->
            <th>电流</th>
            <!-- 功率。 -->
            <th>功率</th>
            <!-- 累计电表。 -->
            <th>累计电量</th>
            <!-- 入库时间。 -->
            <th>上报时间</th>
            <!-- 数据来源。 -->
            <th>来源</th>
          </tr> <!-- 结束表行。 -->
        </thead> <!-- 结束表头。 -->
        <!-- 一行一桩，key 用 pileId。 -->
        <tbody>
          <!-- 遍历接口下发的遥测。 -->
          <tr v-for="item in items" :key="item.pileId">
            <!-- 序列号加粗，站点与 ID 作辅文。 -->
            <td>
              <!-- 桩 SN。 -->
              <strong>{{ item.pileSn }}</strong>
              <!-- 站点名和数字 ID。 -->
              <small>{{ item.stationName }} · ID {{ item.pileId }}</small>
            </td> <!-- 结束单元格。 -->
            <!-- 中文状态胶囊，class 用英文小写上色。 -->
            <td><span class="telemetry-status" :class="item.status.toLowerCase()">{{ statusLabel(item.status) }}</span></td>
            <!-- 温度保留 1 位。 -->
            <td><b>{{ formatNumber(item.temperatureC) }}</b><small> ℃</small></td>
            <!-- 电压保留 1 位。 -->
            <td><b>{{ formatNumber(item.voltageV) }}</b><small> V</small></td>
            <!-- 电流保留 1 位。 -->
            <td><b>{{ formatNumber(item.currentA) }}</b><small> A</small></td>
            <!-- 功率用千瓦、2 位小数。 -->
            <td><b>{{ formatNumber(item.powerKw, 2) }}</b><small> kW</small></td>
            <!-- energyKwh 是生命周期累计电表，不是当前订单电量。 -->
            <td><b>{{ item.energyKwh }}</b><small> kWh</small></td>
            <!-- 上报时间 + 新鲜度辅文。 -->
            <td>
              <!-- 原始时间字符串。 -->
              <strong>{{ item.recordedAt }}</strong>
              <!-- fresh / 无效时间 / N 秒前 三选一。 -->
              <small>{{ item.fresh ? '实时' : item.ageSeconds === null ? '时间无效' : `${item.ageSeconds} 秒前` }}</small>
            </td> <!-- 结束单元格。 -->
            <!-- SIMULATED 显示「模拟器」，其他模式原样。 -->
            <td><span class="source-badge">{{ item.sourceMode === 'SIMULATED' ? '模拟器' : item.sourceMode }}</span></td>
          </tr> <!-- 结束表行。 -->
        </tbody> <!-- 结束表体。 -->
      </table> <!-- 结束表格。 -->
    </div> <!-- 结束容器。 -->
    <!-- 空数组提示去跑场景，不填假行。 -->
    <p v-else class="panel-empty">统一数据库中还没有遥测记录，请运行电桩模拟器场景。</p>
  </section> <!-- 结束面板。 -->
</template> <!-- 结束模板。 -->
<!--
【文件讲解·答辩用】
功能作用：把 /api/dashboard 下发的最新入库遥测排成表。用来证明切场景后统一库真的进了新数据。
技术原理：纯展示组件，没有随机数、没有本地模拟。fresh 由后端按 15 秒窗口计算；空值 formatNumber 显示 --，避免 0 看起来像“桩在工作”。
代码思路：props 只收 items；状态映射表把英文码翻成中文；表格列和接口字段一一对应。没有记录时给空态，而不是藏起整块面板。
与其他文件关系：App.vue 传入 view.telemetry || []。类型在 dashboardDataAdapter.ts 的 TelemetryReading。切场景在 ScenarioSwitcher.vue，本组件只被动刷新。
答辩时可以怎么讲：点「正常充电」后指功率和状态列；点「高温告警」后指温度列，并提醒观众看 AlarmFeed。强调这些格子不是 Canvas 动画，而是 SQLite 里最新一行。
-->
