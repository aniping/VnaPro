<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import type { AcquiredFrame, FrameCondition, Measurement, SweepConfiguration } from '../../api/types'
import { defaultTracePresentation, diagramTraceColor, type LayoutMode, type TracePresentation } from '../../domain/presentation'
import { FORMAT_INFO, frequencyText, numberText } from '../../plot/formatting'
import { markerPlacement, paintDiagram, plotRect, type CanvasCurve } from '../../plot/canvasRenderer'
import { displaySeries, markerResult, nearestIndex, normalizedX, normalizedY, referenceLevel, traceScale } from '../../plot/traceMath'
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
  config: SweepConfiguration
  frameCondition: FrameCondition
}>()
const emit = defineEmits<{
  selectTrace: [name: string]
  moveMarker: [value: { name: string; frequencyHz: number }]
  toggleVisibility: [name: string]
  setLayout: [mode: LayoutMode]
  addMarker: []
}>()
const plotArea = ref<HTMLDivElement | null>(null)
const canvas = ref<HTMLCanvasElement | null>(null)
const width = ref(700)
const height = ref(500)
let observer: ResizeObserver | undefined
let resolutionQuery: MediaQueryList | undefined
let paintRequest = 0
let dragPointer: number | null = null
let draggingName = ''
let dragOffsetX = 0

const traces = computed(() => props.measurements.map(measurement => {
  const presentation = props.presentations[measurement.name] ?? defaultTracePresentation()
  const frameTrace = props.frame?.traces.find(item => item.name === measurement.name)
  const index = Math.max(0, props.allMeasurements.findIndex(item => item.name === measurement.name))
  return {
    measurement: frameTrace ?? measurement, presentation, frameTrace, index, color: diagramTraceColor(index, props.layout),
    scale: traceScale(frameTrace, presentation.format, presentation.scale),
  }
}))
const activeTrace = computed(() => traces.value.find(item => item.measurement.name === props.activeTraceName) ?? traces.value[0])
const active = computed(() => props.measurements.some(item => item.name === props.activeTraceName))
const axisScale = computed(() => activeTrace.value?.scale ?? FORMAT_INFO.db.fallback)
const axisFormat = computed(() => activeTrace.value?.presentation.format ?? 'db')
const relativeAxis = computed(() => {
  const span = axisScale.value.max - axisScale.value.min
  const reference = referenceLevel(axisScale.value)
  return activeTrace.value?.presentation.scale === null
    && (span < 0.001 || span < Math.abs(reference) * 0.0001)
})
const rect = computed(() => plotRect(width.value, height.value))
const xRange = computed(() => {
  const axis = props.frame?.frequencyHz
  return axis?.length
    ? { min: axis[0]!, max: axis[axis.length - 1]! }
    : { min: props.config.startHz, max: props.config.stopHz }
})
const markerModels = computed(() => traces.value.flatMap(item => {
  if (!item.presentation.visible || !item.presentation.marker.enabled) return []
  const result = markerResult(props.frame, item.frameTrace, item.presentation.marker.frequencyHz, item.presentation.format)
  if (!result) return []
  return [{ ...item, name: item.measurement.name, result, active: item.measurement.name === props.activeTraceName }]
}))
const plottedMarkers = computed(() => markerModels.value.flatMap(marker => {
  if (!marker.result.valid || !Number.isFinite(marker.result.value)) return []
  const y = normalizedY(marker.result.value, marker.scale)
  if (y < 0 || y > 1) return []
  const point = {
    x: rect.value.left + normalizedX(marker.result.frequencyHz, xRange.value.min, xRange.value.max) * rect.value.width,
    y: rect.value.top + y * rect.value.height,
  }
  return [{ ...marker,
    placement: markerPlacement(point, rect.value),
  }]
}))
const hasDrawable = computed(() => traces.value.some(item => item.presentation.visible
  && displaySeries(item.frameTrace, item.presentation.format).some(series => series.values.some(Number.isFinite))))
