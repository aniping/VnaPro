<script setup lang="ts">
import { computed, ref } from 'vue'
import ToolbarGlyph from './ToolbarGlyph.vue'
import { beginNewTraceDrag } from './newTraceDrag'
import type { LayoutMode } from '../../domain/presentation'
import { isTrashDrag, trashDropIntent, type TrashIntent } from '../../domain/trashDrop'

const props = defineProps<{
  backend?: string
  layout: LayoutMode
  hasTrace: boolean
  canAutoscale: boolean
  canEditDisplay: boolean
  markerEnabled: boolean
  canExport: boolean
  canWrite: boolean
  sweepSupported?: boolean
  canGraphicalZoom?: boolean
  graphicalZoomSelecting?: boolean
  canResetGraphicalZoom?: boolean
  graphicalZoomReason?: string
  busy: boolean
  canDelete: boolean
  canAddTrace: boolean
  canAddChannel?: boolean
  revision?: string
  displayRevision?: string
}>()

const emit = defineEmits<{
  selectTool: [id: string]
  toggleLayout: []
  autoscale: []
  toggleMarker: []
  exportCsv: []
  restart: []
  addTrace: []
  addChannel: []
  deleteTrace: []
  trashDrop: [intent: TrashIntent]
  chooseGraphicalZoom: []
  resetGraphicalZoom: []
}>()
const dropping = ref(false)
const canTrash = computed(() => (props.canWrite || props.canEditDisplay) && !props.busy && Boolean(props.displayRevision))

function restartSweep() {
  if (props.canWrite && props.sweepSupported !== false && !props.busy) emit('restart')
}

function addTrace() {
  if (props.canAddTrace && !props.busy) emit('addTrace')
}

function deleteTrace() {
  if (props.canWrite && !props.busy && props.canDelete) emit('deleteTrace')
}

function dragOverTrash(event: DragEvent) {
  if (!canTrash.value || !isTrashDrag(event)) return
  event.preventDefault()
  event.dataTransfer!.dropEffect = 'move'
  dropping.value = true
}

function dropTrash(event: DragEvent) {
  dropping.value = false
  if (!canTrash.value) return
  const intent = trashDropIntent(event)
  if (!intent || intent.displayRevision !== props.displayRevision ||
    ('revision' in intent && intent.revision !== props.revision) ||
    ((intent.kind === 'trace' || intent.kind === 'diagram') ? !props.canWrite : !props.canEditDisplay) ||
    (intent.kind === 'trace' && !props.canDelete)) return
  event.preventDefault()
  event.stopPropagation()
  emit('trashDrop', intent)
}
</script>

