<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import type {
  AcquiredFrame,
  FrameCondition,
  Measurement,
  SweepConfiguration,
} from '../../api/types'
import {
  diagramTraceColor,
  isCircularFormat,
  type LayoutMode,
  type TracePresentation,
} from '../../domain/presentation'
import {
  activeMarkerId,
  MARKER_COUNT,
  traceMarkers,
} from '../../domain/markers'
import {
  FORMAT_INFO,
  measurementFormatInfo,
  frequencyText,
  numberText,
} from '../../plot/formatting'
import { markerPlacement, plotRect } from '../../plot/canvasRenderer'
import {
  displaySeries,
  markerResult,
  normalizedX,
  normalizedY,
  referenceLevel,
  usesRelativeAxis,
  scaleCoordinate,
  type MarkerResult,
} from '../../plot/traceMath'
import {
  nearestSmithIndex,
  smithPoint,
  circularReadout,
  smithSeries,
  smithViewport,
} from '../../plot/smithMath'
import { paintMixedDiagram, type MixedCurve } from '../../plot/mixedRenderer'
import { createMarkerPreviewPoints } from '../../plot/markerPreview'
import {
  MarkerDragController,
  createMarkerDragGeometry,
  type DragPoint,
  type MarkerMove,
  type MarkerPreviewPoint,
} from '../../plot/markerDrag'
import MarkerReadout from './MarkerReadout.vue'
import TraceStrip from './TraceStrip.vue'
import DiagramMenu from './DiagramMenu.vue'

const props = defineProps<{
  number: number
  layout: LayoutMode
  frame: AcquiredFrame | null
  measurements: Measurement[]
  allMeasurements: Measurement[]
  presentations: Record<string, TracePresentation>
  activeTraceName: string
  primaryTraceName: string
  selectedDiagram?: number
  canAddTrace?: boolean
  canDeleteDiagram?: boolean
  config: SweepConfiguration
  frameCondition: FrameCondition
  canEditDisplay: boolean
  commitMarker: (value: MarkerMove) => Promise<boolean>
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
  deleteDiagram: [diagram: number]
  arrange: [value: { action: 'overlay' | 'split'; diagram: number }]
}>()
const plotArea = ref<HTMLDivElement | null>(null)
const canvas = ref<HTMLCanvasElement | null>(null)
const width = ref(700)
const height = ref(500)
const screenScale = ref({ x: 1, y: 1 })
let observer: ResizeObserver | undefined
let resolutionQuery: MediaQueryList | undefined
let paintRequest = 0
let dragPointer: number | null = null
let draggingName = ''
let draggingMarkerId = 1
let dragTarget: HTMLElement | null = null
let dragCommandAt: ((point: DragPoint) => MarkerMove | null) | null = null
let suppressMarkerClick = false
let previewName = ''
let previewMarkerId = 1
let previewColor = ''
const dragPreview = ref<{
  name: string
  markerId: number
  color: string
  placement: ReturnType<typeof markerPlacement>
  committing: boolean
  result: MarkerResult | null
} | null>(null)
const markerDrag = new MarkerDragController({
  requestFrame: (callback) => requestAnimationFrame(callback),
  cancelFrame: (id) => cancelAnimationFrame(id),
  preview(point, committing) {
    dragPreview.value = point
      ? {
          name: previewName,
          markerId: previewMarkerId,
          color: previewColor,
          placement: markerPlacement(point, rect.value),
          committing,
          result: point.result ?? null,
        }
      : null
  },
})

