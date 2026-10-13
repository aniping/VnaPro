<script setup lang="ts">
import { computed, ref } from 'vue'
import type {
  AcquiredFrame,
  DiagramDefinition,
  FrameCondition,
  Measurement,
  SweepConfiguration,
} from '../../api/types'
import {
  diagramTraceColor,
  type LayoutMode,
  type TracePresentation,
  type InfoTableSettings,
  type DisplayAnnotations,
  type MarkerInfoLocation,
} from '../../domain/presentation'
import {
  markerReadouts,
  previewMarkerReadouts,
  type MarkerReadoutPreview,
} from '../../domain/markerReadouts'
import type { MarkerMove } from '../../plot/markerDrag'
import DiagramPane from './DiagramPane.vue'
import InfoTable from './InfoTable.vue'

const props = defineProps<{
  diagrams: DiagramDefinition[]
  frame: AcquiredFrame | null
  measurements: Measurement[]
  presentations: Record<string, TracePresentation>
  activeTraceName: string
  selectedDiagram?: number
  canAddTrace?: boolean
  canAddDiagram?: boolean
  layout: LayoutMode
  config: SweepConfiguration
  frameCondition: FrameCondition
  canEditDisplay: boolean
  commitMarker: (value: MarkerMove) => Promise<boolean>
  infoTable?: InfoTableSettings
  annotations?: DisplayAnnotations
}>()
const emit = defineEmits<{
  selectTrace: [name: string]
  moveMarker: [value: MarkerMove]
  toggleVisibility: [name: string]
  setLayout: [value: { layout: LayoutMode; diagram: number }]
  addMarker: [name: string]
  selectMarker: [value: { name: string; markerId: number }]
  selectDiagram: [diagram: number]
  addTrace: [value: { diagram: number; newDiagram: boolean }]
  newTrace: [diagram: number]
  deleteDiagram: [diagram: number]
  arrange: [value: { action: 'overlay' | 'split'; diagram: number }]
  relocateMarkerInfo: [value: { name: string; location: MarkerInfoLocation }]
}>()
const markerPreview = ref<MarkerReadoutPreview | null>(null)
const infoSettings = computed(
  () => props.infoTable ?? { visible: false, position: 'bottom' as const },
)
const annotationSettings = computed(
  () =>
    props.annotations ?? {
      activeTraceOnly: false,
      stimulusInfoVisible: true,
      fontScale: 100,
    },
)
/** 全局表使用全部持久化图窗，不因 Maximized 暂时隐藏绘图区而丢失字段归属。 */
const infoFields = computed(() =>
  props.diagrams.flatMap((diagram) =>
    diagram.markerInfoVisible === false
      ? []
      : diagram.measurementNames.flatMap((name, index) => {
          const measurement = props.measurements.find(
            (measurement) => measurement.name === name,
          )
          const presentation = props.presentations[name]
          if (
            !measurement ||
            !presentation ||
            (presentation.markerInfoLocation ?? 'diagram') !== 'table' ||
            (annotationSettings.value.activeTraceOnly &&
              name !== props.activeTraceName)
          )
            return []
          const color = diagramTraceColor(index, props.layout)
          const markers = previewMarkerReadouts(
            markerReadouts(
              props.frame,
              props.frame?.traces.find((trace) => trace.name === name),
              measurement,
              presentation,
              color,
              props.activeTraceName,
            ),
            markerPreview.value,
          )
          return markers.length ? [{ name, color, markers }] : []
        }),
  ),
)
/** 只查找后端已安排图窗的测量 DTO，不在前端决定布局、分组或主坐标迹线。 */
const panes = computed(() =>
  props.diagrams
    .filter(
      (diagram) =>
        props.layout !== 'maximized' ||
        diagram.number ===
          (props.selectedDiagram ??
            props.diagrams.find((item) =>
              item.measurementNames.includes(props.activeTraceName),
            )?.number ??
            props.diagrams[0]?.number),
    )
    .map((diagram) => ({
      ...diagram,
      measurements: diagram.measurementNames.flatMap((name) => {
        const measurement = props.measurements.find(
          (measurement) => measurement.name === name,
        )
        return measurement ? [measurement] : []
      }),
    })),
)
const gridStyle = computed(() => {
  const columns = Math.max(
    1,
    Math.min(2, Math.ceil(Math.sqrt(panes.value.length))),
  )
  const rows = Math.max(1, Math.ceil(panes.value.length / columns))
  return {
    gridTemplateColumns: `repeat(${columns}, minmax(0, 1fr))`,
    gridTemplateRows: `repeat(${rows}, minmax(150px, 1fr))`,
  }
})
</script>