<template>
  <header
    class="instrument-toolbar"
    role="toolbar"
    aria-label="Diagram toolbar"
  >
    <div class="toolbar-group" role="group" aria-label="History">
      <button
        type="button"
        class="toolbar-button"
        disabled
        title="Undo — 尚未实现"
        aria-label="Undo, unavailable"
      >
        <ToolbarGlyph name="undo" />
      </button>
      <button
        type="button"
        class="toolbar-button"
        disabled
        title="Redo — 尚未实现"
        aria-label="Redo, unavailable"
      >
        <ToolbarGlyph name="redo" />
      </button>
    </div>
    <span class="toolbar-separator" aria-hidden="true" />
    <div class="toolbar-group" role="group" aria-label="Diagram view">
      <button
        type="button"
        class="toolbar-button"
        :disabled="!canGraphicalZoom"
        :aria-pressed="Boolean(graphicalZoomSelecting)"
        :title="graphicalZoomReason ?? 'Zoom Select · 框选放大显示区域'"
        aria-label="Zoom Select"
        data-testid="toolbar-zoom-select"
        @click="canGraphicalZoom && emit('chooseGraphicalZoom')"
      >
        <ToolbarGlyph name="zoom" />
      </button>
      <button
        type="button"
        class="toolbar-button"
        :disabled="!canResetGraphicalZoom"
        title="Zoom Reset · 1:1 恢复原始显示区域"
        aria-label="Zoom Reset 1:1"
        data-testid="toolbar-zoom-reset"
        @click="canResetGraphicalZoom && emit('resetGraphicalZoom')"
      >
        <ToolbarGlyph name="zoom-reset" />
      </button>
      <button
        type="button"
        class="toolbar-button"
        :disabled="!canEditDisplay"
        :aria-pressed="layout === 'maximized'"
        :title="
          layout === 'maximized'
            ? 'All diagrams · 返回全部图窗'
            : 'Maximize active diagram · 最大化当前图窗'
        "
        aria-label="Toggle diagram layout"
        data-testid="toolbar-layout"
        @click="emit('toggleLayout')"
      >
        <ToolbarGlyph name="maximize" />
      </button>
    </div>
    <span class="toolbar-separator" aria-hidden="true" />
    <div class="toolbar-group" role="group" aria-label="Trace and marker">
      <button
        type="button"
        class="toolbar-button glyph-accent"
        :disabled="!canAddTrace || busy"
        :title="hasTrace ? 'Add Trace · 单击复制活动迹线；拖到图窗选择新迹线的 S 参数' : 'Add Trace · 为当前通道新建默认迹线'"
        aria-label="Add trace"
        data-testid="toolbar-add-trace"
        :draggable="canAddTrace && !busy"
        @dragstart="beginNewTraceDrag($event, canAddTrace && !busy)"
        @click="addTrace"
      >
        <ToolbarGlyph name="add-trace" />
      </button>
      <button type="button" class="toolbar-button glyph-accent" :disabled="!canAddChannel || busy"
        title="New Ch+Tr · 使用默认设置新建通道、迹线和图窗；复制当前频段请到 Channel Config 使用 Copy Channel + Diagram"
        aria-label="New channel and trace" data-testid="toolbar-add-channel" @click="canAddChannel && !busy && emit('addChannel')">
        <ToolbarGlyph name="add-channel" />
      </button>
      <button
        type="button"
        class="toolbar-button"
        :disabled="!hasTrace || !canEditDisplay"
        :aria-pressed="markerEnabled"
        title="显示或隐藏活动标记"
        aria-label="Toggle marker"
        data-testid="toolbar-marker"
        @click="emit('toggleMarker')"
      >
        <ToolbarGlyph name="marker" />
      </button>
      <span
        class="trash-target"
        :class="{ 'is-dropping': dropping && canTrash }"
        data-testid="trash-target"
        :data-trash-enabled="canTrash"
        title="拖入迹线、Marker 信息字段、单 Marker 或图窗以删除对应对象"
        @dragover="dragOverTrash"
        @dragleave="dropping = false"
        @drop="dropTrash"
      >
        <button
          type="button"
          class="toolbar-button"
          :disabled="!canWrite || busy || !canDelete"
          title="Delete active measurement · 单击删除当前测量；拖入对象删除该对象"
          aria-label="Delete active trace"
          data-testid="toolbar-delete-trace"
          @click="deleteTrace"
        >
          <ToolbarGlyph name="delete" />
        </button>
      </span>
    </div>
    <span class="toolbar-separator" aria-hidden="true" />
    <div class="toolbar-group" role="group" aria-label="Display actions">
      <button
        type="button"
        class="toolbar-button"
        :disabled="!canExport"
        title="Export active channel frame · CSV · 导出活动通道当前显示的真实采样"
        aria-label="Export CSV"
        data-testid="toolbar-export"
        @click="emit('exportCsv')"
      >
        <ToolbarGlyph name="export" />
      </button>
      <button
        type="button"
        class="toolbar-button"
        :disabled="!canAutoscale"
        title="Autoscale active trace once · 按当前数据适配一次刻度"
        aria-label="Autoscale"
        data-testid="toolbar-autoscale"
        @click="emit('autoscale')"
      >
        <ToolbarGlyph name="autoscale" />
      </button>
    </div>
    <span class="toolbar-separator" aria-hidden="true" />
    <div class="toolbar-group" role="group" aria-label="Acquisition">
      <button
        type="button"
        class="toolbar-button glyph-accent"
        :disabled="!canWrite || sweepSupported === false || busy"
        :aria-busy="busy"
        :title="sweepSupported === false
          ? 'Restart sweep unavailable · 当前服务未提供独立扫频模式，请更新服务'
          : 'Restart sweep in active channel · 重启当前通道，保留扫描模式与触发源'"
        aria-label="Restart sweep in active channel"
        data-testid="toolbar-restart"
        @click="restartSweep"
      >
        <ToolbarGlyph name="restart" />
      </button>
      <button
        type="button"
        class="toolbar-button"
        title="Sweep settings · 打开扫描控制"
        aria-label="Sweep settings"
        @click="emit('selectTool', 'sweep')"
      >
        <ToolbarGlyph name="settings" />
      </button>
    </div>
    <span class="toolbar-flex-spacer" />
    <span
      class="toolbar-identity"
      :title="`VnaPro · ${backend ?? '—'}`"
      >Vna<span>Pro</span></span
    >
    <button
      type="button"
      class="toolbar-button toolbar-rf"
      disabled
      title="RF on/off — 尚无可验证的 RF 开关状态"
      aria-label="RF switch, unavailable"
    >
      <ToolbarGlyph name="rf" />
    </button>
  </header>
</template>

<style scoped>
.trash-target { display: inline-flex; flex: 0 0 34px; width: 34px; height: 34px; border-radius: 2px; }
.trash-target.is-dropping { background: #633e31; outline: 1px solid #ffb186; outline-offset: -1px; }
.trash-target[data-trash-enabled="true"] > button:disabled { pointer-events: none; }
</style>