const noDrawableMessage = computed(() => {
  if (!props.measurements.length) return 'No receiver assigned'
  if (traces.value.every(item => !item.presentation.visible)) return 'All traces hidden'
  if (!props.frame) return 'No acquired frame'
  const zeroDb = traces.value.some(item => item.presentation.visible && item.presentation.format === 'db'
    && displaySeries(item.frameTrace, 'db')[0]?.values.some(value => value === -Infinity))
  return zeroDb ? 'Zero amplitude · dB = −∞' : 'No valid receiver points'
})
const yTickDivisions = 10
const yTicks = computed(() => Array.from({ length: yTickDivisions + 1 }, (_, index) => {
  const span = axisScale.value.max - axisScale.value.min
  const tick = axisScale.value.max - span * index / yTickDivisions
  let value = tick - (relativeAxis.value ? referenceLevel(axisScale.value) : 0)
  if (relativeAxis.value && Math.abs(value) < span * 1e-10) value = 0
  return {
    top: rect.value.top + rect.value.height * index / yTickDivisions,
    reference: Math.abs(tick - referenceLevel(axisScale.value)) <= span * 1e-9,
    value: `${relativeAxis.value ? 'Δ ' : ''}${relativeAxis.value && value > 0 ? '+' : ''}${numberText(value, 4)} ${FORMAT_INFO[axisFormat.value].shortUnit}`,
  }
}))

function schedulePaint(): void {
  cancelAnimationFrame(paintRequest)
  paintRequest = requestAnimationFrame(() => {
    if (!canvas.value || !plotArea.value || width.value <= 0 || height.value <= 0) return
    const curves: CanvasCurve[] = traces.value.flatMap(item => item.presentation.visible
      ? displaySeries(item.frameTrace, item.presentation.format).map(series => ({
        frequencyHz: props.frame?.frequencyHz ?? [], values: series.values, scale: item.scale,
        color: item.color, dashed: series.component === 'imaginary', active: item.measurement.name === props.activeTraceName,
      })) : [])
    const references = traces.value.filter(item => item.presentation.visible).map(item => ({
      scale: item.scale, color: item.color, active: item.measurement.name === activeTrace.value?.measurement.name,
    }))
    paintDiagram(canvas.value, { width: width.value, height: height.value, rect: rect.value,
      xMin: xRange.value.min, xMax: xRange.value.max, references, curves }, plotArea.value.getBoundingClientRect())
  })
}

function watchResolution(): void {
  resolutionQuery?.removeEventListener('change', resolutionChanged)
  resolutionQuery = window.matchMedia(`(resolution: ${window.devicePixelRatio}dppx)`)
  resolutionQuery.addEventListener('change', resolutionChanged)
}
function resolutionChanged(): void {
  watchResolution()
  schedulePaint()
}

onMounted(() => {
  observer = new ResizeObserver(entries => {
    const entry = entries[0]
    if (!entry) return
    width.value = entry.contentRect.width
    height.value = entry.contentRect.height
    schedulePaint()
  })
  if (plotArea.value) observer.observe(plotArea.value)
  window.addEventListener('resize', schedulePaint)
  watchResolution()
  schedulePaint()
})
onBeforeUnmount(() => {
  observer?.disconnect()
  window.removeEventListener('resize', schedulePaint)
  resolutionQuery?.removeEventListener('change', resolutionChanged)
  cancelAnimationFrame(paintRequest)
})
watch([() => props.frame, traces, () => props.activeTraceName, xRange], schedulePaint, { deep: true })

function selectPane(): void {
  if (!active.value && activeTrace.value) emit('selectTrace', activeTrace.value.measurement.name)
}

function moveMarkerAt(name: string, clientX: number): void {
  const bounds = plotArea.value?.getBoundingClientRect()
  const axis = props.frame?.frequencyHz
  if (!bounds || !axis?.length) return
  const logicalX = (clientX - bounds.left) * width.value / bounds.width
  const fraction = Math.max(0, Math.min(1, (logicalX - rect.value.left) / rect.value.width))
  const index = nearestIndex(axis, xRange.value.min + fraction * (xRange.value.max - xRange.value.min))
  if (index >= 0) emit('moveMarker', { name, frequencyHz: axis[index]! })
}

