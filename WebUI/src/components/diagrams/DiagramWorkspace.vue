<script setup lang="ts">
import { computed } from 'vue'
import type { AcquiredFrame, FrameCondition, Measurement, SweepConfiguration } from '../../api/types'
import type { LayoutMode, TracePresentation } from '../../domain/presentation'
import DiagramPane from './DiagramPane.vue'

const props = defineProps<{
  frame: AcquiredFrame | null
  measurements: Measurement[]
  presentations: Record<string, TracePresentation>
  activeTraceName: string
  layout: LayoutMode
  config: SweepConfiguration
  frameCondition: FrameCondition
}>()
const emit = defineEmits<{
  selectTrace: [name: string]
  moveMarker: [value: { name: string; frequencyHz: number; sampleIndex: number }]
  toggleVisibility: [name: string]
  setLayout: [mode: LayoutMode]
  addMarker: []
}>()
const panes = computed(() => {
  const visible = props.measurements.slice(0, 4)
  if (props.layout === 'grid' && visible.length) {
    return visible.map((measurement, index) => ({ number: index + 1, measurements: [measurement] }))
  }
  if (props.layout === 'maximized' && visible.length) {
    const index = Math.max(0, visible.findIndex(item => item.name === props.activeTraceName))
    return [{ number: index + 1, measurements: [visible[index]!] }]
  }
  const smith = visible.filter(item => props.presentations[item.name]?.format === 'smith')
  const cartesian = visible.filter(item => props.presentations[item.name]?.format !== 'smith')
  if (smith.length && cartesian.length) {
    return [{ number: 1, measurements: cartesian }, { number: 2, measurements: smith }]
  }
  return [{ number: 1, measurements: visible }]
})
</script>

<template>
  <section class="diagram-workspace" :class="[layout, `pane-count-${panes.length}`, { 'mixed-formats': layout === 'single' && panes.length > 1 }]" aria-label="Measurement diagrams">
    <DiagramPane v-for="pane in panes" :key="`${layout}-${pane.number}`" :number="pane.number" :layout="layout"
      :frame="frame" :measurements="pane.measurements" :all-measurements="measurements"
      :presentations="presentations" :active-trace-name="activeTraceName" :config="config" :frame-condition="frameCondition"
      @select-trace="emit('selectTrace', $event)" @move-marker="emit('moveMarker', $event)"
      @toggle-visibility="emit('toggleVisibility', $event)" @set-layout="emit('setLayout', $event)" @add-marker="emit('addMarker')" />
  </section>
</template>

<style scoped>
.diagram-workspace { display: grid; min-width: 0; min-height: 0; padding: 0; gap: 2px; background: #0a1115; }
.diagram-workspace.single, .diagram-workspace.maximized, .diagram-workspace.pane-count-1 { grid-template-columns: minmax(0, 1fr); grid-template-rows: minmax(0, 1fr); }
.diagram-workspace.grid { grid-template-columns: repeat(2, minmax(0, 1fr)); grid-template-rows: repeat(2, minmax(0, 1fr)); }
.diagram-workspace.grid.pane-count-1 { grid-template-columns: minmax(0, 1fr); grid-template-rows: minmax(0, 1fr); }
.diagram-workspace.grid.pane-count-2 { grid-template-rows: minmax(0, 1fr); }
.diagram-workspace.single.mixed-formats { grid-template-columns: repeat(2, minmax(0, 1fr)); }
</style>
