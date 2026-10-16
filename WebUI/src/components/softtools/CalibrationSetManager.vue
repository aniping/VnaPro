<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import type { CalibrationFileFormat, CalibrationSetSummary, CalibrationSnapshot } from '../../api/calibrationTypes'
import { calibrationMethodLabel, calibrationPorts, selectedCalibrationSet } from '../../domain/calibration'
import { frequencyText } from '../../plot/formatting'

const props = defineProps<{
  calibration?: CalibrationSnapshot
  channelId: number
  selectedId: string
  disabled: boolean
  busy: boolean
  error: string
}>()
const emit = defineEmits<{
  'update:selectedId': [id: string]
  apply: []
  remove: []
  export: [format: CalibrationFileFormat]
  import: []
  close: []
}>()
const dialog = ref<HTMLDialogElement | null>(null)
const query = ref('')
const compatibleOnly = ref(false)
const removeIntent = ref('')
const rowButtons = new Map<string, HTMLButtonElement>()
const currentSet = computed(() => selectedCalibrationSet(props.calibration))
const active = computed(() => Boolean(props.calibration?.enabled && props.calibration.compatible))
const selectedSet = computed(() => props.calibration?.sets.find((set) => set.id === props.selectedId))
const visibleSets = computed(() => {
  const search = query.value.trim().toLocaleLowerCase()
  return (props.calibration?.sets ?? []).filter((set) =>
    (!compatibleOnly.value || set.compatible) && (!search || set.name.toLocaleLowerCase().includes(search)),
  )
})
const selectedHidden = computed(() => selectedSet.value && !visibleSets.value.some((set) => set.id === props.selectedId))
const tabStopId = computed(() => visibleSets.value.find((set) => set.id === props.selectedId)?.id ?? visibleSets.value[0]?.id)
const canWrite = computed(() => !props.disabled && !props.busy)
watch([() => props.selectedId, () => props.calibration?.revision, () => props.channelId, () => props.disabled], () => {
  removeIntent.value = ''
}, { flush: 'sync' })

function rangeText(set: CalibrationSetSummary): string {
  return set.startHz === undefined || set.stopHz === undefined ? '未记录' : `${frequencyText(set.startHz)}–${frequencyText(set.stopHz)}`
}
function selectSet(id: string) {
  if (!props.busy) emit('update:selectedId', id)
}
function rememberRow(id: string, element: unknown) {
  if (element instanceof HTMLButtonElement) rowButtons.set(id, element)
  else rowButtons.delete(id)
}
function onRowKeydown(event: KeyboardEvent, id: string) {
  if (props.busy) return
  const index = visibleSets.value.findIndex((set) => set.id === id)
  if (index < 0) return
  const last = visibleSets.value.length - 1
  const next = event.key === 'ArrowDown' ? Math.min(index + 1, last)
    : event.key === 'ArrowUp' ? Math.max(index - 1, 0)
    : event.key === 'Home' ? 0 : event.key === 'End' ? last : -1
  if (next < 0) return
  event.preventDefault()
  const set = visibleSets.value[next]
  if (!set) return
  selectSet(set.id)
  rowButtons.get(set.id)?.focus()
}
function apply() {
  if (!canWrite.value || !selectedSet.value?.compatible) return
  removeIntent.value = ''
  emit('apply')
}
function requestRemove() {
  if (canWrite.value && selectedSet.value) removeIntent.value = selectedSet.value.id
}
function confirmRemove() {
  if (!canWrite.value || !removeIntent.value || selectedSet.value?.id !== removeIntent.value) return
  removeIntent.value = ''
  emit('remove')
}
function close() {
  if (!props.busy) emit('close')
}
function exportSelected(format: CalibrationFileFormat = 'binary') {
  if (!props.busy && selectedSet.value) emit('export', format)
}
function importSet() {
  if (canWrite.value) emit('import')
}
onMounted(() => dialog.value?.showModal())
</script>