const traces = computed(() =>
  props.measurements.flatMap((measurement, index) => {
    const presentation = props.presentations[measurement.name]
    if (!presentation) return []
    const frameTrace = props.frame?.traces.find(
      (item) => item.name === measurement.name,
    )
    return [
      {
        measurement: frameTrace ?? measurement,
        presentation,
        frameTrace,
        index,
        color: diagramTraceColor(index, props.layout),
        scale: presentation.scale,
      },
    ]
  }),
)
const activeTrace = computed(() =>
  traces.value.find((item) => item.measurement.name === props.primaryTraceName),
)
const active = computed(() =>
  props.selectedDiagram === undefined
    ? props.measurements.some((item) => item.name === props.activeTraceName)
    : props.number === props.selectedDiagram,
)
const axisScale = computed(
  () => activeTrace.value?.scale ?? FORMAT_INFO.db.fallback,
)
const axisFormat = computed(
  () => activeTrace.value?.presentation.format ?? 'db',
)
const circularAxis = computed(() => isCircularFormat(axisFormat.value))
const smithRadius = computed(
  () => activeTrace.value?.presentation.smithRadius ?? 1,
)
const smithArea = computed(() =>
  smithViewport(
    width.value,
    height.value,
    screenScale.value.x,
    screenScale.value.y,
  ),
)
const axisDefinition = computed(() =>
  measurementFormatInfo(axisFormat.value, activeTrace.value?.measurement),
)
const axisPhysical = computed(
  () =>
    activeTrace.value?.measurement.kind === 'sParameter' &&
    activeTrace.value.measurement.receiverPort ===
      activeTrace.value.measurement.sourcePort,
)
const relativeAxis = computed(() =>
  usesRelativeAxis(axisScale.value, axisFormat.value),
)
const rect = computed(() => plotRect(width.value, height.value))
const xRange = computed(() => {
  const axis = props.frame?.frequencyHz
  return axis?.length
    ? { min: axis[0]!, max: axis[axis.length - 1]! }
    : { min: props.config.startHz, max: props.config.stopHz }
})
const markerModels = computed(() =>
  traces.value.flatMap((item) => {
    if (!item.presentation.visible) return []
    return traceMarkers(item.presentation).flatMap((marker) => {
      if (!marker.enabled) return []
      const result = markerResult(
        props.frame,
        item.frameTrace,
        marker.frequencyHz,
        item.presentation.format,
        marker.sampleIndex,
        marker.id,
      )
      return result
        ? [
            {
              ...item,
              name: item.measurement.name,
              markerId: marker.id,
              result,
              active:
                item.measurement.name === props.activeTraceName &&
                marker.id === activeMarkerId(item.presentation),
            },
          ]
        : []
    })
  }),
)
const markerReadouts = computed(() =>
  markerModels.value.map((marker) => {
    const preview = dragPreview.value
    const result =
      preview?.name === marker.name && preview.markerId === marker.markerId
        ? preview.result
        : null
    return {
      name: marker.name,
      markerId: marker.markerId,
      color: marker.color,
      format: marker.presentation.format,
      measurement: marker.measurement,
      result: result ?? marker.result,
      active: marker.active,
      preview: Boolean(result),
    }
  }),
)
const plottedMarkers = computed(() =>
  markerModels.value.flatMap((marker) => {
    if (!marker.result.valid) return []
    if (isCircularFormat(marker.presentation.format)) {
      const point = smithPoint(
        { real: marker.result.real!, imaginary: marker.result.imaginary! },
        smithArea.value,
        circleRadius(marker.presentation),
      )
      if (
        !Number.isFinite(point.x) ||
        !Number.isFinite(point.y) ||
        point.x < rect.value.left ||
        point.x > rect.value.left + rect.value.width ||
        point.y < rect.value.top ||
        point.y > rect.value.top + rect.value.height
      )
        return []
      return [{ ...marker, placement: markerPlacement(point, rect.value) }]
    }
    if (!Number.isFinite(marker.result.value)) return []
    const y = normalizedY(
      marker.result.value,
      marker.scale,
      marker.presentation.format,
    )
    if (!Number.isFinite(y) || y < 0 || y > 1) return []
    const point = {
      x:
        rect.value.left +
        normalizedX(
          marker.result.frequencyHz,
          xRange.value.min,
          xRange.value.max,
        ) *
          rect.value.width,
      y: rect.value.top + y * rect.value.height,
    }
    return [{ ...marker, placement: markerPlacement(point, rect.value) }]
  }),
)
const hasDrawable = computed(() =>
  traces.value.some(
    (item) =>
      item.presentation.visible &&
      (isCircularFormat(item.presentation.format)
        ? smithSeries(item.frameTrace).some((point) => point !== null)
        : displaySeries(item.frameTrace, item.presentation.format).some(
            (series) =>
              series.values.some((value) =>
                Number.isFinite(
                  scaleCoordinate(value, item.presentation.format),
                ),
              ),
          )),
  ),
)
const noDrawableMessage = computed(() => {
  if (!props.measurements.length) return 'No measurement assigned'
  if (traces.value.every((item) => !item.presentation.visible))
    return 'All traces hidden'
  if (!props.frame) return 'No acquired frame'
  const undefinedPhase = traces.value.some(
    (item) =>
      item.presentation.visible &&
      item.presentation.format === 'phase' &&
      item.frameTrace?.projection.valueStates.some(
        (state) => state === 'undefined',
      ),
  )
  const zeroDb = traces.value.some(
    (item) =>
      item.presentation.visible &&
      item.presentation.format === 'db' &&
      item.frameTrace?.projection.valueStates.some(
        (state) => state === 'negativeInfinity',
      ),
  )
  if (undefinedPhase) return 'Zero amplitude · phase undefined'
  return zeroDb ? 'Zero amplitude · dB = −∞' : 'No valid measurement points'
})
const yTickDivisions = 10
const yTicks = computed(() =>
  Array.from({ length: yTickDivisions + 1 }, (_, index) => {
    const span =
      scaleCoordinate(axisScale.value.max, axisFormat.value) -
      scaleCoordinate(axisScale.value.min, axisFormat.value)
    const coordinate =
      scaleCoordinate(axisScale.value.max, axisFormat.value) -
      (span * index) / yTickDivisions
    const tick =
      axisFormat.value === 'logMagnitude' ? 10 ** coordinate : coordinate
    let value =
      tick -
      (relativeAxis.value
        ? referenceLevel(axisScale.value, axisFormat.value)
        : 0)
    if (relativeAxis.value && Math.abs(value) < span * 1e-10) value = 0
    return {
      top: rect.value.top + (rect.value.height * index) / yTickDivisions,
      reference:
        Math.abs(tick - referenceLevel(axisScale.value, axisFormat.value)) <=
        span * 1e-9,
      value: `${relativeAxis.value ? 'Δ ' : ''}${relativeAxis.value && value > 0 ? '+' : ''}${numberText(value, 4)}${axisDefinition.value.shortUnit}`,
    }
  }),
)

