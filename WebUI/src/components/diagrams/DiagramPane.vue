<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, onScopeDispose, ref, watch } from 'vue'
import type {
  AcquiredFrame,
  ChannelStatus,
  FrameCondition,
  FrameResponse,
  Measurement,
  SweepConfiguration,
  TraceDestination,
  StimulusZoomSelection,
} from '../../api/types'
import {
  diagramTraceColor,
  isCircularFormat,
  type LayoutMode,
  type TracePresentation,
  type DisplayAnnotations,
  type MarkerInfoLocation,
  type MarkerInfoArea,
  type DisplayScale,
} from '../../domain/presentation'
import {
  markerInfoDragName,
  MARKER_INFO_DRAG_TYPE,
  type MarkerReadoutPreview,
} from '../../domain/markerReadouts'
import { measuredTraceTitleLayout } from '../../plot/titleLayout'
import {
  MARKER_INFO_AREAS,
  markerInfoAreaAt,
} from '../../domain/markerInfoArea'
import type { TraceMoveRequest } from '../../domain/diagramLayout'
import { measurementState } from '../../domain/measurements'
import { frameForMeasurement, measurementChannel } from '../../domain/channels'
import { currentSweepProgress, sweepTraceSegments } from '../../domain/sweepProgress'
import { sweepCursorPoint } from '../../plot/sweepCursor'
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
  scaleSettingNumber,
} from '../../plot/formatting'
import { markerPlacement, plotRect } from '../../plot/canvasRenderer'
import { spacedAxisLabels } from '../../plot/axisLabelSpacing'
import { axisTypography } from '../../plot/axisTypography'
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
  smithPoint,
  circularReadout,
  smithSeries,
  smithViewport,
} from '../../plot/smithMath'
import {
  paintMixedDiagram,
  type MixedBackground,
} from '../../plot/mixedRenderer'
import { hitTestTrace, type SelectableCurve } from '../../plot/traceHitTest'
import { createMarkerPreviewPoints } from '../../plot/markerPreview'
import {
  MarkerDragController,
  createMarkerDragGeometry,
  type DragPoint,
  type MarkerMove,
  type MarkerPreviewPoint,
  type MarkerDragPoint,
} from '../../plot/markerDrag'
import MarkerReadout from './MarkerReadout.vue'
import TraceStrip from './TraceStrip.vue'
import DiagramMenu from './DiagramMenu.vue'
import TraceDropZones from './TraceDropZones.vue'
import {
  isTraceDrag,
  traceDragIntent,
  traceDropPosition,
  type TraceDropPosition,
} from './traceDrag'
import { isNewTraceDrag } from '../instrument/newTraceDrag'
import ReferencePositionHandle from './ReferencePositionHandle.vue'
import PlotInteractionLayer from './PlotInteractionLayer.vue'
import type { TracePanMove } from '../../plot/plotGestures'
import {
  FULL_GRAPHICAL_ZOOM,
  composeGraphicalZoom,
  graphicalFrequencyRange,
  graphicalTraceScale,
  isGraphicalZoomed,
  panGraphicalZoom,
  type GraphicalZoomViewport,
} from '../../plot/graphicalZoom'
import { hitTrashTarget, type TrashIntent } from '../../domain/trashDrop'
import {
  previewReferenceScale,
  type ReferencePositionMove,
} from '../../plot/referencePositionDrag'