function beginDrag(event: PointerEvent, name: string): void {
  if (event.button !== 0) return
  event.preventDefault()
  event.stopPropagation()
  emit('selectTrace', name)
  dragPointer = event.pointerId
  draggingName = name
  const marker = plottedMarkers.value.find(item => item.name === name)
  const bounds = plotArea.value?.getBoundingClientRect()
  dragOffsetX = marker && bounds && width.value > 0
    ? event.clientX - (bounds.left + (marker.placement.left + marker.placement.tipX) * bounds.width / width.value)
    : 0
  const target = event.currentTarget as HTMLElement
  target.setPointerCapture(event.pointerId)
}

function dragMarker(event: PointerEvent): void {
  if (event.pointerId !== dragPointer) return
  event.preventDefault()
  moveMarkerAt(draggingName, event.clientX - dragOffsetX)
}

function endDrag(event: PointerEvent): void {
  if (event.pointerId !== dragPointer) return
  if (event.type !== 'pointercancel') moveMarkerAt(draggingName, event.clientX - dragOffsetX)
  const target = event.currentTarget as HTMLElement
  if (target.hasPointerCapture(event.pointerId)) target.releasePointerCapture(event.pointerId)
  dragPointer = null
  draggingName = ''
  dragOffsetX = 0
}

function markerKey(event: KeyboardEvent, name: string, currentIndex: number): void {
  const axis = props.frame?.frequencyHz
  if (!axis?.length) return
  let target: number
  switch (event.key) {
    case 'ArrowLeft': target = currentIndex - (event.shiftKey ? 10 : 1); break
    case 'ArrowRight': target = currentIndex + (event.shiftKey ? 10 : 1); break
    case 'Home': target = 0; break
    case 'End': target = axis.length - 1; break
    default: return
  }
  event.preventDefault()
  event.stopPropagation()
  emit('selectTrace', name)
  emit('moveMarker', { name, frequencyHz: axis[Math.max(0, Math.min(axis.length - 1, target))]! })
}

function placeMarker(event: MouseEvent): void {
  const item = activeTrace.value
  if (item?.presentation.marker.enabled) moveMarkerAt(item.measurement.name, event.clientX)
}

function clickPlot(event: MouseEvent): void {
  selectPane()
  placeMarker(event)
}

function plotKey(event: KeyboardEvent): void {
  // Child marker handles own their keys; the focused plot uses the active trace's M1.
  if (event.target !== event.currentTarget) return
  const trace = activeTrace.value
  if (!trace?.presentation.marker.enabled || !props.frame) return
  const result = markerResult(props.frame, trace.frameTrace, trace.presentation.marker.frequencyHz, trace.presentation.format)
  if (result) markerKey(event, trace.measurement.name, result.index)
}
</script>