function circleRadius(presentation: TracePresentation): number {
  return circularAxis.value ? smithRadius.value : presentation.smithRadius
}

function schedulePaint(): void {
  cancelAnimationFrame(paintRequest)
  paintRequest = requestAnimationFrame(() => {
    if (
      !canvas.value ||
      !plotArea.value ||
      width.value <= 0 ||
      height.value <= 0
    )
      return
    const bounds = plotArea.value.getBoundingClientRect()
    screenScale.value = {
      x: bounds.width / width.value,
      y: bounds.height / height.value,
    }
    const curves: MixedCurve[] = traces.value.flatMap((item): MixedCurve[] => {
      if (!item.presentation.visible) return []
      const format = item.presentation.format
      const shared = {
        color: item.color,
        active: item.measurement.name === props.activeTraceName,
      }
      if (isCircularFormat(format)) {
        return [
          {
            ...shared,
            kind: 'circular',
            format: format as 'smith' | 'polar' | 'invSmith',
            radius: circleRadius(item.presentation),
            points: smithSeries(item.frameTrace),
          },
        ]
      }
      return displaySeries(item.frameTrace, format).map((series) => ({
        ...shared,
        kind: 'cartesian',
        format,
        frequencyHz: props.frame?.frequencyHz ?? [],
        values: series.values,
        scale: item.scale,
        dashed: series.component === 'imaginary',
      }))
    })
    const references = traces.value
      .filter(
        (item) =>
          item.presentation.visible &&
          !isCircularFormat(item.presentation.format),
      )
      .map((item) => ({
        scale: item.scale,
        format: item.presentation.format,
        color: item.color,
        active: item.measurement.name === activeTrace.value?.measurement.name,
      }))
    paintMixedDiagram(
      canvas.value,
      {
        width: width.value,
        height: height.value,
        rect: rect.value,
        xMin: xRange.value.min,
        xMax: xRange.value.max,
        axisFormat: axisFormat.value,
        radius: smithRadius.value,
        references,
        curves,
      },
      bounds,
    )
  })
}

function watchResolution(): void {
  resolutionQuery?.removeEventListener('change', resolutionChanged)
  resolutionQuery = window.matchMedia(
    `(resolution: ${window.devicePixelRatio}dppx)`,
  )
  resolutionQuery.addEventListener('change', resolutionChanged)
}
function resolutionChanged(): void {
  if (markerDrag.dragging || markerDrag.pending) cancelDrag()
  watchResolution()
  schedulePaint()
}

function resizePlot(): void {
  if (markerDrag.dragging || markerDrag.pending) cancelDrag()
  schedulePaint()
}

function cancelDragKey(event: KeyboardEvent): void {
  if (event.key === 'Escape' && markerDrag.dragging) cancelDrag()
}

