<script setup lang="ts">
import type { AcquiredFrame, InstrumentStatus } from '../../api/types'
defineProps<{
  mode: string
  status: InstrumentStatus | null
  frame: AcquiredFrame | null
  connected: boolean
  hasControl: boolean
  controlBusy: boolean
}>()
const emit = defineEmits<{
  export: []
  acquire: []
  release: []
}>()
</script>

<template>
  <section v-if="mode === 'file' || mode === 'tools'" class="softtool-section">
    <h2 class="softtool-section-title">Export Displayed Frame</h2>
    <button
      class="softtool-button softtool-primary"
      :disabled="!frame"
      @click="emit('export')"
    >
      Export CSV
    </button>
    <div class="softtool-readout">
      <span>Frame sequence</span><strong>{{ frame?.frameSeq ?? '—' }}</strong>
    </div>
    <div class="softtool-readout">
      <span>Displayed points</span
      ><strong>{{ frame?.displayPointCount ?? '—' }}</strong>
    </div>
    <div class="softtool-readout">
      <span>Acquired points</span
      ><strong>{{ frame?.originalPointCount ?? '—' }}</strong>
    </div>
    <p class="softtool-note">
      导出当前画面的真实频点、有效位及测量 Re/Im。超过 2001
      点时标明显示抽取；显示投影及原始点索引来自后端，版本号随数据保留。
    </p>
  </section>
  <section v-else-if="mode === 'help'" class="softtool-section">
    <h2 class="softtool-section-title">Quick Start</h2>
    <ol class="softtool-help">
      <li>
        Stimulus：在仪器能力范围内填写 Start/Stop 或 Center/Span，再 Apply。
      </li>
      <li>Sweep → Sweep Params：编辑 Number of Points，再 Apply。</li>
      <li>Power / Bw / Avg → Bandwidth：编辑 IF Bandwidth，再 Apply。</li>
      <li>
        Meas：点击上方参数编号打开 S 参数矩阵，或选择 RAW。单击工具栏 Trc+
        复制活动迹线；拖到图窗后选择新迹线参数。Trace Config 管理迹线。
      </li>
      <li>
        Sweep → Sweep Control：Single 采一帧；Continuous 连续采集；Hold 停止。
      </li>
      <li>点击图内迹线条，Format / Scale / Marker 只调整该迹线显示。</li>
      <li>Cal → Start Cal：选择端口、确认理想 50 Ω 校准件，逐项采集标准后 Apply。Use Cal 启停并管理校准集。</li>
    </ol>
    <div class="softtool-divider" />
    <h2 class="softtool-section-title">Markers</h2>
    <p class="softtool-note">
      在曲线图点击或拖动选择真实频点。聚焦绘图区，← / → 移动一频点，Shift
      移动十点，Home / End 到两端。 每条迹线独立使用 M1～M10。Display → Info
      Table 可将各迹线的读数停靠在表格中。
    </p>
    <div class="softtool-divider" />
    <p class="softtool-note">
      支持已配置端口的 RAW 与完整 Sij；Meas 上方选择器提供全部合法参数。
      Smith 显示复数轨迹，反射参数可读取 Γ 与归一化阻抗。每条 S 参数显示 Cal / Cal Off；RAW 接收波不应用误差校准。
      RF 控制尚未实现。界面参考 ZNB，远程 SCPI 使用项目支持的命令子集。
    </p>
  </section>
  <section v-else class="softtool-section">
    <h2 class="softtool-section-title">Instrument</h2>
    <div class="softtool-readout">
      <span>Connection</span
      ><strong>{{ connected ? 'Online' : 'Offline' }}</strong>
    </div>
    <div class="softtool-readout">
      <span>Backend</span><strong>{{ status?.backend ?? '—' }}</strong>
    </div>
    <div class="softtool-readout">
      <span>State</span><strong>{{ status?.state ?? '—' }}</strong>
    </div>
    <div class="softtool-readout">
      <span>Configuration revision</span
      ><strong>{{ status?.revision ?? '—' }}</strong>
    </div>
    <div class="softtool-readout">
      <span>Run ID</span><strong>{{ status?.runId ?? '—' }}</strong>
    </div>
    <div class="softtool-divider" />
    <h2 class="softtool-section-title">Remote Control</h2>
    <div class="softtool-readout">
      <span>Owner</span
      ><strong>{{
        hasControl ? 'This browser' : (status?.control.owner ?? '—')
      }}</strong>
    </div>
    <button
      class="softtool-button"
      :disabled="
        !connected ||
        controlBusy ||
        (!hasControl && status?.control.owner !== 'none')
      "
      @click="hasControl ? emit('release') : emit('acquire')"
    >
      {{ hasControl ? 'Release Control' : 'Acquire Control' }}
    </button>
    <p class="softtool-note">
      SCPI 与 Web 共享一个采集核心和写控制权。旁观页面仍可读取完整帧。
    </p>
    <p class="softtool-note instrument-identity">
      {{ status?.identity ?? 'Waiting for instrument…' }}
    </p>
  </section>
</template>