<template>
  <article class="diagram-pane" :class="{ active }" :aria-label="`Diagram ${number}${active ? ', active' : ''}`"
    :data-diagram="number">
    <header class="trace-header">
      <div class="trace-list" aria-label="Traces">
        <TraceStrip v-for="trace in traces" :key="trace.measurement.name" :measurement="trace.measurement"
          :index="trace.index" :presentation="trace.presentation" :scale="trace.scale" :color="trace.color"
          :active="trace.measurement.name === activeTraceName" @select="emit('selectTrace', $event)"
          @toggle-visibility="emit('toggleVisibility', $event)" />
        <span v-if="!traces.length" class="empty-header">Diagram {{ number }}</span>
      </div>
      <DiagramMenu :number="number" :active="active" :layout="layout" :has-trace="Boolean(activeTrace)"
        @activate="selectPane" @layout="selectPane(); emit('setLayout', $event)"
        @add-marker="selectPane(); emit('addMarker')" />
    </header>

    <div ref="plotArea" class="plot-area" tabindex="0" role="group"
      :aria-label="`Diagram ${number} plot. When M1 is enabled, click a measured frequency or use Left, Right, Home and End to move the marker.`"
      @click="clickPlot" @keydown="plotKey">
      <canvas ref="canvas" class="diagram-canvas" aria-hidden="true" />
      <span v-for="(tick, index) in yTicks" :key="`y-${index}`" class="y-tick"
        :class="{ 'edge-top': index === 0, 'edge-bottom': index === 10, 'reference-tick': tick.reference }" :style="{ top: `${tick.top}px` }"
        :title="relativeAxis ? `Autoscale offset from Ref ${numberText(referenceLevel(axisScale), 10)} ${FORMAT_INFO[axisFormat].unit}` : FORMAT_INFO[axisFormat].unit">{{ tick.value }}</span>
      <MarkerReadout :markers="markerModels.map(marker => ({ name: marker.name, color: marker.color, format: marker.presentation.format, result: marker.result, active: marker.active }))"
        @select="emit('selectTrace', $event)" />
      <button v-for="marker in plottedMarkers" :key="marker.name" type="button" class="marker-handle"
        :class="{ active: marker.active }" :style="{ left: `${marker.placement.left}px`, top: `${marker.placement.top}px`, width: `${marker.placement.width}px`, height: `${marker.placement.height}px`, color: marker.color }"
        :aria-label="`M1 ${marker.name}, ${frequencyText(marker.result.frequencyHz, 10)}, ${numberText(marker.result.value, 8)} ${FORMAT_INFO[marker.presentation.format].unit}. Arrow keys move between measured frequency points.`"
        @pointerdown="beginDrag($event, marker.name)" @pointermove="dragMarker" @pointerup="endDrag" @pointercancel="endDrag"
        @keydown="markerKey($event, marker.name, marker.result.index)" @click.stop="emit('selectTrace', marker.name)">
        <span :style="{ top: `${marker.placement.labelTop}px` }">M1</span>
        <svg viewBox="0 0 10 9" aria-hidden="true" :style="{ left: `${marker.placement.glyphLeft}px`, top: `${marker.placement.glyphTop}px` }"><path :d="marker.placement.glyphPath" /></svg>
      </button>
      <div v-if="!hasDrawable" class="plot-empty" :style="{ left: `${rect.left}px`, top: `${rect.top}px`, width: `${rect.width}px`, height: `${rect.height}px` }">
        <strong>{{ noDrawableMessage }}</strong>
        <span>{{ measurements.length ? frameCondition.detail : 'Select a receiver in Meas to populate this diagram.' }}</span>
      </div>
      <span v-if="frame && frameCondition.kind !== 'current'" class="frame-condition" :class="frameCondition.kind"
        :style="{ left: `${rect.left + 5}px`, bottom: '5px' }" :title="frameCondition.detail">{{ frameCondition.label }}</span>
      <span v-if="frame?.decimated" class="sample-note" :title="`${frame.originalPointCount} source points; ${frame.displayPointCount} supplied display points. Marker and CSV use these actual display samples.`">
        Display {{ frame.displayPointCount }} / {{ frame.originalPointCount }} pts
      </span>
      <span class="sr-only">{{ activeTrace?.measurement.name ?? 'Empty diagram' }}. Format {{ FORMAT_INFO[axisFormat].label }}.
        {{ hasDrawable ? 'Real acquired receiver samples displayed.' : noDrawableMessage }} {{ frameCondition.detail }}</span>
    </div>

    <footer class="channel-row" aria-label="Channel configuration">
      <span class="channel-label" :class="{ active }">Ch1</span>
      <span class="channel-start">Start <b>{{ frequencyText(xRange.min) }}</b></span>
      <span class="channel-middle">{{ frame?.originalPointCount ?? config.points }} pts
        <span :title="frame && frameCondition.kind !== 'current' ? 'The retained frame does not carry its acquisition IF bandwidth.' : 'Committed acquisition IF bandwidth'">
          IF BW <b>{{ frame && frameCondition.kind !== 'current' ? '—' : frequencyText(config.ifBandwidthHz) }}</b>
        </span>
      </span>
      <span class="channel-stop">Stop <b>{{ frequencyText(xRange.max) }}</b></span>
    </footer>
  </article>
