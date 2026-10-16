<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, ref, watch } from 'vue'
import type { InstrumentStatus, PendingOperation } from '../../api/types'
import type { CalibrationCommand, CalibrationFileFormat } from '../../api/calibrationTypes'
import { calibrationFileBlob, decodeCalibrationFile, readCalibrationFile } from '../../api/calibrationFiles'
import { calibrationMethodLabel, calibrationPorts, selectedCalibrationSet } from '../../domain/calibration'
import { applicabilityMapping, applicabilityRemedy, applicabilityWarnings, calibrationCanApply, calibrationEffective, calibrationStateLabel, calibrationTone } from '../../domain/calibrationApplicability'
import { frequencyText } from '../../plot/formatting'
import CalibrationSetManager from './CalibrationSetManager.vue'

const props = defineProps<{
  mode: 'cal' | 'cal-use'
  status: InstrumentStatus | null
  canCalibrate: boolean
  busy: boolean
  operation?: PendingOperation | null
  operationError?: string | null
  command: (command: CalibrationCommand) => Promise<InstrumentStatus | null>
  exportSet: (setId: string, format?: CalibrationFileFormat) => Promise<Record<string, unknown>>
}>()
const emit = defineEmits<{ start: [] }>()
const calibration = computed(() => props.status?.calibration)
const active = computed(() => calibrationEffective(calibration.value))
const warnings = computed(() => applicabilityWarnings(calibration.value?.applicability))
const currentSet = computed(() => selectedCalibrationSet(calibration.value))
const selected = ref(calibration.value?.selectedSetId ?? '')
const working = ref(false)
const workPhase = ref<'command' | 'activate' | 'readFile' | 'export'>('command')
const busyText = computed(() => {
  if (workPhase.value === 'readFile') return '正在读取并检查校准文件…'
  if (workPhase.value === 'export') return '正在导出校准文件…'
  if (props.operation?.kind === 'calibration') return props.operation.readingBack
    ? '设备已完成，正在读回并确认校准状态…'
    : workPhase.value === 'activate' ? '正在准备校准系数并应用到通道，等待设备完成…' : '正在等待设备完成校准操作…'
  return workPhase.value === 'activate' ? '正在提交应用并启用校准集请求…' : '正在提交校准操作请求…'
})
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
  if (calibrationCanApply(selectedSet.value)) return act({ action: 'activate', setId: selected.value })
}

function deleteSelected() {
  if (selectedSet.value) return act({ action: 'delete', setId: selected.value })
}

async function act(command: CalibrationCommand) {
  if (disposed || managerDisabled.value) return
  working.value = true
  workPhase.value = command.action === 'activate' ? 'activate' : 'command'
  error.value = ''
  const channelId = props.status?.activeChannelId
  try {
    const result = await props.command(command)
    if (disposed || props.status?.activeChannelId !== channelId) return
    if (!result) error.value = props.operationError || '操作未完成，请检查仪器状态栏中的原因。'
    else if (command.action === 'activate' || command.action === 'select') selected.value = result.calibration?.selectedSetId ?? ''
  } catch (failure) {
    if (!disposed && props.status?.activeChannelId === channelId)
      error.value = failure instanceof Error ? failure.message : '操作失败，请检查仪器状态后重试。'
  } finally { working.value = false }
}

async function exportSelected(format: CalibrationFileFormat = 'binary') {
  if (disposed || !selectedSet.value || working.value) return
  const setId = selected.value
  const channelId = props.status?.activeChannelId
  working.value = true
  workPhase.value = 'export'
  error.value = ''
  try {
    const data = await props.exportSet(setId, format)
    if (disposed || props.status?.activeChannelId !== channelId) return
    const file = decodeCalibrationFile(data)
    if (file.format !== format) throw new Error('校准文件格式与请求不符。')
    const url = URL.createObjectURL(calibrationFileBlob(file))
    const link = document.createElement('a')
    link.href = url
    link.download = file.filename
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
  workPhase.value = 'readFile'
  error.value = ''
  try {
    const command = await readCalibrationFile(file)
    if (disposed || props.status?.activeChannelId !== channelId) return
    working.value = false
    await act(command)
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
      <span class="calibration-state" :class="[calibrationTone(calibration), { 'is-active': active }]" data-testid="calibration-correction-state">{{ calibrationStateLabel(calibration) }}<template v-if="currentSet"> · {{ calibrationMethodLabel(currentSet.method) }} · {{ calibrationPorts(currentSet.method, currentSet.port1, currentSet.port2) }}</template></span>
    </div>
    <p v-if="calibration?.reason && !active" class="softtool-note" :class="{ warning: !calibration.compatible }">{{ calibration.reason }} {{ applicabilityRemedy(calibration.applicability) }}</p>
    <p v-if="active && calibration?.applicability" class="softtool-note" data-testid="calibration-active-mapping">{{ applicabilityMapping(calibration.applicability) }}</p>
    <p v-for="warning in warnings" :key="warning" class="softtool-note warning" data-testid="calibration-active-warning">{{ warning }}</p>
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
      <input ref="fileInput" type="file" accept=".vcal,.csv" class="calibration-file" aria-label="Import VnaPro calibration file" @change="importSelected">
    </template>
    <p v-if="calibration?.collecting" class="softtool-note warning">正在收集标准。更改扫描或校准集前，请完成或取消手动校准。</p>
    <p v-if="!canCalibrate && !busy" class="softtool-note warning">采集和应用校准需要控制权及可用的仪器状态。</p>
    <p v-if="error" class="softtool-note warning" role="alert">{{ error }}</p>
    <CalibrationSetManager
      v-if="managerOpen"
      v-model:selected-id="selected"
      :calibration="calibration"
      :config="status?.config"
      :channel-id="status?.activeChannelId ?? 1"
      :disabled="managerDisabled"
      :busy="working"
      :error="error"
      :busy-text="busyText"
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
.calibration-state.warning { color: var(--color-warning); }
</style>
