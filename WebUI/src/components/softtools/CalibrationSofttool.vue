<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, ref, watch } from 'vue'
import type { InstrumentStatus } from '../../api/types'
import type { CalibrationCommand } from '../../api/calibrationTypes'
import { decodeCalibrationExport } from '../../api/calibrationDecoders'
import { calibrationMethodLabel, calibrationPorts, selectedCalibrationSet } from '../../domain/calibration'
import { frequencyText } from '../../plot/formatting'
import CalibrationSetManager from './CalibrationSetManager.vue'

const props = defineProps<{
  mode: 'cal' | 'cal-use'
  status: InstrumentStatus | null
  canCalibrate: boolean
  busy: boolean
  command: (command: CalibrationCommand) => Promise<InstrumentStatus | null>
  exportSet: (setId: string) => Promise<Record<string, unknown>>
}>()
const emit = defineEmits<{ start: [] }>()
const calibration = computed(() => props.status?.calibration)
const active = computed(() => Boolean(calibration.value?.enabled && calibration.value.compatible))
const currentSet = computed(() => selectedCalibrationSet(calibration.value))
const selected = ref(calibration.value?.selectedSetId ?? '')
const working = ref(false)
const error = ref('')
const fileInput = ref<HTMLInputElement | null>(null)
const managerEntry = ref<HTMLButtonElement | null>(null)
const managerOpen = ref(false)
let disposed = false
onBeforeUnmount(() => { disposed = true })
const disabled = computed(() => !props.canCalibrate || props.busy || working.value)
const managerDisabled = computed(() => disabled.value || calibration.value?.collecting === true)
const selectedSet = computed(() => calibration.value?.sets.find((entry) => entry.id === selected.value))
watch(() => calibration.value?.sets, (sets) => {
  if (!sets?.some((entry) => entry.id === selected.value)) selected.value = calibration.value?.selectedSetId ?? ''
})
watch(() => props.status?.activeChannelId, () => {
  managerOpen.value = false
  selected.value = calibration.value?.selectedSetId ?? ''
  error.value = ''
})

function closeManager() {
  if (working.value) return
  managerOpen.value = false
  void nextTick(() => managerEntry.value?.focus())
}

function applySelected() {
  if (selectedSet.value?.compatible) return act({ action: 'select', setId: selected.value })
}

function deleteSelected() {
  if (selectedSet.value) return act({ action: 'delete', setId: selected.value })
}

async function act(command: CalibrationCommand) {
  if (disposed || managerDisabled.value) return
  working.value = true
  error.value = ''
  const channelId = props.status?.activeChannelId
  try {
    const result = await props.command(command)
    if (disposed || props.status?.activeChannelId !== channelId) return
    if (!result) error.value = '操作未完成，请检查仪器状态栏中的原因。'
    else if (command.action === 'select') selected.value = result.calibration?.selectedSetId ?? ''
  } catch (failure) {
    if (!disposed && props.status?.activeChannelId === channelId)
      error.value = failure instanceof Error ? failure.message : '操作失败，请检查仪器状态后重试。'
  } finally { working.value = false }
}