</template>

<style scoped>
.diagram-pane { display: grid; grid-template-rows: auto minmax(0, 1fr) 22px; min-width: 0; min-height: 0; border: 1px solid #526773; background: #000; color: #eff4f6; }
.diagram-pane.active { border-color: #91a4af; }
.trace-header { display: flex; min-height: 23px; gap: 2px; padding: 1px 2px; background: #000; }
.trace-list { display: flex; flex: 1; min-width: 0; flex-wrap: wrap; align-content: flex-start; gap: 2px 4px; }
.empty-header { align-self: center; padding-left: 5px; color: #a0b1bb; font-size: 12px; }
.plot-area { position: relative; min-height: 0; overflow: hidden; background: #000; }
.plot-area:focus-visible { outline: 2px solid #6fc5ea; outline-offset: -2px; }
.diagram-canvas { position: absolute; inset: 0; width: 100%; height: 100%; transform-origin: 0 0; }
.y-tick { position: absolute; z-index: 2; left: 4px; padding: 0; transform: translateY(-50%); text-align: left; color: #8ca0a8; background: transparent; font-size: 11px; line-height: 14px; font-variant-numeric: tabular-nums; white-space: nowrap; pointer-events: none; }
.y-tick.edge-top { top: 3px !important; transform: none; }.y-tick.edge-bottom { top: auto !important; bottom: 3px; transform: none; }
.y-tick.reference-tick { color: #dbe2e6; }
.marker-handle { position: absolute; z-index: 4; width: 32px; height: 32px; padding: 0; border: 0; background: transparent; cursor: ew-resize; touch-action: none; }
.marker-handle span { position: absolute; left: 0; width: 100%; font-size: 11px; font-weight: 600; line-height: 16px; color: inherit; text-align: center; text-shadow: 1px 1px #000; }
.marker-handle svg { position: absolute; width: 10px; height: 9px; fill: currentColor; stroke: none; }
.marker-handle:focus-visible { outline: 1px solid #fff; outline-offset: 2px; }
.plot-empty { position: absolute; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 8px; padding: 35px; box-sizing: border-box; pointer-events: none; text-align: center; color: #90a3af; background: transparent; }
.plot-empty strong { font-size: 16px; font-weight: 500; }.plot-empty span { max-width: 420px; font-size: 12px; line-height: 1.6; color: #728a97; }
.frame-condition { position: absolute; z-index: 2; padding: 1px 4px; color: #d2b887; background: #171915dc; border-left: 1px solid #bea66e; font-size: 11px; }
.frame-condition.disconnected, .frame-condition.fault { color: #ffb5ab; border-color: #ff7d6e; }
.sample-note { position: absolute; right: 5px; bottom: 5px; font-size: 10px; color: #a6b4bc; background: #000c; }
.channel-row { display: grid; grid-template-columns: auto 1fr auto 1fr; align-items: center; min-width: 0; gap: 4px; padding: 0 2px; color: #eef4f6; background: #000; font-size: 11px; line-height: 20px; white-space: nowrap; font-variant-numeric: tabular-nums; }
.channel-label { padding: 0 3px; font-weight: 400; background: transparent; }.channel-label.active { color: #fff; background: #008fda; }
.channel-row b { margin-left: 4px; color: #f0f4f6; font-weight: 400; }
.channel-middle { display: flex; gap: 7px; color: #eef4f6; }.channel-stop { justify-self: end; }
.sr-only { position: absolute; width: 1px; height: 1px; padding: 0; margin: -1px; overflow: hidden; clip: rect(0, 0, 0, 0); white-space: nowrap; border: 0; }
</style>
