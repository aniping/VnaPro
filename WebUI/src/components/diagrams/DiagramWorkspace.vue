<script setup lang="ts">
import { computed } from 'vue'
import type {
  AcquiredFrame,
  DiagramDefinition,
  FrameCondition,
  Measurement,
  SweepConfiguration,
} from '../../api/types'
import type { LayoutMode, TracePresentation } from '../../domain/presentation'
import type { MarkerMove } from '../../plot/markerDrag'
import DiagramPane from './DiagramPane.vue'

const props = defineProps<{
  diagrams: DiagramDefinition[]
  frame: AcquiredFrame | null
  measurements: Measurement[]
  presentations: Record<string, TracePresentation>
  activeTraceName: string
  layout: LayoutMode
  config: SweepConfiguration
  frameCondition: FrameCondition
  canEditDisplay: boolean
  commitMarker: (value: MarkerMove) => Promise<boolean>
}>()
const emit = defineEmits<{
  selectTrace: [name: string]
  moveMarker: [value: MarkerMove]
  toggleVisibility: [name: string]
  setLayout: [mode: LayoutMode]
  addMarker: [name: string]
  selectMarker: [value: { name: string; markerId: number }]
}>()
/** 只查找后端已安排图窗的测量 DTO，不在前端决定布局、分组或主坐标迹线。 */
const panes = computed(() =>
  props.diagrams.map((diagram) => ({
    ...diagram,
    measurements: diagram.measurementNames.flatMap((name) => {
      const measurement = props.measurements.find(
        (measurement) => measurement.name === name,
      )
      return measurement ? [measurement] : []
    }),
  })),
)
</script>

<template>
  <section
    class="diagram-workspace"
    :class="[
      layout,
      `pane-count-${panes.length}`,
      { 'mixed-formats': layout === 'single' && panes.length > 1 },
    ]"
    aria-label="Measurement diagrams"
  >
    <div v-if="!diagrams.length" class="diagram-loading" role="status">
      Waiting for instrument display…
    </div>
    <DiagramPane
      v-for="pane in panes"
      :key="`${layout}-${pane.number}`"
      :number="pane.number"
      :layout="layout"
      :frame="frame"
      :measurements="pane.measurements"
      :all-measurements="measurements"
      :presentations="presentations"
      :active-trace-name="activeTraceName"
      :primary-trace-name="pane.primaryMeasurement"
      :config="config"
      :frame-condition="frameCondition"
      :can-edit-display="canEditDisplay"
      :commit-marker="commitMarker"
      @select-trace="emit('selectTrace', $event)"
      @move-marker="emit('moveMarker', $event)"
      @toggle-visibility="emit('toggleVisibility', $event)"
      @set-layout="emit('setLayout', $event)"
      @add-marker="emit('addMarker', $event)"
      @select-marker="emit('selectMarker', $event)"
    />
  </section>
</template>

<style scoped>
.diagram-loading {
  display: grid;
  place-content: center;
  color: #8fa2ae;
  background: #000;
}
.diagram-workspace {
  display: grid;
  min-width: 0;
  min-height: 0;
  padding: 0;
  gap: 2px;
  background: #0a1115;
}
.diagram-workspace.single,
.diagram-workspace.maximized,
.diagram-workspace.pane-count-1 {
  grid-template-columns: minmax(0, 1fr);
  grid-template-rows: minmax(0, 1fr);
}
.diagram-workspace.grid {
  grid-template-columns: repeat(2, minmax(0, 1fr));
  grid-template-rows: repeat(2, minmax(0, 1fr));
}
.diagram-workspace.grid.pane-count-1 {
  grid-template-columns: minmax(0, 1fr);
  grid-template-rows: minmax(0, 1fr);
}
.diagram-workspace.grid.pane-count-2 {
  grid-template-rows: minmax(0, 1fr);
}
.diagram-workspace.single.mixed-formats {
  grid-template-columns: repeat(2, minmax(0, 1fr));
}
</style>