<template>
  <div
    class="diagram-display"
    :class="{
      'with-info-table': infoSettings.visible,
      [`info-${infoSettings.position}`]: infoSettings.visible,
    }"
  >
    <section
      class="diagram-workspace"
      :class="[
        layout,
        `pane-count-${panes.length}`,
        { 'mixed-formats': layout === 'single' && panes.length > 1 },
      ]"
      aria-label="Measurement diagrams"
      :style="gridStyle"
    >
      <div v-if="!diagrams.length" class="diagram-loading" role="status">
        Waiting for instrument display…
      </div>
      <DiagramPane
        v-for="pane in panes"
        :key="`${pane.number}-${JSON.stringify(pane.measurementNames)}`"
        :number="pane.number"
        :layout="layout"
        :frame="frame"
        :measurements="pane.measurements"
        :all-measurements="measurements"
        :presentations="presentations"
        :active-trace-name="activeTraceName"
        :selected-diagram="selectedDiagram"
        :can-add-trace="canAddTrace"
        :can-add-diagram="canAddDiagram"
        :can-delete-diagram="diagrams.length > 1"
        :primary-trace-name="pane.primaryMeasurement"
        :config="config"
        :frame-condition="frameCondition"
        :can-edit-display="canEditDisplay"
        :commit-marker="commitMarker"
        :marker-info-visible="pane.markerInfoVisible"
        :annotations="annotationSettings"
        :info-table-visible="infoSettings.visible"
        @select-trace="emit('selectTrace', $event)"
        @move-marker="emit('moveMarker', $event)"
        @toggle-visibility="emit('toggleVisibility', $event)"
        @set-layout="emit('setLayout', $event)"
        @add-marker="emit('addMarker', $event)"
        @select-marker="emit('selectMarker', $event)"
        @select-diagram="emit('selectDiagram', $event)"
        @add-trace="emit('addTrace', $event)"
        @new-trace="emit('newTrace', $event)"
        @delete-diagram="emit('deleteDiagram', $event)"
        @arrange="emit('arrange', $event)"
        @marker-preview="markerPreview = $event"
        @relocate-marker-info="emit('relocateMarkerInfo', $event)"
      />
    </section>
    <InfoTable
      v-if="infoSettings.visible"
      :fields="infoFields"
      :position="infoSettings.position"
      :annotations="annotationSettings"
      :can-edit-display="canEditDisplay"
      @select="emit('selectMarker', $event)"
      @relocate="emit('relocateMarkerInfo', $event)"
    />
  </div>
</template>

<style scoped>
.diagram-display {
  display: flex;
  flex-direction: column;
  min-width: 0;
  min-height: 0;
  overflow: hidden;
}
.diagram-display.info-left {
  flex-direction: row-reverse;
}
.diagram-display.info-right {
  flex-direction: row;
}
.diagram-loading {
  display: grid;
  place-content: center;
  color: #8fa2ae;
  background: #000;
}
.diagram-workspace {
  flex: 1 1 0;
  display: grid;
  min-width: 0;
  min-height: 0;
  padding: 0;
  gap: 2px;
  background: #0a1115;
  overflow: auto;
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
