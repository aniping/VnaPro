<script setup lang="ts">
import type { AcquiredFrame, InstrumentStatus } from '../../api/types'
import type { LayoutMode } from '../../domain/presentation'
defineProps<{ mode: string; status: InstrumentStatus | null; frame: AcquiredFrame | null; layout: LayoutMode; connected: boolean; hasControl: boolean; controlBusy: boolean }>()
const emit = defineEmits<{ layout: [value: LayoutMode]; export: []; acquire: []; release: [] }>()
</script>

<template>
  <section v-if="mode === 'display'" class="softtool-section">
    <h2 class="softtool-section-title">Diagram Layout</h2>
    <button class="softtool-choice" :class="{ 'is-active': layout === 'grid' }" :aria-pressed="layout === 'grid'" @click="emit('layout', 'grid')">All Diagrams</button>
    <button class="softtool-choice" :class="{ 'is-active': layout === 'maximized' }" :aria-pressed="layout === 'maximized'" @click="emit('layout', 'maximized')">Maximized · Active</button>
    <button class="softtool-choice" :class="{ 'is-active': layout === 'single' }" :aria-pressed="layout === 'single'" @click="emit('layout', 'single')">Overlay All</button>
    <p class="softtool-note">最多显示前四条接收波。Maximized 仅显示当前迹线；Overlay 共用当前迹线的纵轴。</p>
    <div class="softtool-divider" />
    <h2 class="softtool-section-title">Instrument Workspace</h2>
    <div class="softtool-readout"><span>Reference canvas</span><strong>1280 × 800</strong></div>
    <p class="softtool-note">窗口按比例缩放，保留仪器操作位置。Esc 收起软工具，硬键重新打开。</p>
  </section>
  <section v-else-if="mode === 'file' || mode === 'tools'" class="softtool-section">
    <h2 class="softtool-section-title">Export Displayed Frame</h2>
    <button class="softtool-button softtool-primary" :disabled="!frame" @click="emit('export')">Export CSV</button>
    <div class="softtool-readout"><span>Frame sequence</span><strong>{{ frame?.frameSeq ?? '—' }}</strong></div>
    <div class="softtool-readout"><span>Displayed points</span><strong>{{ frame?.displayPointCount ?? '—' }}</strong></div>
    <div class="softtool-readout"><span>Acquired points</span><strong>{{ frame?.originalPointCount ?? '—' }}</strong></div>
    <p class="softtool-note">导出当前画面的真实频点、有效位及原始 Re/Im。超过 2001 点时标明显示抽取；旧帧的版本号随数据保留。</p>
  </section>
  <section v-else-if="mode === 'help'" class="softtool-section">
    <h2 class="softtool-section-title">Quick Start</h2>
    <ol class="softtool-help"><li>Stimulus：填写频率、点数和 IF 带宽，再 Apply。</li><li>Meas：添加 a / b 接收波，指定接收端口和激励端口。</li><li>Sweep：Single 采一帧；Continuous 连续采集；Hold 停止。</li><li>点击图内迹线条，Format / Scale / Marker 只调整该迹线显示。</li></ol>
    <div class="softtool-divider" />
    <h2 class="softtool-section-title">Marker M1</h2>
    <p class="softtool-note">在曲线图点击或拖动选择真实频点。聚焦绘图区，← / → 移动一频点，Shift 移动十点，Home / End 到两端。</p>
    <div class="softtool-divider" />
    <p class="softtool-note">NativeSimulator 支持 RAW 接收波与未校准 S11/S21。未实现误差校准、S12/S22、Smith 图与 RF 控制；相关入口禁用。界面参考 ZNB，远程 SCPI 使用已有 Keysight 命令子集。</p>
  </section>
  <section v-else class="softtool-section">
    <h2 class="softtool-section-title">Instrument</h2>
    <div class="softtool-readout"><span>Connection</span><strong>{{ connected ? 'Online' : 'Offline' }}</strong></div>
    <div class="softtool-readout"><span>Backend</span><strong>{{ status?.backend ?? '—' }}</strong></div>
    <div class="softtool-readout"><span>State</span><strong>{{ status?.state ?? '—' }}</strong></div>
    <div class="softtool-readout"><span>Configuration revision</span><strong>{{ status?.revision ?? '—' }}</strong></div>
    <div class="softtool-readout"><span>Run ID</span><strong>{{ status?.runId ?? '—' }}</strong></div>
    <div class="softtool-divider" />
    <h2 class="softtool-section-title">Remote Control</h2>
    <div class="softtool-readout"><span>Owner</span><strong>{{ hasControl ? 'This browser' : status?.control.owner ?? '—' }}</strong></div>
    <button class="softtool-button" :disabled="!connected || controlBusy || (!hasControl && status?.control.owner !== 'none')" @click="hasControl ? emit('release') : emit('acquire')">{{ hasControl ? 'Release Control' : 'Acquire Control' }}</button>
    <p class="softtool-note">SCPI 与 Web 共享一个采集核心和写控制权。旁观页面仍可读取完整帧。</p>
    <p class="softtool-note instrument-identity">{{ status?.identity ?? 'Waiting for instrument…' }}</p>
  </section>
</template>
