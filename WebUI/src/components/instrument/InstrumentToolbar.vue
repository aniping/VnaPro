<script setup lang="ts">
import ToolbarGlyph from './ToolbarGlyph.vue'
import { beginNewTraceDrag } from './newTraceDrag'
import type { LayoutMode } from '../../domain/presentation'

const props = defineProps<{
  backend?: string
  layout: LayoutMode
  hasTrace: boolean
  canAutoscale: boolean
  canEditDisplay: boolean
  markerEnabled: boolean
  canExport: boolean
  canWrite: boolean
  busy: boolean
  canDelete: boolean
  canAddTrace: boolean
}>()

const emit = defineEmits<{
  selectTool: [id: string]
  toggleLayout: []
  autoscale: []
  toggleMarker: []
  exportCsv: []
  single: []
  addTrace: []
  deleteTrace: []
}>()

function singleSweep() {
  if (props.canWrite && !props.busy) emit('single')
}

function addTrace() {
  if (props.canAddTrace && props.hasTrace && !props.busy) emit('addTrace')
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
        disabled
        title="Zoom area — 尚未实现"
        aria-label="Zoom area, unavailable"
      >
        <ToolbarGlyph name="zoom" />
      </button>
      <button
        type="button"
        class="toolbar-button"
        disabled
        title="Zoom 1:1 — 尚未实现"
        aria-label="Zoom 1:1, unavailable"
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
        title="Add Trace · 单击复制活动迹线；拖到图窗选择新迹线的 S 参数"
        aria-label="Add trace"
        data-testid="toolbar-add-trace"
        :draggable="canAddTrace && !busy"
        @dragstart="beginNewTraceDrag($event, canAddTrace && !busy)"
        @click="addTrace"
      >
        <ToolbarGlyph name="add-trace" />
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
      <button
        type="button"
        class="toolbar-button"
        :disabled="!canWrite || busy || !canDelete"
        title="Delete active measurement · 删除当前测量"
        aria-label="Delete active trace"
        data-testid="toolbar-delete-trace"
        @click="emit('deleteTrace')"
      >
        <ToolbarGlyph name="delete" />
      </button>
    </div>
    <span class="toolbar-separator" aria-hidden="true" />
    <div class="toolbar-group" role="group" aria-label="Display actions">
      <button
        type="button"
        class="toolbar-button"
        :disabled="!canExport"
        title="Export displayed frame · CSV"
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
        :disabled="!canWrite || busy"
        :aria-busy="busy"
        title="Single sweep · 单次扫描完成后进入 HOLD"
        aria-label="Single sweep"
        data-testid="toolbar-single"
        @click="singleSweep"
      >
        <ToolbarGlyph name="single" />
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