onMounted(() => {
  observer = new ResizeObserver((entries) => {
    const entry = entries[0]
    if (!entry) return
    if (markerDrag.dragging || markerDrag.pending) cancelDrag()
    width.value = entry.contentRect.width
    height.value = entry.contentRect.height
    schedulePaint()
  })
  if (plotArea.value) observer.observe(plotArea.value)
  window.addEventListener('resize', resizePlot)
  window.addEventListener('keydown', cancelDragKey)
  watchResolution()
  schedulePaint()
})
onBeforeUnmount(() => {
  cancelDrag()
  observer?.disconnect()
  window.removeEventListener('resize', resizePlot)
  window.removeEventListener('keydown', cancelDragKey)
  resolutionQuery?.removeEventListener('change', resolutionChanged)
  cancelAnimationFrame(paintRequest)
})
watch(
  [() => props.frame, traces, () => props.activeTraceName, xRange],
  schedulePaint,
  { deep: true },
)
watch(
  () => props.canEditDisplay,
  (editable) => {
    if (!editable && markerDrag.dragging) cancelDrag()
  },
)
watch(
  () =>
    JSON.stringify([
      props.primaryTraceName,
      props.frame?.revision,
      props.frame?.measurementRevision,
      props.frame?.originalPointCount,
      xRange.value,
      traces.value.map((trace) => [
        trace.measurement.name,
        trace.presentation.format,
        trace.presentation.visible,
        traceMarkers(trace.presentation).map((marker) => [
          marker.id,
          marker.enabled,
        ]),
        trace.presentation.smithRadius,
        trace.scale,
      ]),
    ]),
  () => {
    // 普通采集帧变化不取消手势，显示几何或采集轴变化则放弃旧坐标。
    if (markerDrag.dragging || markerDrag.pending) cancelDrag()
  },
)

function selectPane(): void {
  if (props.canEditDisplay && !active.value) emit('selectDiagram', props.number)
}

function markerMoveAt(
  name: string,
  clientX: number,
  clientY: number,
  markerId: number,
): MarkerMove | null {
  const bounds = plotArea.value?.getBoundingClientRect()
  const axis = props.frame?.frequencyHz
  if (!props.canEditDisplay || !bounds || !axis?.length) return null
  const logicalX = ((clientX - bounds.left) * width.value) / bounds.width
  const item = traces.value.find((trace) => trace.measurement.name === name)
  if (item && isCircularFormat(item.presentation.format)) {
    const logicalY = ((clientY - bounds.top) * height.value) / bounds.height
    const index = nearestSmithIndex(
      item?.frameTrace,
      smithArea.value,
      circleRadius(item.presentation),
      logicalX,
      logicalY,
      screenScale.value.x,
      screenScale.value.y,
    )
    return index >= 0
      ? {
          name,
          markerId,
          sampleIndex: props.frame!.sampleIndexes[index]!,
          discrete: true,
        }
      : null
  }
  const fraction = Math.max(
    0,
    Math.min(1, (logicalX - rect.value.left) / rect.value.width),
  )
  return {
    name,
    markerId,
    frequencyHz:
      xRange.value.min + fraction * (xRange.value.max - xRange.value.min),
    discrete: true,
  }
}

function moveMarkerAt(
  name: string,
  clientX: number,
  clientY: number,
  markerId: number,
): void {
  const command = markerMoveAt(name, clientX, clientY, markerId)
  if (command) emit('moveMarker', command)
}

function beginDrag(
  event: PointerEvent,
  name: string,
  markerId = activeMarkerId(props.presentations[name]),
): void {
  if (
    event.button !== 0 ||
    !props.canEditDisplay ||
    markerDrag.dragging ||
    markerDrag.pending
  )
    return
  const marker = plottedMarkers.value.find(
    (item) => item.name === name && item.markerId === markerId,
  )
  const bounds = plotArea.value?.getBoundingClientRect()
  if (!marker || !bounds || !bounds.width || !bounds.height) return
  const points = markerPreviewPoints(name)
  if (!points.length) return
  const offsetX =
    marker && bounds && width.value > 0
      ? event.clientX -
        (bounds.left +
          ((marker.placement.left + marker.placement.tipX) * bounds.width) /
            width.value)
      : 0
  const offsetY =
    marker && bounds && height.value > 0
      ? event.clientY -
        (bounds.top +
          ((marker.placement.top + marker.placement.tipY) * bounds.height) /
            height.value)
      : 0
  const geometry = createMarkerDragGeometry({
    name,
    markerId,
    points,
    smith: isCircularFormat(marker.presentation.format),
    area: rect.value,
    bounds,
    size: { width: width.value, height: height.value },
    scale: screenScale.value,
    range: xRange.value,
    offset: { x: offsetX, y: offsetY },
  })
  const started = markerDrag.begin(
    event.pointerId,
    { x: event.clientX, y: event.clientY },
    geometry.project,
  )
  if (!started) return
  dragCommandAt = geometry.command
  event.preventDefault()
  event.stopPropagation()
  dragPointer = event.pointerId
  draggingName = name
  draggingMarkerId = markerId
  previewName = name
  previewMarkerId = markerId
  previewColor = marker.color
  suppressMarkerClick = false
  dragTarget = event.currentTarget as HTMLElement
  dragTarget.setPointerCapture(event.pointerId)
}

