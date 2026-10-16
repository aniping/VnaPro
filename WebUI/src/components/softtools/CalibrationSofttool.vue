<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type { InstrumentStatus } from '../../api/types'
import type { CalibrationCommand } from '../../api/calibrationTypes'
import { calibrationMethodLabel, calibrationPorts, selectedCalibrationSet } from '../../domain/calibration'
import { frequencyText } from '../../plot/formatting'

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
const disabled = computed(() => !props.canCalibrate || props.busy || working.value)
const managerDisabled = computed(() => disabled.value || calibration.value?.collecting === true)
const selectedSet = computed(() => calibration.value?.sets.find((entry) => entry.id === selected.value))
watch(() => calibration.value?.sets, (sets) => {
  if (!sets?.some((entry) => entry.id === selected.value)) selected.value = calibration.value?.selectedSetId ?? ''
})

async function act(command: CalibrationCommand) {
  if (managerDisabled.value) return
  working.value = true
  error.value = ''
  try {
    const result = await props.command(command)
    if (!result) error.value = '操作未完成，请检查仪器状态栏中的原因。'
    else if (command.action === 'select') selected.value = result.calibration?.selectedSetId ?? ''
  } finally { working.value = false }
}

async function exportSelected() {
  if (!selectedSet.value || working.value) return
  working.value = true
  error.value = ''
  try {
    const data = await props.exportSet(selected.value)
    const url = URL.createObjectURL(new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' }))
    const link = document.createElement('a')
    link.href = url
    link.download = `vnapro-${selected.value.replace(/[^a-zA-Z0-9_-]/g, '_')}.json`
    link.click()
    setTimeout(() => URL.revokeObjectURL(url), 1000)
  } catch (failure) { error.value = failure instanceof Error ? failure.message : '导出失败，请重试。' }
  finally { working.value = false }
}

async function importSelected(event: Event) {
  const input = event.target as HTMLInputElement
  const file = input.files?.[0]
  input.value = ''
  if (!file || managerDisabled.value) return
  error.value = ''
  try {
    if (file.size > 16 * 1024 * 1024) throw new Error('校准文件超过 16 MiB，请选择 VnaPro 校准集 JSON。')
    const data: unknown = JSON.parse(await file.text())
    if (!data || typeof data !== 'object' || Array.isArray(data)) throw new Error('请选择 VnaPro 校准集 JSON 对象。')
    await act({ action: 'import', data: data as Record<string, unknown> })
  } catch (failure) { error.value = failure instanceof Error ? failure.message : '读取校准文件失败。' }
}
</script>

<template>
  <section class="softtool-section" aria-label="Calibration controls">
    <div class="softtool-readout" data-testid="current-calibration-set" aria-label="Current channel calibration">
      <span>Current Cal Set · Ch{{ status?.activeChannelId ?? 1 }}</span>
      <strong>{{ currentSet?.name ?? 'No calibration data' }}</strong>
      <span>{{ active ? 'User Cal On' : 'Cal Off' }}<template v-if="currentSet"> · {{ calibrationMethodLabel(currentSet.method) }} · {{ calibrationPorts(currentSet.method, currentSet.port1, currentSet.port2) }}</template></span>
    </div>
    <p v-if="calibration?.reason" class="softtool-note" :class="{ warning: !calibration.compatible }">{{ calibration.reason }}</p>
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
      <label class="softtool-label" for="calibration-set">Available Cal Sets</label>
      <select id="calibration-set" v-model="selected" class="field-input" :disabled="working">
        <option value="">{{ calibration?.sets.length ? 'Select set…' : 'No calibration data' }}</option>
        <option v-for="set in calibration?.sets ?? []" :key="set.id" :value="set.id">{{ set.name }}{{ set.compatible ? '' : ' · 不适用' }}</option>
      </select>
      <div v-if="selectedSet" class="softtool-readout"><span>{{ calibrationMethodLabel(selectedSet.method) }}</span><strong>{{ calibrationPorts(selectedSet.method, selectedSet.port1, selectedSet.port2) }}</strong></div>
      <p v-if="selectedSet && selectedSet.id !== calibration?.selectedSetId" class="softtool-note">仅查看此集；点击 Use Set 后才会用于 Ch{{ status?.activeChannelId ?? 1 }}。</p>
      <div v-if="selectedSet" class="softtool-readout"><span>Calibrated frequency range</span><strong>{{ selectedSet.startHz === undefined || selectedSet.stopHz === undefined ? '未记录' : `${frequencyText(selectedSet.startHz)}–${frequencyText(selectedSet.stopHz)}` }}</strong><span>Points / IFBW</span><strong>{{ selectedSet.points ?? '未记录' }} pts · {{ selectedSet.ifBandwidthHz === undefined ? '未记录' : frequencyText(selectedSet.ifBandwidthHz) }} IFBW</strong></div>
      <p v-if="selectedSet?.method === 'twoPort'" class="softtool-note">{{ selectedSet.includeIsolation === undefined ? 'Isolation 模型未说明' : selectedSet.includeIsolation ? 'Isolation：已测串扰' : 'Isolation：假定串扰为零' }}</p>
      <div class="softtool-grid">
        <button type="button" class="softtool-button" :disabled="managerDisabled || !selectedSet?.compatible" @click="act({ action: 'select', setId: selected })">Use Set</button>
        <button type="button" class="softtool-button" :disabled="managerDisabled || !selectedSet" @click="act({ action: 'delete', setId: selected })">Delete Set</button>
        <button type="button" class="softtool-button" :disabled="working || !selectedSet" @click="exportSelected">Export…</button>
        <button type="button" class="softtool-button" :disabled="managerDisabled" @click="fileInput?.click()">Import…</button>
      </div>
      <input ref="fileInput" type="file" accept=".json,application/json" class="calibration-file" aria-label="Import VnaPro calibration JSON" @change="importSelected">
      <p class="softtool-note">校准集保存在服务端内存。Export / Import 使用 VnaPro JSON；服务重启后可导入恢复。</p>
    </template>
    <p v-if="calibration?.collecting" class="softtool-note warning">正在收集标准。更改扫描或校准集前，请完成或取消手动校准。</p>
    <p v-if="!canCalibrate && !busy" class="softtool-note warning">采集和应用校准需要控制权及可用的仪器状态。</p>
    <p v-if="error" class="softtool-note warning" role="alert">{{ error }}</p>
  </section>
</template>

<style scoped>
.calibration-file { display: none; }
</style>
