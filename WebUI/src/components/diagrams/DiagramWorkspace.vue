<script setup lang="ts">
import { computed, ref } from 'vue'
import type {
  AcquiredFrame,
  DiagramDefinition,
  FrameCondition,
  Measurement,
  SweepConfiguration,
  TraceDestination,
} from '../../api/types'
import {
  diagramTraceColor,
  type LayoutMode,
  type TracePresentation,
  type InfoTableSettings,
  type DisplayAnnotations,
  type MarkerInfoLocation,
  type MarkerInfoArea,
} from '../../domain/presentation'
import {
  markerReadouts,
  previewMarkerReadouts,
  type MarkerReadoutPreview,
} from '../../domain/markerReadouts'
import type { MarkerMove } from '../../plot/markerDrag'
import type { TraceMoveRequest } from '../../domain/diagramLayout'
import DiagramPane from './DiagramPane.vue'
import InfoTable from './InfoTable.vue'

const props = defineProps<{
  diagrams: DiagramDefinition[]
  frame: AcquiredFrame | null
  measurements: Measurement[]
  presentations: Record<string, TracePresentation>
  activeTraceName: string
  selectedDiagram?: number
  displayRevision?: string
  canAddTrace?: boolean
  canAddDiagram?: boolean
  layout: LayoutMode
  gridColumns: number
  gridRows: number
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
  newTrace: [destination: TraceDestination]
  moveTrace: [value: TraceMoveRequest]
  deleteDiagram: [diagram: number]
  arrange: [value: { action: 'overlay' | 'split'; diagram: number }]
  relocateMarkerInfo: [
    value: {
      name: string
      location: MarkerInfoLocation
      area?: MarkerInfoArea
    },
  ]
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
  // 只将后端保存的排列转换为像素布局，最大化不覆盖正式网格。
  const columns = props.layout === 'maximized' ? 1 : props.gridColumns
  const rows = props.layout === 'maximized' ? 1 : props.gridRows
  return {
    gridTemplateColumns: `repeat(${columns}, minmax(0, 1fr))`,
    gridTemplateRows: `repeat(${rows}, minmax(0, 1fr))`,
  }
})
const relativeLayout = computed(
  () =>
    props.layout !== 'maximized' &&
    panes.value.length > 0 &&
    panes.value.every((pane) => pane.region),
)
function paneStyle(pane: DiagramDefinition) {
  const region = relativeLayout.value ? pane.region : undefined
  return region
    ? {
        position: 'absolute' as const,
        left: `calc(${region.left * 100}% + 1px)`,
        top: `calc(${region.top * 100}% + 1px)`,
        width: `calc(${region.width * 100}% - 2px)`,
        height: `calc(${region.height * 100}% - 2px)`,
      }
    : undefined
}
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
        {
          'mixed-formats': layout === 'single' && panes.length > 1,
          'relative-layout': relativeLayout,
        },
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
        :style="paneStyle(pane)"
        :display-revision="displayRevision"
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
        @move-trace="emit('moveTrace', $event)"
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
  overflow: hidden;
}
.diagram-workspace.relative-layout {
  position: relative;
  display: block;
}
</style>