function dragMarker(event: PointerEvent): void {
  if (event.pointerId !== dragPointer) return
  event.preventDefault()
  markerDrag.move(event.pointerId, { x: event.clientX, y: event.clientY })
}

function endDrag(event: PointerEvent): void {
  if (event.pointerId !== dragPointer) return
  if (event.type === 'pointercancel') {
    cancelDrag()
    return
  }
  suppressMarkerClick = true
  const name = draggingName
  const markerId = draggingMarkerId
  const commandAt = dragCommandAt
  void markerDrag
    .finish(
      event.pointerId,
      { x: event.clientX, y: event.clientY },
      async (point) => {
        const command = props.canEditDisplay ? commandAt?.(point) : null
        return command ? props.commitMarker(command) : false
      },
      () => emit('selectMarker', { name, markerId }),
    )
    .catch(() => {
      // 命令入口负责错误提示；失败时手势层只恢复后端权威位置。
    })
  releaseDragCapture()
}

function releaseDragCapture(): void {
  const pointer = dragPointer
  const target = dragTarget
  dragPointer = null
  dragTarget = null
  dragCommandAt = null
  draggingName = ''
  if (pointer !== null && target?.hasPointerCapture(pointer))
    target.releasePointerCapture(pointer)
}

function cancelDrag(): void {
  markerDrag.cancel()
  releaseDragCapture()
}

function lostDragCapture(): void {
  if (markerDrag.dragging) cancelDrag()
}

/** 复制当前快照的投影与读数，持续采集不替换手势选点的数据。 */
function markerPreviewPoints(name: string): MarkerPreviewPoint[] {
  const trace = traces.value.find((item) => item.measurement.name === name)
  if (!trace) return []
  return createMarkerPreviewPoints(
    props.frame,
    trace.frameTrace,
    trace.presentation.format,
    {
      area: rect.value,
      range: xRange.value,
      scale: trace.scale,
      smithArea: smithArea.value,
      smithRadius: circleRadius(trace.presentation),
    },
  )
}

function markerClick(name: string, markerId: number): void {
  if (suppressMarkerClick) {
    suppressMarkerClick = false
    return
  }
  if (props.canEditDisplay) emit('selectMarker', { name, markerId })
}

function markerKey(event: KeyboardEvent, name: string, markerId: number): void {
  if (!props.canEditDisplay || !props.frame) return
  let command: { step?: number; sampleIndex?: number }
  switch (event.key) {
    case 'ArrowLeft':
      command = { step: -(event.shiftKey ? 10 : 1) }
      break
    case 'ArrowRight':
      command = { step: event.shiftKey ? 10 : 1 }
      break
    case 'Home':
      command = { sampleIndex: 0 }
      break
    case 'End':
      command = { sampleIndex: props.frame.originalPointCount - 1 }
      break
    default:
      return
  }
  event.preventDefault()
  event.stopPropagation()
  emit('moveMarker', { name, markerId, ...command, discrete: true })
}

function placeMarker(event: MouseEvent): void {
  const item = activeTrace.value
  if (item?.presentation.marker.enabled)
    moveMarkerAt(
      item.measurement.name,
      event.clientX,
      event.clientY,
      activeMarkerId(item.presentation),
    )
}

function clickPlot(event: MouseEvent): void {
  if (!active.value) {
    selectPane()
    return
  }
  placeMarker(event)
}

function plotKey(event: KeyboardEvent): void {
  // 子级标记控件自行处理按键；绘图区获得焦点时操作当前迹线的活动标记。
  if (event.target !== event.currentTarget) return
  const trace = activeTrace.value
  if (!trace?.presentation.marker.enabled || !props.frame) return
  const result = markerResult(
    props.frame,
    trace.frameTrace,
    trace.presentation.marker.frequencyHz,
    trace.presentation.format,
    trace.presentation.marker.sampleIndex,
    activeMarkerId(trace.presentation),
  )
  if (result)
    markerKey(event, trace.measurement.name, activeMarkerId(trace.presentation))
}
</script>