async function exportSelected() {
  if (disposed || !selectedSet.value || working.value) return
  const setId = selected.value
  const channelId = props.status?.activeChannelId
  working.value = true
  error.value = ''
  try {
    const data = await props.exportSet(setId)
    if (disposed || props.status?.activeChannelId !== channelId) return
    const url = URL.createObjectURL(new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' }))
    const link = document.createElement('a')
    link.href = url
    link.download = `vnapro-${setId.replace(/[^a-zA-Z0-9_-]/g, '_')}.json`
    link.click()
    setTimeout(() => URL.revokeObjectURL(url), 1000)
  } catch (failure) {
    if (!disposed && props.status?.activeChannelId === channelId)
      error.value = failure instanceof Error ? failure.message : '导出失败，请重试。'
  }
  finally { working.value = false }
}

async function importSelected(event: Event) {
  const input = event.target as HTMLInputElement
  const file = input.files?.[0]
  input.value = ''
  if (disposed || !file || managerDisabled.value) return
  const channelId = props.status?.activeChannelId
  working.value = true
  error.value = ''
  try {
    if (file.size > 32 * 1024 * 1024) throw new Error('校准文件超过 32 MiB，请选择 VnaPro 校准集 JSON。')
    const data: unknown = JSON.parse(await file.text())
    if (disposed || props.status?.activeChannelId !== channelId) return
    if (!data || typeof data !== 'object' || Array.isArray(data)) throw new Error('请选择 VnaPro 校准集 JSON 对象。')
    if ('schema' in data && data.schema === 3) decodeCalibrationExport(data)
    working.value = false
    await act({ action: 'import', data: data as Record<string, unknown> })
  } catch (failure) {
    if (!disposed && props.status?.activeChannelId === channelId)
      error.value = failure instanceof Error ? failure.message : '读取校准文件失败。'
  } finally { working.value = false }
}
</script>

<template>
  <section class="softtool-section" aria-label="Calibration controls">
    <div class="softtool-readout" data-testid="current-calibration-set" aria-label="Current channel calibration">
      <span>Current Cal Set · Ch{{ status?.activeChannelId ?? 1 }}</span>
      <strong>{{ currentSet?.name ?? 'No calibration data' }}</strong>
      <span class="calibration-state" :class="{ 'is-active': active }">{{ active ? 'User Cal On' : 'Cal Off' }}<template v-if="currentSet"> · {{ calibrationMethodLabel(currentSet.method) }} · {{ calibrationPorts(currentSet.method, currentSet.port1, currentSet.port2) }}</template></span>
    </div>
    <p v-if="calibration?.reason && !active" class="softtool-note" :class="{ warning: !calibration.compatible }">{{ calibration.reason }}</p>
    <div class="softtool-divider" />
    <template v-if="mode === 'cal'">
      <h2 class="softtool-section-title">Calibration</h2>
      <button type="button" class="softtool-button softtool-primary" :disabled="busy || working || (!calibration?.collecting && !canCalibrate)" @click="emit('start')">
        {{ calibration?.collecting ? 'Continue (Manual)…' : 'Start (Manual)…' }}
      </button>
      <p class="softtool-note">选择端口和类型，连接理想 50 Ω 标准，再逐项采集并 Apply。</p>
      <div class="softtool-readout"><span>Channel</span><strong>Ch{{ status?.activeChannelId ?? 1 }}</strong></div>
      <div class="softtool-readout"><span>Frequency range / points</span><strong>{{ status ? frequencyText(status.config.startHz) : '—' }}–{{ status ? frequencyText(status.config.stopHz) : '—' }}<br>{{ status?.config.points }} pts · {{ status ? frequencyText(status.config.ifBandwidthHz) : '—' }} IFBW</strong></div>
      <div class="softtool-divider" />
      <h2 class="softtool-section-title">Calibration Unit</h2>
      <button type="button" class="softtool-button" disabled title="尚未接入电子校准设备">Start (Cal Unit)…</button>
      <p class="softtool-note">尚未接入电子校准设备。TRL 与非理想校准件模型待扩展。</p>
    </template>
    <template v-else>
      <h2 class="softtool-section-title">User Cal</h2>
      <button type="button" class="softtool-button" :class="{ 'is-active': active }" :aria-pressed="active" :disabled="managerDisabled || !calibration?.selectedSetId || (!calibration?.enabled && !calibration?.compatible)" @click="act({ action: 'enable', enabled: !calibration?.enabled })">
        User Cal Active · {{ active ? 'On' : 'Off' }}
        <small v-if="calibration?.enabled && !calibration.compatible">请求 On 未生效 · 点击关闭请求</small>
      </button>
      <div class="softtool-divider" />
      <h2 class="softtool-section-title">Cal Manager</h2>
      <button ref="managerEntry" type="button" class="softtool-button" data-testid="calibration-manager-entry" :disabled="working" @click="managerOpen = true">Cal Manager…</button>
      <p class="softtool-note">{{ calibration?.sets.length ?? 0 }} 套校准集 · 搜索、查看与应用</p>
      <input ref="fileInput" type="file" accept=".json,application/json" class="calibration-file" aria-label="Import VnaPro calibration JSON" @change="importSelected">
    </template>
    <p v-if="calibration?.collecting" class="softtool-note warning">正在收集标准。更改扫描或校准集前，请完成或取消手动校准。</p>
    <p v-if="!canCalibrate && !busy" class="softtool-note warning">采集和应用校准需要控制权及可用的仪器状态。</p>
    <p v-if="error" class="softtool-note warning" role="alert">{{ error }}</p>
    <CalibrationSetManager
      v-if="managerOpen"
      v-model:selected-id="selected"
      :calibration="calibration"
      :channel-id="status?.activeChannelId ?? 1"
      :disabled="managerDisabled"
      :busy="working"
      :error="error"
      @apply="applySelected"
      @remove="deleteSelected"
      @export="exportSelected"
      @import="fileInput?.click()"
      @close="closeManager"
    />
  </section>
</template>

<style scoped>
.calibration-file { display: none; }
.calibration-state.is-active { color: var(--color-success); }
</style>