const props = defineProps<{
  number: number
  layout: LayoutMode
  frame: AcquiredFrame | null
  channelFrames?: Record<number, FrameResponse>
  channels?: ChannelStatus[]
  activeChannelId?: number
  connected?: boolean
  measurements: Measurement[]
  allMeasurements: Measurement[]
  presentations: Record<string, TracePresentation>
  activeTraceName: string
  primaryTraceName: string
  selectedDiagram?: number
  displayRevision?: string
  revision?: string
  zoomSelecting?: boolean
  graphicalZoomSelecting?: boolean
  graphicalZoomReset?: number
  canZoom?: boolean
  commitStimulusZoom?: (value: StimulusZoomSelection) => Promise<boolean>
  commitTracePan?: (value: TracePanMove) => Promise<boolean>
  commitTrash?: (value: TrashIntent) => Promise<boolean>
  canAddTrace?: boolean
  canAddDiagram?: boolean
  canDeleteDiagram?: boolean
  config: SweepConfiguration
  frameCondition: FrameCondition
  canEditDisplay: boolean
  canContinueReferenceDrag?: boolean
  commitMarker: (value: MarkerMove) => Promise<boolean>
  commitReferencePosition: (value: ReferencePositionMove) => Promise<boolean>
  markerInfoVisible?: boolean
  annotations?: DisplayAnnotations
  infoTableVisible?: boolean
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
  cancelZoom: []
  cancelGraphicalZoom: []
  graphicalZoomChanged: [value: { diagram: number; zoomed: boolean }]
  arrange: [value: { action: 'overlay' | 'split'; diagram: number }]
  markerPreview: [value: MarkerReadoutPreview | null]
  relocateMarkerInfo: [
    value: {
      name: string
      location: MarkerInfoLocation
      area?: MarkerInfoArea
    },
  ]
}>()
const traceList = ref<HTMLElement | null>(null)
const traceDrop = ref<TraceDropPosition | null>(null)
const movingTrace = ref(false)
const titleLayout = ref({
  columns: 1,
  slotWidth: 0,
  compact: false,
  nameWidth: 0,
  chipWidth: 0,
  formatWidth: 0,
  sections: { format: true, scale: true, reference: true, state: true },
})
let titleObserver: ResizeObserver | undefined
let titleMeasure: CanvasRenderingContext2D | null = null
const plotArea = ref<HTMLDivElement | null>(null)
const canvas = ref<HTMLCanvasElement | null>(null)
const tickLabels = ref<HTMLSpanElement[]>([])
const width = ref(700)
const height = ref(500)
const screenScale = ref({ x: 1, y: 1 })
const referencePreview = ref<{
  name: string
  referencePosition: number
} | null>(null)
const referenceGestureName = ref('')
const markerGestureActive = ref(false)
const plotGestureActive = ref(false)
const panPreview = ref<{ name: string; scale: DisplayScale } | null>(null)
const graphicalViewport = ref<GraphicalZoomViewport | null>(null)
const graphicalPanPreview = ref<GraphicalZoomViewport | null>(null)
let observer: ResizeObserver | undefined
let resolutionQuery: MediaQueryList | undefined
let paintRequest = 0
let dragPointer: number | null = null
let draggingName = ''
let draggingMarkerId = 1
let draggingDisplayRevision = ''
let dragTarget: HTMLElement | null = null
let dragCommandAt: ((point: DragPoint) => MarkerMove | null) | null = null
let markerVisual: {
  project: (point: DragPoint) => MarkerDragPoint
  bounds: { left: number; top: number; width: number; height: number }
  offset: DragPoint
  origin: DragPoint
  scale: DragPoint
  detached: boolean
  lastProjected: MarkerDragPoint | null
} | null = null
const detachedMarker = ref<{
  name: string
  markerId: number
  color: string
  clientX: number
  clientY: number
  scaleX: number
  scaleY: number
  trashBounds: { left: number; top: number; width: number; height: number } | null
} | null>(null)
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
    if (detachedMarker.value) {
      dragPreview.value = null
      return
    }
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
    const channelId = measurementChannel(measurement)
    const channel = props.channels?.find((item) => item.channelId === channelId)
    const source = frameForMeasurement(measurement, props.channelFrames, props.frame)
    const frame = source?.channelId === channelId ? source : null
    const sweepProgress = currentSweepProgress(props.channelFrames?.[channelId], channel,
      props.connected !== false, props.revision, props.displayRevision)
    const sweepSegments = sweepTraceSegments(frame, sweepProgress, measurement.name)
    const config = channel?.config ?? props.config
    const axis = sweepProgress?.frame.frequencyHz ?? frame?.frequencyHz
    const range = axis?.length
      ? { min: axis[0]!, max: axis[axis.length - 1]! }
      : { min: config.startHz, max: config.stopHz }
    const frameTrace = frame?.traces.find(
      (item) => item.name === measurement.name,
    )
    return [
      {
        measurement: sweepProgress?.frame.traces.find((trace) => trace.name === measurement.name) ?? frameTrace ?? measurement,
        presentation,
        frameTrace,
        frame,
        sweepProgress,
        sweepSegments,
        channelId,
        channel,
        config,
        range,
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
const hasSweepProgress = computed(() => traces.value.some((trace) =>
  trace.presentation.visible && trace.sweepProgress))
type DiagramTrace = (typeof traces.value)[number]
function traceCondition(trace: DiagramTrace): FrameCondition {
  const { channel, frame } = trace
  if (!channel) return props.frameCondition
  if (props.connected === false)
    return { kind: 'disconnected', label: 'DISCONNECTED', detail: 'Retained frame · connection unavailable' }
  if (channel.state === 'Faulted') return { kind: 'fault', label: 'FAULT', detail: channel.error }
  if (!frame) return { kind: 'empty', label: 'NO DATA', detail: 'Waiting for this channel data' }
  if (!channel.activeConfigValid || frame.revision !== channel.acquisitionRevision)
    return { kind: 'stale', label: 'PREVIOUS CONFIG', detail: 'Waiting for this channel configuration' }
  if ((props.displayRevision !== undefined && frame.displayRevision !== props.displayRevision) ||
    (props.revision !== undefined && frame.measurementRevision !== props.revision) ||
    (channel.calibration && frame.calibrationRevision !== channel.calibration.revision))
    return { kind: 'stale', label: 'PREVIOUS DISPLAY', detail: 'Waiting for this channel projection' }
  if (frame.runId !== channel.runId)
    return { kind: 'acquiring', label: 'PREVIOUS RUN', detail: 'Waiting for this channel run' }
  return frame.current ? { kind: 'current', label: 'CURRENT FRAME', detail: '' }
    : { kind: 'stale', label: 'RETAINED FRAME', detail: 'Waiting for current channel data' }
}
/** 冻结的是采集身份和轴几何；下一帧的序号或样本值不会中断手势。 */
function acquisitionIdentity(trace: DiagramTrace): unknown[] {
  return [trace.channelId, trace.frame?.revision, trace.frame?.measurementRevision,
    trace.frame?.calibrationRevision, trace.frame?.runId, trace.frame?.originalPointCount,
    trace.range.min, trace.range.max, trace.config.startHz, trace.config.stopHz,
    trace.config.points, trace.config.ifBandwidthHz]
}
const gestureAcquisitionKey = computed(() => JSON.stringify(traces.value.map(acquisitionIdentity)))
// 预览只替换一条迹线的坐标映射；原始采样、格式投影及后端快照均保持不变。
const referenceScalePreview = computed(() => {
  const preview = referencePreview.value
  if (!preview) return null
  const trace = traces.value.find(
    (item) => item.measurement.name === preview.name,
  )
  if (!trace || !trace.presentation.visible) return null
  const scale = previewReferenceScale(
    trace.scale,
    trace.presentation.format,
    preview.referencePosition,
  )
  return scale ? { name: trace.measurement.name, scale } : null
})
function savedScale(name: string, scale: DisplayScale): DisplayScale {
  if (panPreview.value?.name === name) return panPreview.value.scale
  const preview = referenceScalePreview.value
  return preview?.name === name ? preview.scale : scale
}
function renderScale(name: string, scale: DisplayScale): DisplayScale {
  const saved = savedScale(name, scale)
  const viewport = graphicalPanPreview.value ?? graphicalViewport.value
  const format = props.presentations[name]?.format
  return viewport && format ? graphicalTraceScale(saved, format, viewport) ?? saved : saved
}
const referenceHandles = computed(() =>
  graphicalViewport.value ? [] : traces.value.filter(
    (item) => item.presentation.visible && !isCircularFormat(item.presentation.format),
  ),
)
function setReferenceGesture(name: string, active: boolean): void {
  if (active && !referenceGestureName.value && !referencePreview.value)
    referenceGestureName.value = name
  else if (!active && referenceGestureName.value === name)
    referenceGestureName.value = ''
}
function setReferencePreview(
  name: string,
  preview: { name: string; referencePosition: number } | null,
): void {
  if (preview && preview.name === name && referenceGestureName.value === name)
    referencePreview.value = preview
  else if (!preview && referencePreview.value?.name === name)
    referencePreview.value = null
}
const active = computed(() =>
  props.selectedDiagram === undefined
    ? props.measurements.some((item) => item.name === props.activeTraceName)
    : props.number === props.selectedDiagram,
)
const axisScale = computed(
  () =>
    activeTrace.value
      ? renderScale(activeTrace.value.measurement.name, activeTrace.value.scale)
      : FORMAT_INFO.db.fallback,
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
const acquisitionRange = computed(() => {
  return activeTrace.value?.range ?? { min: props.config.startHz, max: props.config.stopHz }
})
function displayRange(range: { min: number; max: number }): { min: number; max: number } {
  const viewport = graphicalPanPreview.value ?? graphicalViewport.value
  return viewport ? graphicalFrequencyRange(range, viewport) ?? range : range
}
const xRange = computed(() => graphicalViewport.value
  ? graphicalFrequencyRange(acquisitionRange.value, graphicalViewport.value) ?? acquisitionRange.value
  : acquisitionRange.value)
const channelRows = computed(() => {
  const seen = new Set<number>()
  return traces.value.flatMap((trace) => {
    if (seen.has(trace.channelId)) return []
    seen.add(trace.channelId)
    // 优先恢复该通道在图中的主迹线，其余通道使用图中第一条所属迹线。
    const selected = activeTrace.value?.channelId === trace.channelId ? activeTrace.value : trace
    return [{ channelId: trace.channelId, name: trace.channel?.name ?? `Ch${trace.channelId}`,
      traceName: selected.measurement.name, frame: trace.frame, config: trace.config,
      range: trace.range, zoomRange: displayRange(trace.range), condition: traceCondition(trace),
      active: props.activeChannelId === undefined ? active.value : props.activeChannelId === trace.channelId }]
  })
})
const graphicalReady = computed(() => Boolean(
  active.value && activeTrace.value?.frame && props.frameCondition.kind === 'current' &&
  activeTrace.value?.presentation.visible && !circularAxis.value &&
  acquisitionRange.value.max > acquisitionRange.value.min,
))
const interactionScale = computed(() => {
  const trace = activeTrace.value
  if (!trace) return FORMAT_INFO.db.fallback
  // 手势输入使用已确认的本地视口，不能把每帧预览反传给手势层并取消其自身。
  return graphicalViewport.value
    ? graphicalTraceScale(trace.scale, trace.presentation.format, graphicalViewport.value) ?? trace.scale
    : trace.scale
})
function clearGraphicalZoom(): void {
  graphicalPanPreview.value = null
  graphicalViewport.value = null
  if (props.graphicalZoomSelecting) emit('cancelGraphicalZoom')
}
async function commitPlotZoom(value: StimulusZoomSelection): Promise<boolean> {
  if (!props.graphicalZoomSelecting)
    return !graphicalViewport.value && props.commitStimulusZoom ? props.commitStimulusZoom(value) : false
  const trace = activeTrace.value
  if (!graphicalReady.value || !trace || value.traceName !== trace.measurement.name ||
    value.revision !== props.revision || value.displayRevision !== props.displayRevision) return false
  const range = xRange.value
  const scale = interactionScale.value
  const format = trace.presentation.format
  const minimum = scaleCoordinate(scale.min, format)
  const maximum = scaleCoordinate(scale.max, format)
  const span = maximum - minimum
  const left = Math.max(0, Math.min(1, (value.startHz - range.min) / (range.max - range.min)))
  const right = Math.max(0, Math.min(1, (value.stopHz - range.min) / (range.max - range.min)))
  const top = Math.max(0, Math.min(1, (maximum - scaleCoordinate(value.max, format)) / span))
  const bottom = Math.max(0, Math.min(1, (maximum - scaleCoordinate(value.min, format)) / span))
  const next = composeGraphicalZoom(graphicalViewport.value ?? FULL_GRAPHICAL_ZOOM,
    { left, top, width: right - left, height: bottom - top })
  if (!next || !graphicalTraceScale(trace.scale, format, next)) return false
  graphicalPanPreview.value = null
  graphicalViewport.value = isGraphicalZoomed(next) ? next : null
  emit('cancelGraphicalZoom')
  return true
}
async function commitPlotPan(value: TracePanMove): Promise<boolean> {
  const viewport = graphicalViewport.value
  if (!viewport) return props.canEditDisplay && props.commitTracePan ? props.commitTracePan(value) : false
  const trace = activeTrace.value
  if (!graphicalReady.value || !trace || value.name !== trace.measurement.name ||
    value.displayRevision !== props.displayRevision) return false
  const format = trace.presentation.format
  const scale = interactionScale.value
  const span = scaleCoordinate(scale.max, format) - scaleCoordinate(scale.min, format)
  const shift = scaleCoordinate(value.reference, format) - scaleCoordinate(referenceLevel(scale, format), format)
  const next = panGraphicalZoom(viewport, -shift / span)
  if (!next || !graphicalTraceScale(trace.scale, format, next)) return false
  graphicalViewport.value = next
  graphicalPanPreview.value = null
  return true
}
function setPlotPreview(scale: DisplayScale | null): void {
  const trace = activeTrace.value
  const viewport = graphicalViewport.value
  if (viewport && trace) {
    const format = trace.presentation.format
    const current = interactionScale.value
    const span = scaleCoordinate(current.max, format) - scaleCoordinate(current.min, format)
    graphicalPanPreview.value = scale
      ? panGraphicalZoom(viewport, -(scaleCoordinate(scale.max, format) - scaleCoordinate(current.max, format)) / span)
      : null
    return
  }
  panPreview.value = scale && trace ? { name: trace.measurement.name, scale } : null
}
const markerModels = computed(() =>
  traces.value.flatMap((item) => {
    if (!item.presentation.visible) return []
    if (item.sweepProgress && item.sweepSegments.length < 2) return []
    return traceMarkers(item.presentation).flatMap((marker) => {
      if (!marker.enabled) return []
      const result = markerResult(
        item.frame,
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
const infoVisible = computed(() => props.markerInfoVisible !== false)
const annotationFontScale = computed(() => props.annotations?.fontScale ?? 100)
const markerInfoFields = computed(() =>
  traces.value.flatMap((trace) => {
    if (
      (trace.presentation.markerInfoLocation ?? 'diagram') !== 'diagram' ||
      (props.annotations?.activeTraceOnly &&
        trace.measurement.name !== props.activeTraceName)
    )
      return []
    const markers = markerReadouts.value.filter(
      (marker) => marker.name === trace.measurement.name,
    )
    return markers.length
      ? [
          {
            name: trace.measurement.name,
            markers,
            area: trace.presentation.markerInfoArea ?? 'topRight',
          },
        ]
      : []
  }),
)
const markerInfoGroups = computed(() =>
  MARKER_INFO_AREAS.flatMap((area) => {
    const fields = markerInfoFields.value.filter((field) => field.area === area)
    return fields.length ? [{ area, fields }] : []
  }),
)

function markerInfoGroupStyle(area: MarkerInfoArea) {
  const column = MARKER_INFO_AREAS.indexOf(area) % 3
  const row = Math.floor(MARKER_INFO_AREAS.indexOf(area) / 3)
  return {
    left: column === 0 ? '2px' : column === 1 ? '50%' : undefined,
    right: column === 2 ? '2px' : undefined,
    top: row === 0 ? '2px' : row === 1 ? '50%' : undefined,
    bottom: row === 2 ? '2px' : undefined,
    transform: `translate(${column === 1 ? '-50%' : '0'}, ${row === 1 ? '-50%' : '0'})`,
  }
}
watch(dragPreview, (preview) =>
  emit(
    'markerPreview',
    preview?.result
      ? {
          name: preview.name,
          markerId: preview.markerId,
          result: preview.result,
        }
      : null,
  ),
)

/** DOM 实际字体测量只负责列几何；不会保存测量数据或应用状态。 */
function measureTitles(): void {
  const list = traceList.value
  if (!list || !titleMeasure) return
  const titles = Array.from(list.querySelectorAll<HTMLElement>('.trace-select'))
  const fields = titles.map((title) =>
    Array.from(title.children).map((child) => {
      const style = window.getComputedStyle(child)
      return {
        kind: child.className,
        text: (child.textContent ?? '').replace(/\s+/g, ' ').trim(),
        font: style.font,
        paddingLeft: parseFloat(style.paddingLeft || '0'),
        paddingRight: parseFloat(style.paddingRight || '0'),
      }
    }),
  )
  const geometry = measuredTraceTitleLayout(
    list.clientWidth,
    fields,
    (text, font) => {
      titleMeasure!.font = font
      return titleMeasure!.measureText(text).width
    },
  )
  titleLayout.value = geometry
}
function traceDropAt(event: DragEvent): TraceDropPosition | null {
  const target = event.currentTarget as HTMLElement | null
  const bounds = target?.getBoundingClientRect?.()
  if (!bounds) return 'center'
  return traceDropPosition(
    event.clientX - bounds.left,
    event.clientY - bounds.top,
    bounds.width,
    bounds.height,
    props.canAddDiagram !== false,
  )
}

function dropOnDiagram(event: DragEvent): void {
  traceDrop.value = null
  if (!props.canEditDisplay) return
  const intent = traceDragIntent(event)
  if (intent) {
    const position = traceDropAt(event)
    if (!position || (position !== 'center' && props.canAddDiagram === false))
      return
    if (intent.kind === 'new' && props.canAddTrace !== false) {
      event.preventDefault()
      emit('newTrace', {
        diagram: props.number,
        ...(position !== 'center' ? { position } : {}),
      })
    } else if (
      intent.kind === 'move' &&
      intent.displayRevision === props.displayRevision &&
      props.allMeasurements.some(
        (measurement) => measurement.name === intent.name,
      )
    ) {
      event.preventDefault()
      emit('moveTrace', {
        name: intent.name,
        diagram: props.number,
        position,
        displayRevision: intent.displayRevision,
      })
    }
    return
  }
  const name = markerInfoDragName(event)
  if (
    name &&
    props.measurements.some((measurement) => measurement.name === name)
  ) {
    const bounds = plotArea.value?.getBoundingClientRect()
    const area = bounds
      ? markerInfoAreaAt({ x: event.clientX, y: event.clientY }, bounds)
      : null
    if (bounds && !area) return
    event.preventDefault()
    emit('relocateMarkerInfo', {
      name,
      location: 'diagram',
      ...(area ? { area } : {}),
    })
  }
}
function dragOverDiagram(event: DragEvent): void {
  if (isNewTraceDrag(event) || isTraceDrag(event)) {
    const moving = isTraceDrag(event)
    if (props.canEditDisplay && (moving || props.canAddTrace !== false)) {
      event.preventDefault()
      event.dataTransfer!.dropEffect = moving ? 'move' : 'copy'
      movingTrace.value = moving
      traceDrop.value = traceDropAt(event)
    }
    return
  }
  if (
    props.canEditDisplay &&
    event.dataTransfer?.types.includes(MARKER_INFO_DRAG_TYPE)
  ) {
    event.preventDefault()
    event.dataTransfer.dropEffect = 'move'
  }
}
function leaveDiagram(event: DragEvent): void {
  const target = event.currentTarget as HTMLElement | null
  if (!event.relatedTarget || !target?.contains(event.relatedTarget as Node))
    traceDrop.value = null
}
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
      renderScale(marker.name, marker.scale),
      marker.presentation.format,
    )
    if (!Number.isFinite(y) || y < 0 || y > 1) return []
    const range = displayRange(marker.range)
    const x = normalizedX(marker.result.frequencyHz, range.min, range.max)
    if (!Number.isFinite(x) || x < 0 || x > 1) return []
    const point = {
      x:
        rect.value.left +
        x * rect.value.width,
      y: rect.value.top + y * rect.value.height,
    }
    return [{ ...marker, placement: markerPlacement(point, rect.value) }]
  }),
)
const hasDrawable = computed(() =>
  traces.value.some(
    (item) =>
      item.presentation.visible &&
      item.sweepSegments.some((segment) => (isCircularFormat(item.presentation.format)
        ? smithSeries(segment.trace).some((point) => point !== null)
        : displaySeries(segment.trace, item.presentation.format).some(
            (series) =>
              series.values.some((value) =>
                Number.isFinite(
                  scaleCoordinate(value, item.presentation.format),
                ),
              ),
          ))),
  ),
)
const noDrawableMessage = computed(() => {
  if (!props.measurements.length) return 'No measurement assigned'
  if (traces.value.every((item) => !item.presentation.visible))
    return 'All traces hidden'
  if (!traces.value.some((item) => item.frame)) return 'No acquired frame'
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
const axisType = computed(() =>
  axisTypography(width.value, height.value, annotationFontScale.value),
)
const axisLabelHeight = computed(() => axisType.value.lineHeight)
const yTicks = computed(() =>
  spacedAxisLabels(
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
        division: index,
        top: rect.value.top + (rect.value.height * index) / yTickDivisions,
        reference:
          Math.abs(tick - referenceLevel(axisScale.value, axisFormat.value)) <=
          span * 1e-9,
        value: `${relativeAxis.value ? 'Δ ' : ''}${relativeAxis.value && value > 0 ? '+' : ''}${axisFormat.value === 'logMagnitude' ? numberText(value, 4) : scaleSettingNumber(value, span / yTickDivisions)}${axisDefinition.value.shortUnit}`,
      }
    }),
    height.value,
    axisLabelHeight.value,
    yTickDivisions,
  ),
)

function circleRadius(presentation: TracePresentation): number {
  return circularAxis.value ? smithRadius.value : presentation.smithRadius
}

/** 背景来自可见呈现格式，等待第一帧时也能显示；仅派生像素几何，不保存控制状态。 */
const backgrounds = computed<MixedBackground[]>(() => {
  const visible = traces.value.filter((item) => item.presentation.visible)
  return visible.length
    ? visible.map((item) => ({
        format: item.presentation.format,
        radius: circleRadius(item.presentation),
      }))
    : [{ format: axisFormat.value, radius: smithRadius.value }]
})

/** 绘制与点击共用服务端投影；命中只决定用户点中了哪条可见曲线。 */
const projectedCurves = computed<SelectableCurve[]>(() =>
  traces.value.flatMap((item): SelectableCurve[] => item.sweepSegments.flatMap((segment): SelectableCurve[] => {
    if (!item.presentation.visible) return []
    const format = item.presentation.format
    const shared = {
      name: item.measurement.name,
      color: item.color,
      // 各图窗保留自己的局部选择；置顶和重叠命中不依赖全局活动图窗。
      active: item.measurement.name === props.primaryTraceName,
    }
    if (isCircularFormat(format)) {
      return [
        {
          ...shared,
          kind: 'circular',
          format: format as 'smith' | 'polar' | 'invSmith',
          radius: circleRadius(item.presentation),
          points: smithSeries(segment.trace),
        },
      ]
    }
    return displaySeries(segment.trace, format).map((series) => ({
      ...shared,
      kind: 'cartesian',
      format,
      frequencyHz: segment.frame.frequencyHz,
      range: item.range,
      values: series.values,
      scale: item.scale,
      dashed: series.component === 'imaginary',
    }))
  })),
)
const curves = computed<SelectableCurve[]>(() => {
  const preview = panPreview.value ?? referenceScalePreview.value
  return preview || graphicalViewport.value
    ? projectedCurves.value.map((curve) =>
        curve.kind === 'cartesian'
          ? { ...curve, scale: renderScale(curve.name, curve.scale), range: displayRange(curve.range!) }
          : curve,
      )
    : projectedCurves.value
})
const sweepCursors = computed(() => curves.value.flatMap((curve) => {
  const trace = traces.value.find((item) => item.measurement.name === curve.name)
  const progress = trace?.sweepProgress
  if (!progress) return []
  const point = sweepCursorPoint(curve, progress.completedPoints, {
    width: width.value, height: height.value, rect: rect.value,
    scaleX: screenScale.value.x, scaleY: screenScale.value.y,
  })
  return point ? [{ ...point, name: curve.name, color: curve.color, channelId: trace.channelId,
    completedPoints: progress.completedPoints, totalPoints: progress.frame.originalPointCount }] : []
}))

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
    // 批量读取字框，再绘制黑底；DOM 仅保留透明文字层，不遮挡采样曲线。
    const labelMasks = tickLabels.value.map((label) => {
      const box = label.getBoundingClientRect()
      return {
        left: (box.left - bounds.left) / screenScale.value.x,
        top: (box.top - bounds.top) / screenScale.value.y,
        width: box.width / screenScale.value.x,
        height: box.height / screenScale.value.y,
      }
    })
    const references = (graphicalViewport.value ? [] : traces.value)
      .filter(
        (item) =>
          item.presentation.visible &&
          !isCircularFormat(item.presentation.format),
      )
      .map((item) => ({
        scale: renderScale(item.measurement.name, item.scale),
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
        fontScale: annotationFontScale.value,
        backgrounds: backgrounds.value,
        references,
        curves: curves.value,
        labelMasks,
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
  titleMeasure = document.createElement('canvas').getContext('2d')
  titleObserver = new ResizeObserver(measureTitles)
  if (traceList.value) titleObserver.observe(traceList.value)
  void nextTick(measureTitles)
  void document.fonts?.ready.then(() => {
    measureTitles()
    schedulePaint()
  })
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
  titleObserver?.disconnect()
  emit('markerPreview', null)
  cancelDrag()
  observer?.disconnect()
  window.removeEventListener('resize', resizePlot)
  window.removeEventListener('keydown', cancelDragKey)
  resolutionQuery?.removeEventListener('change', resolutionChanged)
  cancelAnimationFrame(paintRequest)
})
watch(
  () =>
    JSON.stringify([
      props.primaryTraceName,
      smithRadius.value,
      annotationFontScale.value,
      traces.value.map((trace) => [trace.measurement.name, trace.frameTrace?.calibration]),
      props.measurements.map((measurement) => {
        const presentation = props.presentations[measurement.name]
        return [
          measurement.name,
          measurement.parameter,
          measurement.unit,
          measurement.calibration,
          presentation?.format,
          presentation?.scale,
          presentation?.smithRadius,
        ]
      }),
    ]),
  () => void nextTick(measureTitles),
)
// 完整帧不可变；只监听引用，不能让每个图窗递归遍历其他图窗的全部样本。
watch(() => traces.value.map((trace) => [trace.frame, trace.sweepProgress]), schedulePaint)
watch(referenceScalePreview, schedulePaint)
watch(panPreview, schedulePaint)
watch(graphicalPanPreview, schedulePaint)
watch(graphicalViewport, (viewport) => {
  emit('graphicalZoomChanged', { diagram: props.number, zoomed: isGraphicalZoomed(viewport) })
  schedulePaint()
}, { flush: 'sync' })
watch(() => props.graphicalZoomReset, (next, previous) => {
  if (next !== previous && active.value) clearGraphicalZoom()
}, { flush: 'sync' })
watch(() => JSON.stringify([
  props.number, props.layout, props.primaryTraceName,
  // 本地视口只随本图的几何与通道采集状态失效；其他图窗推进共享 CAS 不改变此视口。
  // 写入手势仍由 acquisitionIdentity 和各自的全局 CAS 检查取消，不复用本地视口规则。
  traces.value.map((trace) => [trace.measurement.name, trace.presentation.format,
    trace.presentation.visible, trace.scale.min, trace.scale.max, trace.scale.referencePosition,
    trace.presentation.smithRadius, trace.channelId, trace.channel?.acquisitionRevision,
    trace.channel?.calibration?.revision, trace.frame?.revision, trace.frame?.calibrationRevision,
    trace.frame?.originalPointCount, trace.range.min, trace.range.max,
    trace.config.startHz, trace.config.stopHz, trace.config.points,
    trace.config.ifBandwidthHz, trace.config.powerDbm]),
]), clearGraphicalZoom, { flush: 'sync' })
watch(graphicalReady, (ready) => {
  if (!ready && props.graphicalZoomSelecting) emit('cancelGraphicalZoom')
})
onScopeDispose(() => {
  if (props.graphicalZoomSelecting) emit('cancelGraphicalZoom')
  emit('graphicalZoomChanged', { diagram: props.number, zoomed: false })
})
watch(
  [
    () => JSON.stringify(traces.value.map((trace) => [trace.measurement.name, trace.presentation, trace.color])),
    () => props.activeTraceName,
    () => props.primaryTraceName,
    xRange,
    annotationFontScale,
  ],
  schedulePaint,
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
      props.displayRevision,
      props.revision,
      xRange.value,
      traces.value.map((trace) => [
        trace.measurement.name,
        acquisitionIdentity(trace),
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
  const restoreTrace =
    props.primaryTraceName !== '' &&
    props.activeTraceName !== props.primaryTraceName
  if (props.canEditDisplay && (!active.value || restoreTrace))
    emit('selectDiagram', props.number)
}

function beginDrag(
  event: PointerEvent,
  name: string,
  markerId = activeMarkerId(props.presentations[name]),
): void {
  if (
    event.button !== 0 ||
    event.isPrimary === false ||
    !props.canEditDisplay ||
    plotGestureActive.value ||
    props.zoomSelecting ||
    props.graphicalZoomSelecting ||
    referenceGestureName.value !== '' ||
    referencePreview.value !== null ||
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
    range: displayRange(marker.range),
    offset: { x: offsetX, y: offsetY },
  })
  markerVisual = {
    project: geometry.project,
    bounds: { left: bounds.left, top: bounds.top, width: bounds.width, height: bounds.height },
    offset: { x: offsetX, y: offsetY },
    origin: { x: event.clientX, y: event.clientY },
    scale: { x: bounds.width / width.value, y: bounds.height / height.value },
    detached: false,
    lastProjected: null,
  }
  const started = markerDrag.begin(
    event.pointerId,
    { x: event.clientX, y: event.clientY },
    projectMarkerVisual,
  )
  if (!started) { markerVisual = null; return }
  dragCommandAt = geometry.command
  event.preventDefault()
  event.stopPropagation()
  dragPointer = event.pointerId
  markerGestureActive.value = true
  draggingName = name
  draggingMarkerId = markerId
  draggingDisplayRevision = props.displayRevision ?? ''
  previewName = name
  previewMarkerId = markerId
  previewColor = marker.color
  suppressMarkerClick = false
  dragTarget = event.currentTarget as HTMLElement
  dragTarget.setPointerCapture(event.pointerId)
}

function projectMarkerVisual(point: DragPoint): MarkerDragPoint {
  const visual = markerVisual
  if (!visual) return point
  const projected = visual.detached && visual.lastProjected ? visual.lastProjected : visual.project(point)
  visual.lastProjected = projected
  const clientX = point.x - visual.offset.x
  const clientY = point.y - visual.offset.y
  const curveY = visual.bounds.top + projected.y * visual.scale.y
  // 12 屏幕 CSS 像素是本项目的脱离阈值；抓取文字或箭头边缘不会提前触发。
  const moved = Math.hypot(point.x - visual.origin.x, point.y - visual.origin.y) >= 3
  const verticalIntent = Math.abs(point.y - visual.origin.y) >= 12
  // 精确 Marker 读数与最近采样点可不重合；单击或纯水平移频不能因此脱离。
  visual.detached ||= moved && verticalIntent && Math.abs(clientY - curveY) >= 12
  if (visual.detached) {
    const overTrash = props.canEditDisplay && Boolean(props.commitTrash) && hitTrashTarget(point.x, point.y)
    const target = overTrash ? document.querySelector<HTMLElement>('[data-testid="trash-target"]') : null
    const bounds = target?.getBoundingClientRect()
    detachedMarker.value = {
      name: previewName, markerId: previewMarkerId, color: previewColor, clientX, clientY,
      scaleX: visual.scale.x, scaleY: visual.scale.y,
      trashBounds: bounds ? { left: bounds.left, top: bounds.top, width: bounds.width, height: bounds.height } : null,
    }
  }
  return projected
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
  const displayRevision = draggingDisplayRevision
  if (!props.canEditDisplay || (props.displayRevision ?? '') !== displayRevision) {
    cancelDrag()
    return
  }
  // 释放也处理最后一次未赶上 rAF 的坐标；脱离后只接受有效垃圾箱落点。
  projectMarkerVisual({ x: event.clientX, y: event.clientY })
  if (detachedMarker.value && props.commitTrash && /^[0-9]+$/.test(displayRevision) &&
      hitTrashTarget(event.clientX, event.clientY)) {
    markerDrag.cancel()
    releaseDragCapture()
    void props.commitTrash({ kind: 'marker', name, markerId, displayRevision }).catch(() => {})
    return
  }
  if (detachedMarker.value) {
    cancelDrag()
    return
  }
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
  markerGestureActive.value = false
  dragTarget = null
  dragCommandAt = null
  markerVisual = null
  detachedMarker.value = null
  draggingName = ''
  draggingDisplayRevision = ''
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
  const points = createMarkerPreviewPoints(
    trace.frame,
    trace.frameTrace,
    trace.presentation.format,
    {
      area: rect.value,
      range: displayRange(trace.range),
      scale: renderScale(name, trace.scale),
      smithArea: smithArea.value,
      smithRadius: circleRadius(trace.presentation),
    },
  )
  return graphicalViewport.value && !isCircularFormat(trace.presentation.format)
    ? points.filter((point) => point.x >= rect.value.left && point.x <= rect.value.left + rect.value.width &&
      point.y >= rect.value.top && point.y <= rect.value.top + rect.value.height)
    : points
}

function markerClick(name: string, markerId: number): void {
  if (suppressMarkerClick) {
    suppressMarkerClick = false
    return
  }
  if (props.canEditDisplay) emit('selectMarker', { name, markerId })
}

function markerKey(event: KeyboardEvent, name: string, markerId: number): void {
  const frame = traces.value.find((trace) => trace.measurement.name === name)?.frame
  if (!props.canEditDisplay || !frame) return
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
      command = { sampleIndex: frame.originalPointCount - 1 }
      break
    default:
      return
  }
  event.preventDefault()
  event.stopPropagation()
  emit('moveMarker', { name, markerId, ...command, discrete: true })
}

function clickPlot(event: MouseEvent): void {
  if (
    event.button !== 0 ||
    !props.canEditDisplay ||
    props.zoomSelecting ||
    props.graphicalZoomSelecting ||
    markerDrag.dragging ||
    markerDrag.pending
  )
    return
  const target = event.target as Element | null
  if (
    target?.closest?.('.marker-readouts, .marker-handle, .reference-position-handle')
  )
    return
  const bounds = plotArea.value?.getBoundingClientRect()
  const name = bounds
    ? hitTestTrace(
        {
          width: width.value,
          height: height.value,
          rect: rect.value,
          xMin: xRange.value.min,
          xMax: xRange.value.max,
          curves: curves.value,
          bounds,
        },
        { x: event.clientX, y: event.clientY },
      )
    : null
  if (name) emit('selectTrace', name)
  else selectPane()
}

function plotKey(event: KeyboardEvent): void {
  // 子级标记控件自行处理按键；绘图区获得焦点时操作当前迹线的活动标记。
  if (event.target !== event.currentTarget || props.zoomSelecting || props.graphicalZoomSelecting) return
  const trace = activeTrace.value
  if (!trace?.presentation.marker.enabled || !trace.frame) return
  const result = markerResult(
    trace.frame,
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
    :style="{
      '--diagram-label-font-size': `${(11 * annotationFontScale) / 100}px`,
      '--diagram-label-line-height': `${(18 * annotationFontScale) / 100}px`,
      '--diagram-axis-line-height': `${axisLabelHeight}px`,
      '--diagram-axis-font-size': `${axisType.fontSize}px`,
    }"
    @dragover="dragOverDiagram"
    @drop="dropOnDiagram"
    @dragleave="leaveDiagram"
    @dragend="traceDrop = null"
  >
    <TraceDropZones
      v-if="traceDrop"
      :position="traceDrop"
      :moving="movingTrace"
      :allow-split="canAddDiagram !== false"
    />
    <header class="trace-header">
      <div
        ref="traceList"
        class="trace-list"
        aria-label="Traces"
        :style="{
          gridTemplateColumns:
            titleLayout.slotWidth > 0
              ? `repeat(${titleLayout.columns}, max-content)`
              : 'minmax(0, 1fr)',
          '--trace-name-width': `${titleLayout.nameWidth}px`,
          '--trace-chip-width': `${titleLayout.chipWidth}px`,
          '--trace-format-width': `${titleLayout.formatWidth}px`,
        }"
      >
        <TraceStrip
          v-for="trace in traces"
          :key="trace.measurement.name"
          :measurement="trace.measurement"
          :presentation="trace.presentation"
          :scale="savedScale(trace.measurement.name, trace.scale)"
          :color="trace.color"
          :smith-radius="circleRadius(trace.presentation)"
          :can-edit-display="canEditDisplay"
          :display-revision="displayRevision"
          :revision="revision"
          :active="active && trace.measurement.name === primaryTraceName"
          :remembered="trace.measurement.name === primaryTraceName"
          :sections="titleLayout.sections"
          @select="emit('selectTrace', $event)"
          @toggle-visibility="emit('toggleVisibility', $event)"
        />
        <span v-if="!traces.length" class="empty-header"
          >Diagram {{ number }}</span
        >
      </div>
      <DiagramMenu
        :number="number"
        :revision="revision"
        :display-revision="displayRevision"
        :active="active"
        :layout="layout"
        :has-trace="Boolean(activeTrace)"
        :can-edit-display="canEditDisplay"
        :can-add-trace="canAddTrace"
        :can-add-diagram="canAddDiagram"
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
      :aria-label="`Diagram ${number} plot. Click a trace to select it, or the background to activate this diagram. Click a marker to select it; drag the marker or use Left, Right, Home and End to move it.`"
      @click="clickPlot"
      @keydown="plotKey"
    >
      <canvas ref="canvas" class="diagram-canvas" aria-hidden="true" />
      <svg
        v-for="(cursor, index) in sweepCursors"
        :key="`${cursor.name}-${index}`"
        class="sweep-cursor"
        data-sweep-cursor
        :data-sweep-channel="cursor.channelId"
        :data-completed-points="cursor.completedPoints"
        :data-total-points="cursor.totalPoints"
        :style="{ left: `${cursor.x - 5}px`, top: `${cursor.y - 9}px`, color: cursor.color }"
        viewBox="0 0 10 9"
        aria-hidden="true"
      ><title>{{ cursor.name }} · Sweep {{ cursor.completedPoints }} / {{ cursor.totalPoints }} · Marker readings use the last complete sweep</title><path d="M0 0H10L5 9Z" /></svg>
      <PlotInteractionLayer
        v-if="activeTrace && activeTrace.presentation.visible && !isCircularFormat(activeTrace.presentation.format)"
        :key="`${graphicalZoomSelecting || graphicalViewport ? 'graphical' : 'shared'}-${gestureAcquisitionKey}`"
        :name="activeTrace.measurement.name"
        :format="activeTrace.presentation.format"
        :scale="interactionScale"
        :revision="revision ?? ''"
        :display-revision="displayRevision ?? ''"
        :range="xRange"
        :rect="rect"
        :size="{ width, height }"
        :can-pan="Boolean(active && (graphicalViewport ? graphicalReady : canEditDisplay && commitTracePan) && !markerGestureActive && !dragPreview && !referenceGestureName && !referencePreview)"
        :zoom-selecting="Boolean(graphicalZoomSelecting || zoomSelecting)"
        :can-zoom="Boolean(active && (graphicalZoomSelecting ? graphicalReady : canZoom && !graphicalViewport) && !markerGestureActive && !referenceGestureName)"
        :can-continue-gesture="graphicalZoomSelecting || graphicalViewport ? graphicalReady : Boolean(canContinueReferenceDrag)"
        :commit-pan="commitPlotPan"
        :commit-zoom="commitPlotZoom"
        @preview="setPlotPreview"
        @active="plotGestureActive = $event"
        @cancel-zoom="graphicalZoomSelecting ? emit('cancelGraphicalZoom') : emit('cancelZoom')"
      />
      <ReferencePositionHandle
        v-for="trace in referenceHandles"
        :key="`${trace.measurement.name}-${gestureAcquisitionKey}`"
        :name="trace.measurement.name"
        :format="trace.presentation.format"
        :scale="trace.scale"
        :color="trace.color"
        :active="trace.measurement.name === primaryTraceName"
        :can-edit-display="
          canEditDisplay && !markerGestureActive && !dragPreview && !plotGestureActive && !zoomSelecting && !graphicalZoomSelecting &&
          (!referenceGestureName || referenceGestureName === trace.measurement.name) &&
          (!referencePreview || referencePreview.name === trace.measurement.name)
        "
        :can-continue-gesture="canContinueReferenceDrag"
        :display-revision="displayRevision ?? ''"
        :rect="rect"
        :plot-size="{ width, height }"
        :commit="commitReferencePosition"
        @preview="setReferencePreview(trace.measurement.name, $event)"
        @active="setReferenceGesture(trace.measurement.name, $event)"
        @select="emit('selectTrace', $event)"
      />
      <span
        v-for="tick in circularAxis ? [] : yTicks"
        ref="tickLabels"
        :key="`y-${tick.division}`"
        class="y-tick"
        :class="{
          'edge-top': tick.division === 0,
          'edge-bottom': tick.division === yTickDivisions,
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
      <div
        v-for="group in infoVisible ? markerInfoGroups : []"
        :key="group.area"
        class="marker-info-fields"
        :data-marker-info-area="group.area"
        :style="markerInfoGroupStyle(group.area)"
      >
        <MarkerReadout
          v-for="field in group.fields"
          :key="field.name"
          inline
          :show-trace-names="markerInfoFields.length > 1"
          location="diagram"
          :info-table-visible="infoTableVisible"
          :can-edit-display="canEditDisplay"
          :display-revision="displayRevision"
          :markers="field.markers"
          :font-scale="annotationFontScale"
          :stimulus-info-visible="annotations?.stimulusInfoVisible"
          @select="emit('selectMarker', $event)"
          @relocate="emit('relocateMarkerInfo', $event)"
        />
      </div>
      <button
        v-for="marker in plottedMarkers"
        :key="`${marker.name}-M${marker.markerId}`"
        type="button"
        class="marker-handle"
        :disabled="!canEditDisplay || Boolean(zoomSelecting || graphicalZoomSelecting)"
        :class="{
          active: marker.active,
          'drag-source':
            (dragPreview?.name === marker.name && dragPreview.markerId === marker.markerId) ||
            (detachedMarker?.name === marker.name && detachedMarker.markerId === marker.markerId),
        }"
        :style="{
          left: `${marker.placement.left}px`,
          top: `${marker.placement.top}px`,
          width: `${marker.placement.width}px`,
          height: `${marker.placement.height}px`,
          color: marker.color,
          pointerEvents: zoomSelecting || graphicalZoomSelecting ? 'none' : undefined,
          zIndex: marker.active ? 6 : marker.name === activeTraceName ? 5 : 4,
        }"
        :aria-label="`M${marker.markerId} ${marker.name}, ${frequencyText(marker.result.frequencyHz, 10)}, ${isCircularFormat(marker.presentation.format) ? circularReadout(marker.presentation.format, marker.result, marker.measurement.unit) : numberText(marker.result.value, 8) + ' ' + measurementFormatInfo(marker.presentation.format, marker.measurement).unit}. Arrow keys move between measured frequency points.`"
        title="沿曲线拖动改变频率；纵向拖离后拖入垃圾箱，仅关闭此 Marker"
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
            : 'Use Add Trace, or Trace Config → Diagram, to display a trace here.'
        }}</span>
      </div>
      <span
        v-if="hasSweepProgress || (frame && frameCondition.kind !== 'current')"
        class="frame-condition"
        :class="hasSweepProgress ? 'acquiring' : frameCondition.kind"
        :style="{ left: `${rect.left + 5}px`, bottom: '5px' }"
        :title="hasSweepProgress ? 'Live sweep prefix; marker readings and exports use the last complete sweep.' : frameCondition.detail"
        >{{ hasSweepProgress ? 'SWEEP' : frameCondition.label }}</span
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
        ·
        {{
          activeTrace ? measurementState(activeTrace.measurement) : 'Cal Off'
        }}</span
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

    <footer class="channel-footer" aria-label="Channel configuration">
      <button
      v-for="channel in channelRows"
      :key="channel.channelId"
      type="button"
      class="channel-row"
      :data-channel-id="channel.channelId"
      :disabled="!canEditDisplay"
      :class="{
        compact: width < (180 * annotationFontScale) / 100,
        minimal: width < (100 * annotationFontScale) / 100,
      }"
      :aria-label="`${channel.name} configuration. Select ${channel.traceName}.`"
      :title="`${channel.name} · Start ${frequencyText(channel.range.min)} · Stop ${frequencyText(channel.range.max)}${channel.condition.detail ? ' · ' + channel.condition.detail : ''}`"
      @click="emit('selectTrace', channel.traceName)"
    >
      <span class="channel-label" :class="{ active: channel.active }">{{ channel.name }}</span>
      <span class="channel-start"
        >Start <b>{{ frequencyText(channel.range.min) }}</b></span
      >
      <span class="channel-middle"
        >{{ channel.frame?.originalPointCount ?? channel.config.points }} pts
        <span
          :title="
            channel.frame && channel.condition.kind !== 'current'
              ? 'The retained frame does not carry its acquisition IF bandwidth.'
              : 'Committed acquisition IF bandwidth'
          "
        >
          Bw
          <b>{{
            channel.frame && channel.condition.kind !== 'current'
              ? '—'
              : frequencyText(channel.config.ifBandwidthHz)
          }}</b>
        </span>
        <span v-if="channel.config.powerDbm !== undefined" class="channel-power"
          :title="channel.frame && channel.condition.kind !== 'current'
            ? 'The retained frame does not carry its acquisition power.'
            : 'Committed source power'">
          Pwr <b>{{ channel.frame && channel.condition.kind !== 'current'
            ? '—' : `${numberText(channel.config.powerDbm, 6)} dBm` }}</b>
        </span>
      </span>
      <span class="channel-stop"
        >Stop <b>{{ frequencyText(channel.range.max) }}</b></span
      >
      <span v-if="graphicalViewport" class="graphical-zoom-line" data-testid="graphical-zoom-line">
        <span>Zoom</span><span>Start <b>{{ frequencyText(channel.zoomRange.min) }}</b></span><span>Stop <b>{{ frequencyText(channel.zoomRange.max) }}</b></span>
      </span>
      </button>
    </footer>
    <Teleport to="body">
      <div
        v-if="detachedMarker?.trashBounds"
        class="marker-trash-highlight"
        data-testid="marker-trash-highlight"
        aria-hidden="true"
        :style="{
          left: `${detachedMarker.trashBounds.left}px`, top: `${detachedMarker.trashBounds.top}px`,
          width: `${detachedMarker.trashBounds.width}px`, height: `${detachedMarker.trashBounds.height}px`,
        }"
      />
      <div
        v-if="detachedMarker"
        class="detached-marker-preview"
        data-marker-drag="detached"
        aria-hidden="true"
        :style="{
          left: `${detachedMarker.clientX}px`, top: `${detachedMarker.clientY}px`,
          width: `${32 * detachedMarker.scaleX}px`, height: `${32 * detachedMarker.scaleY}px`,
          color: detachedMarker.color,
          fontSize: `${11 * detachedMarker.scaleY}px`, lineHeight: `${14 * detachedMarker.scaleY}px`,
        }"
      >
        <span>M{{ detachedMarker.markerId }}</span>
        <svg viewBox="0 0 10 9" :style="{ width: `${10 * detachedMarker.scaleX}px`, height: `${9 * detachedMarker.scaleY}px` }">
          <path d="M0 0H10L5 9Z" />
        </svg>
      </div>
    </Teleport>
  </article>