<template>
  <article
    class="diagram-pane"
    :class="{ active }"
    :aria-label="`Diagram ${number}${active ? ', active' : ''}`"
    :data-diagram="number"
  >
    <header class="trace-header">
      <div class="trace-list" aria-label="Traces">
        <TraceStrip
          v-for="trace in traces"
          :key="trace.measurement.name"
          :measurement="trace.measurement"
          :presentation="trace.presentation"
          :scale="trace.scale"
          :color="trace.color"
          :smith-radius="circleRadius(trace.presentation)"
          :can-edit-display="canEditDisplay"
          :active="trace.measurement.name === activeTraceName"
          @select="emit('selectTrace', $event)"
          @toggle-visibility="emit('toggleVisibility', $event)"
        />
        <span v-if="!traces.length" class="empty-header"
          >Diagram {{ number }}</span
        >
      </div>
      <DiagramMenu
        :number="number"
        :active="active"
        :layout="layout"
        :has-trace="Boolean(activeTrace)"
        :can-edit-display="canEditDisplay"
        :can-add-trace="canAddTrace"
        :can-delete-diagram="canDeleteDiagram"
        :can-add-marker="
          activeTrace &&
          traceMarkers(activeTrace.presentation).filter(
            (marker) => marker.enabled,
          ).length < MARKER_COUNT
        "
        @activate="selectPane"
        @layout="emit('setLayout', { layout: $event, diagram: number })"
        @add-marker="
          activeTrace && emit('addMarker', activeTrace.measurement.name)
        "
        @add-trace="emit('addTrace', { diagram: number, newDiagram: $event })"
        @delete-diagram="emit('deleteDiagram', number)"
        @arrange="emit('arrange', { action: $event, diagram: number })"
      />
    </header>

    <div
      ref="plotArea"
      class="plot-area"
      tabindex="0"
      role="group"
      :aria-label="`Diagram ${number} plot. When the active marker is enabled, click a measured frequency or use Left, Right, Home and End to move the marker.`"
      @click="clickPlot"
      @keydown="plotKey"
    >
      <canvas ref="canvas" class="diagram-canvas" aria-hidden="true" />
      <span
        v-for="(tick, index) in circularAxis ? [] : yTicks"
        :key="`y-${index}`"
        class="y-tick"
        :class="{
          'edge-top': index === 0,
          'edge-bottom': index === 10,
          'reference-tick': tick.reference,
        }"
        :style="{ top: `${tick.top}px` }"
        :title="
          relativeAxis
            ? `相对 Ref ${numberText(referenceLevel(axisScale, axisFormat), 10)} ${axisDefinition.unit} 的偏移`
            : axisDefinition.unit
        "
        >{{ tick.value }}</span
      >
      <MarkerReadout
        :can-edit-display="canEditDisplay"
        :markers="markerReadouts"
        @select="emit('selectMarker', $event)"
      />
      <button
        v-for="marker in plottedMarkers"
        :key="`${marker.name}-M${marker.markerId}`"
        type="button"
        class="marker-handle"
        :disabled="!canEditDisplay"
        :class="{
          active: marker.active,
          'drag-source':
            dragPreview?.name === marker.name &&
            dragPreview.markerId === marker.markerId,
        }"
        :style="{
          left: `${marker.placement.left}px`,
          top: `${marker.placement.top}px`,
          width: `${marker.placement.width}px`,
          height: `${marker.placement.height}px`,
          color: marker.color,
          zIndex: marker.active ? 6 : marker.name === activeTraceName ? 5 : 4,
        }"
        :aria-label="`M${marker.markerId} ${marker.name}, ${frequencyText(marker.result.frequencyHz, 10)}, ${isCircularFormat(marker.presentation.format) ? circularReadout(marker.presentation.format, marker.result, marker.measurement.unit) : numberText(marker.result.value, 8) + ' ' + measurementFormatInfo(marker.presentation.format, marker.measurement).unit}. Arrow keys move between measured frequency points.`"
        @pointerdown="beginDrag($event, marker.name, marker.markerId)"
        @pointermove="dragMarker"
        @pointerup="endDrag"
        @pointercancel="endDrag"
        @lostpointercapture="lostDragCapture"
        @keydown="markerKey($event, marker.name, marker.markerId)"
        @click.stop="markerClick(marker.name, marker.markerId)"
      >
        <span :style="{ top: `${marker.placement.labelTop}px` }"
          >M{{ marker.markerId }}</span
        >
        <svg
          viewBox="0 0 10 9"
          aria-hidden="true"
          :style="{
            left: `${marker.placement.glyphLeft}px`,
            top: `${marker.placement.glyphTop}px`,
          }"
        >
          <path :d="marker.placement.glyphPath" />
        </svg>
      </button>
      <div
        v-if="dragPreview"
        class="marker-handle marker-drag-preview"
        aria-hidden="true"
        :data-marker-drag="dragPreview.committing ? 'committing' : 'moving'"
        :style="{
          transform: `translate3d(${dragPreview.placement.left}px, ${dragPreview.placement.top}px, 0)`,
          width: `${dragPreview.placement.width}px`,
          height: `${dragPreview.placement.height}px`,
          color: dragPreview.color,
        }"
      >
        <span :style="{ top: `${dragPreview.placement.labelTop}px` }"
          >M{{ dragPreview.markerId }}</span
        >
        <svg
          viewBox="0 0 10 9"
          :style="{
            left: `${dragPreview.placement.glyphLeft}px`,
            top: `${dragPreview.placement.glyphTop}px`,
          }"
        >
          <path :d="dragPreview.placement.glyphPath" />
        </svg>
      </div>
      <span v-if="dragPreview" class="marker-drag-status" role="status">{{
        dragPreview.committing
          ? `Setting M${dragPreview.markerId}…`
          : `Moving M${dragPreview.markerId}`
      }}</span>
      <div
        v-if="!hasDrawable"
        class="plot-empty"
        :style="{
          left: `${rect.left}px`,
          top: `${rect.top}px`,
          width: `${rect.width}px`,
          height: `${rect.height}px`,
        }"
      >
        <strong>{{ noDrawableMessage }}</strong>
        <span>{{
          measurements.length
            ? frameCondition.detail
            : 'Select a measurement in Meas to populate this diagram.'
        }}</span>
      </div>
      <span
        v-if="frame && frameCondition.kind !== 'current'"
        class="frame-condition"
        :class="frameCondition.kind"
        :style="{ left: `${rect.left + 5}px`, bottom: '5px' }"
        :title="frameCondition.detail"
        >{{ frameCondition.label }}</span
      >
      <span
        v-if="frame?.decimated"
        class="sample-note"
        :title="`${frame.originalPointCount} source points; ${frame.displayPointCount} supplied display points. Marker uses the complete original frame; CSV carries the returned original sample indexes.`"
      >
        Display {{ frame.displayPointCount }} /
        {{ frame.originalPointCount }} pts
      </span>
      <span v-if="circularAxis" class="smith-note"
        >{{ FORMAT_INFO[axisFormat].label }} ·
        {{
          axisFormat === 'polar' || !axisPhysical
            ? 'Complex Re/Im'
            : axisFormat === 'invSmith'
              ? '归一化导纳 y'
              : 'Γ · 归一化阻抗 z'
        }}
        · 未校准</span
      >
      <span class="sr-only"
        >{{ activeTrace?.measurement.name ?? 'Empty diagram' }}. Format
        {{ FORMAT_INFO[axisFormat].label }}.
        {{
          hasDrawable
            ? 'Acquired measurement samples displayed.'
            : noDrawableMessage
        }}
        {{ frameCondition.detail }}</span
      >
    </div>

    <footer class="channel-row" aria-label="Channel configuration">
      <span class="channel-label" :class="{ active }">Ch1</span>
      <span class="channel-start"
        >Start <b>{{ frequencyText(xRange.min) }}</b></span
      >
      <span class="channel-middle"
        >{{ frame?.originalPointCount ?? config.points }} pts
        <span
          :title="
            frame && frameCondition.kind !== 'current'
              ? 'The retained frame does not carry its acquisition IF bandwidth.'
              : 'Committed acquisition IF bandwidth'
          "
        >
          Bw
          <b>{{
            frame && frameCondition.kind !== 'current'
              ? '—'
              : frequencyText(config.ifBandwidthHz)
          }}</b>
        </span>
      </span>
      <span class="channel-stop"
        >Stop <b>{{ frequencyText(xRange.max) }}</b></span
      >
    </footer>
  </article>