<template>
  <Teleport to="body">
    <dialog
      ref="dialog"
      class="cal-manager"
      data-testid="calibration-manager"
      aria-labelledby="cal-manager-title"
      :aria-busy="busy"
      @cancel.prevent="close"
    >
      <header class="manager-header">
        <h2 id="cal-manager-title">Calibration Manager</h2>
        <button type="button" class="close-button" data-testid="calibration-manager-close" :disabled="busy" aria-label="Close Calibration Manager" @click="close">
          <svg viewBox="0 0 16 16" aria-hidden="true"><path d="m4 4 8 8m0-8-8 8" /></svg>
        </button>
      </header>
      <div class="manager-content">
        <section class="channel-state" aria-label="Current channel calibration">
          <span class="channel-label">Ch{{ channelId }} 当前校准集</span>
          <strong :title="currentSet?.name">{{ currentSet?.name ?? '未应用校准集' }}</strong>
          <span class="correction-state" :class="{ enabled: active }">{{ active ? 'User Cal On · 校正生效' : 'Cal Off · 校正未生效' }}</span>
          <p v-if="calibration?.reason" class="current-reason">{{ calibration.reason }}</p>
        </section>
        <div class="manager-toolbar">
          <label class="search-field">
            <span>名称搜索</span>
            <input v-model="query" data-testid="calibration-manager-search" type="search" placeholder="搜索校准集名称" autocomplete="off" autofocus @keydown.esc.prevent.stop="close">
          </label>
          <label class="compatible-filter"><input v-model="compatibleOnly" data-testid="calibration-compatible-only" type="checkbox">仅显示适用校准集</label>
          <span class="result-count" aria-live="polite">{{ visibleSets.length }} / {{ calibration?.sets.length ?? 0 }} 套</span>
        </div>
        <div class="table-scroll" tabindex="0" aria-label="Calibration set list">
          <table>
            <colgroup><col class="name-column"><col class="method-column"><col class="range-column"><col class="points-column"><col class="ifbw-column"><col class="state-column"></colgroup>
            <thead><tr><th scope="col">校准集名称</th><th scope="col">类型 / 端口</th><th scope="col">校准频段</th><th scope="col">点数</th><th scope="col">IFBW</th><th scope="col">适用状态</th></tr></thead>
            <tbody>
              <tr v-for="set in visibleSets" :key="set.id" :class="{ selected: set.id === selectedId }" data-testid="calibration-set-row" :data-set-id="set.id">
                <td class="name-cell">
                  <button :ref="(element) => rememberRow(set.id, element)" type="button" class="set-select" data-testid="calibration-set-select" :data-set-id="set.id" :aria-pressed="set.id === selectedId" :title="set.name" :disabled="busy" :tabindex="set.id === tabStopId ? 0 : -1" @click="selectSet(set.id)" @keydown="onRowKeydown($event, set.id)">
                    <span class="set-name">{{ set.name }}</span>
                    <span v-if="set.id === calibration?.selectedSetId" class="current-badge" data-testid="calibration-current-badge">当前使用</span>
                  </button>
                </td>
                <td><span>{{ calibrationMethodLabel(set.method) }}</span><small>{{ calibrationPorts(set.method, set.port1, set.port2) }}</small></td>
                <td class="numeric">{{ rangeText(set) }}</td>
                <td class="numeric">{{ set.points ?? '未记录' }}</td>
                <td class="numeric">{{ set.ifBandwidthHz === undefined ? '未记录' : frequencyText(set.ifBandwidthHz) }}</td>
                <td :class="{ incompatible: !set.compatible }">{{ set.compatible ? '适用' : '不适用' }}</td>
              </tr>
              <tr v-if="!visibleSets.length"><td colspan="6" class="empty-state">
                <template v-if="calibration?.sets.length">无匹配校准集。<button type="button" class="text-button" @click="query = ''; compatibleOnly = false">清除搜索与筛选</button></template>
                <template v-else>暂无校准集。可导入 .vcal 或 CSV，或关闭窗口后开始手动校准。</template>
              </td></tr>
            </tbody>
          </table>
        </div>
        <section class="selected-details" aria-label="Selected calibration properties">
          <template v-if="selectedSet">
            <div class="selection-heading"><span>浏览选中</span><strong data-testid="calibration-selected-name">{{ selectedSet.name }}</strong></div>
            <p v-if="selectedHidden" class="notice" data-testid="calibration-selected-hidden">此校准集被当前筛选隐藏，仍保留选中；尚未改变通道校准。</p>
            <dl>
              <div><dt>类型 / 端口</dt><dd>{{ calibrationMethodLabel(selectedSet.method) }} · {{ calibrationPorts(selectedSet.method, selectedSet.port1, selectedSet.port2) }}</dd></div>
              <div><dt>校准频段</dt><dd>{{ rangeText(selectedSet) }}</dd></div>
              <div><dt>点数 / IFBW</dt><dd>{{ selectedSet.points ?? '未记录' }} pts · {{ selectedSet.ifBandwidthHz === undefined ? '未记录' : frequencyText(selectedSet.ifBandwidthHz) }} IFBW</dd></div>
              <div v-if="selectedSet.method !== 'onePort'"><dt>Isolation</dt><dd>{{ selectedSet.includeIsolation === undefined ? '未说明' : selectedSet.includeIsolation ? '已测串扰' : '假定串扰为零' }}</dd></div>
            </dl>
            <p v-if="!selectedSet.compatible" class="notice warning">此集与当前通道配置不匹配，不能应用。</p>
            <p v-else-if="selectedSet.id !== calibration?.selectedSetId" class="notice">仅浏览此集；点击“应用到 Ch{{ channelId }}”后才会改变通道校准。</p>
          </template>
          <p v-else class="notice">选择一个校准集以查看属性。浏览和筛选不会改变测量。</p>
        </section>
        <p v-if="calibration?.collecting" class="notice warning">正在采集标准。请完成或取消手动校准后，再应用、导入或删除校准集。</p>
        <p v-else-if="disabled && !busy" class="notice warning">当前不可更改校准集。请取得控制权并等待仪器就绪；仍可浏览和导出。</p>
        <p v-if="error" class="notice error" role="alert">{{ error }}</p>
      </div>
      <footer class="manager-footer">
        <section v-if="removeIntent" class="delete-confirmation" data-testid="calibration-delete-confirmation" aria-label="Confirm calibration deletion">
          <p>确认删除校准集 <strong>{{ selectedSet?.name }}</strong>？此操作会移除此集；若当前通道正在使用，也会影响当前校准。</p>
          <div class="confirmation-actions">
            <button type="button" data-testid="calibration-cancel-delete" :disabled="busy" @click="removeIntent = ''">取消</button>
            <button type="button" class="delete-button" data-testid="calibration-confirm-delete" :disabled="!canWrite || selectedSet?.id !== removeIntent" @click="confirmRemove">确认删除</button>
          </div>
        </section>
        <div class="footer-actions">
          <div class="file-actions">
            <button type="button" data-testid="calibration-import" :disabled="!canWrite" @click="importSet">导入…</button>
            <button type="button" data-testid="calibration-export" :disabled="busy || !selectedSet" @click="exportSelected('binary')">导出二进制</button>
            <button type="button" data-testid="calibration-export-csv" :disabled="busy || !selectedSet" @click="exportSelected('csv')">导出 CSV</button>
            <button type="button" class="delete-button" data-testid="calibration-delete" :disabled="!canWrite || !selectedSet || Boolean(removeIntent)" @click="requestRemove">删除…</button>
          </div>
          <span class="working-state" role="status">{{ busy ? '正在处理，请稍候…' : '' }}</span>
          <button type="button" class="apply-button" data-testid="calibration-apply" :disabled="!canWrite || !selectedSet?.compatible" @click="apply">应用到 Ch{{ channelId }}</button>
        </div>
      </footer>
    </dialog>
  </Teleport>
