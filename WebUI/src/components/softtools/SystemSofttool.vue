<script setup lang="ts">
import type { AcquiredFrame, InstrumentStatus } from '../../api/types'
import type { LayoutMode } from '../../domain/presentation'
defineProps<{
  mode: string
  status: InstrumentStatus | null
  frame: AcquiredFrame | null
  layout: LayoutMode
  connected: boolean
  hasControl: boolean
  controlBusy: boolean
  canEditDisplay: boolean
}>()
const emit = defineEmits<{
  layout: [value: LayoutMode]
  export: []
  acquire: []
  release: []
}>()
</script>

<template>
  <section v-if="mode === 'display'" class="softtool-section">
    <h2 class="softtool-section-title">Diagram Layout</h2>
    <button
      class="softtool-choice"
      :disabled="!canEditDisplay"
      :class="{ 'is-active': layout === 'grid' }"
      :aria-pressed="layout === 'grid'"
      @click="emit('layout', 'grid')"
    >
      All Diagrams
    </button>
    <button
      class="softtool-choice"
      :disabled="!canEditDisplay"
      :class="{ 'is-active': layout === 'maximized' }"
      :aria-pressed="layout === 'maximized'"
      @click="emit('layout', 'maximized')"
    >
      Maximized · Active
    </button>
    <button
      class="softtool-choice"
      :disabled="!canEditDisplay"
      :class="{ 'is-active': layout === 'single' }"
      :aria-pressed="layout === 'single'"
      @click="emit('layout', 'single')"
    >
      Overlay All
    </button>
    <p class="softtool-note">
      Meas 中选择最多四条显示迹线。Maximized 仅显示当前迹线；Overlay 按矩形与
      Smith 坐标分窗，避免混画。
    </p>
    <div class="softtool-divider" />
    <h2 class="softtool-section-title">Instrument Workspace</h2>
    <div class="softtool-readout">
      <span>Reference canvas</span><strong>1280 × 800</strong>
    </div>
    <p class="softtool-note">
      窗口按比例缩放，保留仪器操作位置。Esc 收起软工具，硬键重新打开。
    </p>
  </section>
  <section
    v-else-if="mode === 'file' || mode === 'tools'"
    class="softtool-section"
  >
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
      导出当前画面的真实频点、有效位及原始 Re/Im。超过 2001
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
      <li>Meas：添加 S11/S21/S12/S22 或 RAW，选择最多四条显示迹线。</li>
      <li>
        Sweep → Sweep Control：Single 采一帧；Continuous 连续采集；Hold 停止。
      </li>
      <li>点击图内迹线条，Format / Scale / Marker 只调整该迹线显示。</li>
    </ol>
    <div class="softtool-divider" />
    <h2 class="softtool-section-title">Marker M1</h2>
    <p class="softtool-note">
      在曲线图点击或拖动选择真实频点。聚焦绘图区，← / → 移动一频点，Shift
      移动十点，Home / End 到两端。
    </p>
    <div class="softtool-divider" />
    <p class="softtool-note">
      NativeSimulator 支持 RAW 与未校准 S11/S21/S12/S22；S11/S22 可显示 Smith 的
      Γ 与归一化阻抗。校准与 RF 控制尚未实现。界面参考 ZNB，远程 SCPI 使用已有
      Keysight 命令子集。
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