</template>

<style scoped>
.diagram-pane {
  display: grid;
  grid-template-rows: auto minmax(0, 1fr) 20px;
  min-width: 0;
  min-height: 0;
  border: 1px solid #526773;
  background: #000;
  color: #eff4f6;
  font-family: Tahoma, Arial, sans-serif;
}
.diagram-pane.active {
  border-color: #91a4af;
}
.trace-header {
  display: flex;
  min-height: 23px;
  gap: 2px;
  padding: 2px;
  background: #111518;
}
.trace-list {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(min(100%, 220px), 1fr));
  flex: 1;
  min-width: 0;
  align-content: flex-start;
  gap: 2px 4px;
}
.empty-header {
  align-self: center;
  padding-left: 5px;
  color: #a0b1bb;
  font-size: 12px;
}
.plot-area {
  position: relative;
  min-height: 0;
  overflow: hidden;
  background: #000;
}
.plot-area:focus-visible {
  outline: 2px solid #6fc5ea;
  outline-offset: -2px;
}
.diagram-canvas {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  transform-origin: 0 0;
}
.y-tick {
  position: absolute;
  z-index: 2;
  left: 5px;
  padding: 0;
  transform: translateY(-50%);
  text-align: left;
  color: #4b5f6c;
  background: transparent;
  font-size: 11px;
  line-height: 14px;
  white-space: nowrap;
  pointer-events: none;
}
.y-tick.edge-top {
  top: 3px !important;
  transform: none;
}
.y-tick.edge-bottom {
  top: auto !important;
  bottom: 3px;
  transform: none;
}
.y-tick.reference-tick {
  color: #dbe2e6;
}
.marker-handle {
  position: absolute;
  z-index: 4;
  width: 32px;
  height: 32px;
  padding: 0;
  border: 0;
  background: transparent;
  cursor: ew-resize;
  touch-action: none;
}
.marker-handle span {
  position: absolute;
  left: 0;
  width: 100%;
  font-size: 11px;
  font-weight: 400;
  line-height: 16px;
  color: inherit;
  text-align: center;
}
.marker-handle svg {
  position: absolute;
  width: 10px;
  height: 9px;
  fill: currentColor;
  stroke: none;
}
.marker-handle:focus-visible {
  outline: 1px solid #fff;
  outline-offset: 2px;
}
.marker-handle.drag-source {
  opacity: 0;
}
.marker-drag-preview {
  z-index: 7;
  left: 0;
  top: 0;
  pointer-events: none;
  will-change: transform;
}
.marker-drag-status {
  position: absolute;
  z-index: 5;
  right: 5px;
  top: 42px;
  color: #c9d6dd;
  background: #000b;
  font-size: 10px;
  pointer-events: none;
}
.plot-empty {
  position: absolute;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 8px;
  padding: 35px;
  box-sizing: border-box;
  pointer-events: none;
  text-align: center;
  color: #90a3af;
  background: transparent;
}
.plot-empty strong {
  font-size: 16px;
  font-weight: 500;
}
.plot-empty span {
  max-width: 420px;
  font-size: 12px;
  line-height: 1.6;
  color: #728a97;
}
.frame-condition {
  position: absolute;
  z-index: 2;
  padding: 1px 4px;
  color: #d2b887;
  background: #171915dc;
  border-left: 1px solid #bea66e;
  font-size: 11px;
}
.frame-condition.disconnected,
.frame-condition.fault {
  color: #ffb5ab;
  border-color: #ff7d6e;
}
.sample-note {
  position: absolute;
  right: 5px;
  bottom: 5px;
  font-size: 10px;
  color: #a6b4bc;
  background: #000c;
}
.smith-note {
  position: absolute;
  left: 5px;
  bottom: 5px;
  font-size: 10px;
  color: #8ca0a8;
}
.channel-row {
  display: grid;
  grid-template-columns: auto 1fr auto 1fr;
  align-items: center;
  min-width: 0;
  gap: 4px;
  padding: 0 2px;
  color: #fff;
  background: #000;
  font-size: 11px;
  line-height: 18px;
  white-space: nowrap;
}

.channel-label {
  padding: 0 3px;
  font-weight: 400;
  background: transparent;
}

.channel-label.active {
  color: #fff;
  background: #0d75bf;
}
.channel-row b {
  margin-left: 4px;
  color: #f0f4f6;
  font-weight: 400;
}
.channel-middle {
  display: flex;
  gap: 7px;
  color: #eef4f6;
}
.channel-stop {
  justify-self: end;
}
.sr-only {
  position: absolute;
  width: 1px;
  height: 1px;
  padding: 0;
  margin: -1px;
  overflow: hidden;
  clip: rect(0, 0, 0, 0);
  white-space: nowrap;
  border: 0;
}
</style>