</template>

<style scoped>
.cal-manager { width: 1000px; height: 640px; max-width: calc(100vw - 24px); max-height: calc(100dvh - 24px); box-sizing: border-box; margin: auto; padding: 0; border: 1px solid #708693; color: var(--color-text); background: var(--color-panel); font: 12px "Segoe UI", Arial, sans-serif; }
.cal-manager[open] { display: grid; grid-template-rows: auto minmax(0, 1fr) auto; }
.cal-manager::backdrop { background: #0009; }
.manager-header { display: flex; align-items: center; justify-content: space-between; gap: 16px; padding: 8px 12px 8px 16px; background: var(--color-toolbar); }
.manager-header h2 { margin: 0; font-size: 15px; font-weight: 600; }
.cal-manager button { min-height: 36px; padding: 7px 12px; border: 1px solid #607b8a; border-radius: 1px; color: var(--color-text); background: var(--color-button); font: inherit; cursor: pointer; }
.cal-manager button:hover:not(:disabled) { background: var(--color-button-hover); border-color: #98b6c6; }
.cal-manager button:disabled { color: var(--color-disabled); background: #2d3d47; border-color: #3f515d; cursor: default; }
.cal-manager :focus-visible { outline: 2px solid var(--color-focus); outline-offset: -3px; }
.cal-manager ::selection { color: var(--color-text); background: var(--color-trace-active); }
.cal-manager .close-button { display: grid; place-items: center; width: 36px; min-height: 32px; padding: 6px; border-color: transparent; background: transparent; }
.close-button svg { width: 18px; height: 18px; fill: none; stroke: currentColor; stroke-width: 1.5; }
.manager-content { display: grid; grid-template-rows: auto auto minmax(100px, 1fr) auto; align-content: start; gap: 12px; min-height: 0; padding: 12px 16px; overflow: auto; }
.channel-state { display: grid; grid-template-columns: auto minmax(0, 1fr) auto; align-items: baseline; gap: 4px 12px; }
.channel-label, .selection-heading > span, dt { color: var(--color-muted); }
.channel-state strong { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; font-size: 13px; font-weight: 600; }
.correction-state { color: var(--color-muted); white-space: nowrap; }
.correction-state.enabled { color: var(--color-success); }
.current-reason { grid-column: 2 / -1; margin: 0; color: var(--color-muted); font-size: 11px; overflow-wrap: anywhere; }
.manager-toolbar { display: flex; align-items: end; gap: 16px; }
.search-field { display: flex; flex: 1; flex-direction: column; gap: 5px; min-width: 140px; color: var(--color-muted); }
.search-field input { width: 100%; min-height: 36px; box-sizing: border-box; padding: 7px 9px; border: 1px solid #708693; border-radius: 1px; color: var(--color-text); background: var(--color-panel-dark); font: inherit; caret-color: var(--color-focus); }
.search-field input::placeholder { color: var(--color-muted); }
.compatible-filter { display: flex; align-items: center; gap: 6px; min-height: 36px; white-space: nowrap; cursor: pointer; }
.compatible-filter input { width: 16px; height: 16px; margin: 0; accent-color: var(--color-active); }
.result-count { min-height: 36px; display: flex; align-items: center; color: var(--color-muted); white-space: nowrap; font-variant-numeric: tabular-nums; }
.table-scroll { min-height: 0; overflow: auto; border: 1px solid #4a5d69; background: var(--color-panel-dark); scrollbar-width: thin; scrollbar-color: #647985 var(--color-panel-dark); }
table { width: 100%; min-width: 820px; border-collapse: collapse; table-layout: fixed; text-align: left; }
.name-column { width: 32%; } .method-column { width: 145px; } .range-column { width: 150px; } .points-column { width: 60px; } .ifbw-column { width: 75px; } .state-column { width: 70px; }
th { position: sticky; top: 0; z-index: 1; padding: 9px 8px; border-bottom: 1px solid #60717b; color: var(--color-muted); background: var(--color-toolbar); font-weight: 500; }
td { height: 34px; padding: 4px 8px; border-bottom: 1px solid #35454e; overflow-wrap: anywhere; }
td small { display: block; margin-top: 3px; color: var(--color-muted); font-size: 10px; }
.numeric { font-variant-numeric: tabular-nums; }
tr.selected { background: #17435f; }
.name-cell { padding: 0; }
.cal-manager .set-select { display: flex; align-items: center; gap: 8px; width: 100%; height: 100%; min-height: 42px; padding: 8px; border: 0; text-align: left; background: transparent; }
.cal-manager .set-select:hover { background: #ffffff0a; }
.set-name { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.current-badge { flex-shrink: 0; padding: 2px 4px; border: 1px solid #82b7d1; color: #c9eaff; font-size: 10px; white-space: nowrap; }
.incompatible, .warning { color: var(--color-warning); }
.empty-state { height: 100px; padding: 16px; color: var(--color-muted); text-align: center; }
.cal-manager .text-button { min-height: 32px; margin-left: 4px; padding: 4px; border: 0; background: transparent; color: #9dd6ff; text-decoration: underline; text-underline-offset: 3px; }
.selected-details { min-width: 0; max-height: 166px; overflow: auto; padding-top: 12px; border-top: 1px solid #4a5d69; }
.selection-heading { display: flex; align-items: baseline; gap: 12px; }
.selection-heading > span { flex-shrink: 0; }
.selection-heading strong { min-width: 0; font-size: 13px; font-weight: 600; overflow-wrap: anywhere; }
dl { display: grid; grid-template-columns: 1fr 1fr; gap: 8px 24px; margin: 10px 0 0; }
dl > div { display: grid; grid-template-columns: 86px minmax(0, 1fr); gap: 8px; }
dd { margin: 0; overflow-wrap: anywhere; font-variant-numeric: tabular-nums; }
.notice { margin: 0; font-size: 11px; line-height: 1.6; overflow-wrap: anywhere; }
.selected-details .notice { margin-top: 8px; color: var(--color-muted); }
.selected-details .warning { color: var(--color-warning); }
.error { color: var(--color-error); }
.manager-footer { padding: 12px 16px; border-top: 1px solid #4a5d69; background: var(--color-panel-dark); }
.footer-actions, .file-actions, .confirmation-actions { display: flex; align-items: center; gap: 8px; }
.file-actions { flex-wrap: wrap; }
.working-state { flex: 1; color: var(--color-muted); font-size: 11px; text-align: right; }
.cal-manager .apply-button { min-width: 140px; color: #fff; background: var(--color-trace-active); border-color: #49bded; }
.cal-manager .apply-button:hover:not(:disabled) { background: #0b75b9; }
.cal-manager .apply-button:disabled { background: #2d3d47; border-color: #3f515d; }
.cal-manager .delete-button:not(:disabled) { color: var(--color-error); }
.delete-confirmation { margin-bottom: 12px; padding-bottom: 12px; border-bottom: 1px solid #4a5d69; }
.delete-confirmation p { max-height: 96px; margin: 0 0 8px; overflow: auto; line-height: 1.6; overflow-wrap: anywhere; }
.confirmation-actions { justify-content: flex-end; }
@media (max-width: 720px) {
  .cal-manager { max-width: calc(100vw - 16px); max-height: calc(100dvh - 16px); }
  .manager-content { gap: 10px; padding: 12px; }
  .channel-state { grid-template-columns: auto minmax(0, 1fr); }
  .correction-state { grid-column: 1 / -1; }
  .current-reason { grid-column: 1 / -1; }
  .manager-toolbar { flex-wrap: wrap; gap: 4px 12px; }
  .search-field { flex-basis: 100%; }
  .result-count { margin-left: auto; }
  .selected-details { max-height: 138px; }
  dl { grid-template-columns: 1fr; gap: 6px; }
  .footer-actions { flex-wrap: wrap; }
  .working-state { order: 3; flex-basis: 100%; text-align: left; }
  .working-state:empty { display: none; }
  .cal-manager .apply-button { margin-left: auto; }
}
@media (max-height: 560px) {
  .manager-content { gap: 8px; }
  .selected-details { max-height: 96px; }
  .manager-footer { padding-top: 8px; padding-bottom: 8px; }
}
</style>