</template>

<style scoped>
.diagram-pane {
  position: relative;
  display: grid;
  grid-template-columns: minmax(0, 1fr);
  grid-template-rows: auto minmax(0, 1fr) auto;
  min-width: 0;
  min-height: 0;
  border: 1px solid #526773;
  background: #000;
  color: #eff4f6;
  font-family: Tahoma, Arial, sans-serif;
  container-type: inline-size;
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
  flex: 1;
  min-width: 0;
  align-content: flex-start;
  gap: 2px;
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
.marker-info-fields {
  position: absolute;
  z-index: 3;
  display: flex;
  flex-direction: column;
  align-items: flex-end;
  gap: 3px;
  width: max-content;
  min-width: 0;
  max-width: calc(100% - 4px);
}
.marker-info-fields[data-marker-info-area^='bottom'] :deep(.marker-info-menu) {
  top: auto;
  bottom: 100%;
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
  padding: 0 1px;
  transform: translateY(-50%);
  text-align: left;
  color: #4b5f6c;
  background: transparent;
  font-size: var(--diagram-axis-font-size, 11px);
  line-height: var(--diagram-axis-line-height, 14px);
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
.sweep-cursor {
  position: absolute;
  z-index: 3;
  width: 10px;
  height: 9px;
  fill: currentColor;
  pointer-events: none;
}
.marker-handle span {
  position: absolute;
  left: 0;
  width: 100%;
  font-size: var(--diagram-label-font-size, 11px);
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
.detached-marker-preview {
  position: fixed;
  z-index: 10002;
  transform: translate(-50%, -100%);
  pointer-events: none;
  text-align: center;
  font-family: Tahoma, Arial, sans-serif;
}
.detached-marker-preview span { position: absolute; inset: 0 0 auto; }
.detached-marker-preview svg { position: absolute; bottom: 0; left: 50%; transform: translateX(-50%); fill: currentColor; }
.marker-trash-highlight {
  position: fixed;
  z-index: 10001;
  pointer-events: none;
  border-radius: 2px;
  outline: 2px solid #ffb186;
  outline-offset: -2px;
  background: #633e3170;
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
.channel-footer {
  min-width: 0;
}
.channel-row {
  display: grid;
  width: 100%;
  border: 0;
  border-radius: 0;
  text-align: left;
  font-family: inherit;
  grid-template-columns: auto 1fr auto 1fr;
  align-items: center;
  min-width: 0;
  min-height: 20px;
  gap: 4px;
  padding: 0 2px;
  color: #fff;
  background: #000;
  font-size: var(--diagram-label-font-size, 11px);
  line-height: var(--diagram-label-line-height, 18px);
  white-space: nowrap;
  overflow: hidden;
}
.channel-row:disabled {
  opacity: 1;
  cursor: default;
}
.channel-row:focus-visible {
  outline: 1px solid #91a4af;
  outline-offset: -1px;
}
.graphical-zoom-line {
  grid-column: 1 / -1;
  display: flex;
  justify-content: space-between;
  gap: 5px;
  min-width: 0;
  overflow: hidden;
}

/* 窄图窗保留起止频率，整项省略中部辅助信息，避免相邻图窗文字相互覆盖。 */
@container (max-width: 420px) {
  .channel-row {
    grid-template-columns: auto minmax(0, 1fr) minmax(0, 1fr);
  }
  .channel-row .channel-middle {
    display: none;
  }
}

.channel-label {
  padding: 0 3px;
  font-weight: 400;
  background: transparent;
}

.channel-label.active {
  color: #fff;
  background: var(--color-trace-active);
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
.channel-row.compact {
  grid-template-columns: auto 1fr;
}
.channel-row.compact .channel-start,
.channel-row.compact .channel-middle,
.channel-row.minimal .channel-stop {
  display: none;
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
