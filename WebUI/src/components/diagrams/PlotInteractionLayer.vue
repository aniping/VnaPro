<script setup lang="ts">
import { computed, nextTick, onMounted, onScopeDispose, ref, watch } from 'vue'
import type { StimulusZoomSelection } from '../../api/types'
import { isCircularFormat, type DisplayFormat, type DisplayScale } from '../../domain/presentation'
import type { PlotRect } from '../../plot/canvasRenderer'
import {
  createStimulusZoomGeometry,
  createTracePanGeometry,
  type PlotGesturePoint,
  type StimulusZoomGeometry,
  type TracePanGeometry,
  type TracePanMove,
} from '../../plot/plotGestures'

const props = defineProps<{
  name: string
  format: DisplayFormat
  scale: DisplayScale
  revision: string
  displayRevision: string
  range: { min: number; max: number }
  rect: PlotRect
  size: { width: number; height: number }
  canPan: boolean
  zoomSelecting: boolean
  canZoom: boolean
  canContinueGesture: boolean
  commitPan?: (value: TracePanMove) => Promise<boolean>
  commitZoom?: (value: StimulusZoomSelection) => Promise<boolean>
}>()
const emit = defineEmits<{
  preview: [scale: DisplayScale | null]
  active: [value: boolean]
  cancelZoom: []
}>()

type Gesture = {
  pointer: number
  target: HTMLElement
  start: PlotGesturePoint
  latest: PlotGesturePoint
  bounds: PlotRect
  moved: boolean
} & (
  | { mode: 'pan'; geometry: TracePanGeometry; commit: (value: TracePanMove) => Promise<boolean> }
  | { mode: 'zoom'; geometry: StimulusZoomGeometry; commit: (value: StimulusZoomSelection) => Promise<boolean> }
)
const layer = ref<HTMLElement | null>(null)
const captured = ref(false)
const committing = ref(false)
const box = ref<PlotRect | null>(null)
let gesture: Gesture | null = null
let frame: number | null = null
let observer: ResizeObserver | null = null
let suppressClick = false
let disposed = false

const validRect = computed(() =>
  [props.rect.left, props.rect.top, props.rect.width, props.rect.height, props.size.width, props.size.height].every(Number.isFinite) &&
  props.rect.left >= 0 && props.rect.top >= 0 && props.rect.width > 0 && props.rect.height > 0 &&
  props.size.width > 0 && props.size.height > 0 &&
  props.rect.left + props.rect.width <= props.size.width && props.rect.top + props.rect.height <= props.size.height,
)
const panReady = computed(() => Boolean(props.commitPan && createTracePanGeometry({
  name: props.name, format: props.format, scale: props.scale,
  displayRevision: props.displayRevision, startClientY: 0, plotHeightCSS: props.rect.height,
})))
const zoomReady = computed(() => Boolean(props.commitZoom && createStimulusZoomGeometry({
  traceName: props.name, format: props.format, scale: props.scale,
  revision: props.revision, displayRevision: props.displayRevision, range: props.range,
  rect: props.rect, size: props.size,
  bounds: { left: 0, top: 0, width: props.size.width, height: props.size.height },
})))
const ready = computed(() => !committing.value && props.canContinueGesture && validRect.value &&
  !isCircularFormat(props.format) && (props.zoomSelecting
    ? props.canZoom && zoomReady.value
    : props.canPan && panReady.value))
const placement = computed(() => ({
  pointerEvents: ready.value || captured.value ? 'auto' as const : 'none' as const,
  cursor: props.zoomSelecting ? 'crosshair' : captured.value ? 'grabbing' : 'default',
}))

function cancelFrame() {
  if (frame !== null) cancelAnimationFrame(frame)
  frame = null
}
function release(value: Gesture) {
  captured.value = false
  if (value.target.hasPointerCapture?.(value.pointer)) value.target.releasePointerCapture(value.pointer)
}
function clearPreview() {
  box.value = null
  emit('preview', null)
}
function cancelGesture() {
  if (!gesture) return
  const previous = gesture
  gesture = null
  cancelFrame()
  release(previous)
  clearPreview()
  emit('active', false)
}
function point(event: PointerEvent): PlotGesturePoint {
  return { x: event.clientX, y: event.clientY }
}
function begin(event: PointerEvent) {
  if (!ready.value || gesture || event.button !== 0 || event.isPrimary === false) return
  const target = event.currentTarget as HTMLElement | null
  const bounds = target?.getBoundingClientRect()
  if (!target || !bounds || ![bounds.left, bounds.top, bounds.width, bounds.height, event.clientX, event.clientY].every(Number.isFinite) ||
    bounds.width <= 0 || bounds.height <= 0) return
  const x = (event.clientX - bounds.left) * props.size.width / bounds.width
  const y = (event.clientY - bounds.top) * props.size.height / bounds.height
  if (x < props.rect.left || x > props.rect.left + props.rect.width ||
    y < props.rect.top || y > props.rect.top + props.rect.height) return
  const base = { pointer: event.pointerId, target, start: point(event), latest: point(event),
    bounds: { left: bounds.left, top: bounds.top, width: bounds.width, height: bounds.height }, moved: false }
  if (props.zoomSelecting) {
    const geometry = createStimulusZoomGeometry({
      traceName: props.name, format: props.format, scale: props.scale,
      revision: props.revision, displayRevision: props.displayRevision, range: props.range,
      rect: props.rect, bounds: base.bounds, size: props.size,
    })
    if (!geometry || !props.commitZoom) return
    gesture = { ...base, mode: 'zoom', geometry, commit: props.commitZoom }
  } else {
    const geometry = createTracePanGeometry({
      name: props.name, format: props.format, scale: props.scale, displayRevision: props.displayRevision,
      startClientY: event.clientY, plotHeightCSS: bounds.height * props.rect.height / props.size.height,
    })
    if (!geometry || !props.commitPan) return
    gesture = { ...base, mode: 'pan', geometry, commit: props.commitPan }
  }
  try { target.setPointerCapture(event.pointerId) } catch { gesture = null; return }
  suppressClick = false
  captured.value = true
  emit('active', true)
}
function draw(value: Gesture) {
  if (!value.moved) return
  if (value.mode === 'pan') {
    emit('preview', value.geometry.scaleAt(value.latest.y))
  } else {
    const start = value.geometry.pointAt(value.start)
    const end = value.geometry.pointAt(value.latest)
    if (start && end) box.value = {
      left: Math.min(start.x, end.x), top: Math.min(start.y, end.y),
      width: Math.abs(end.x - start.x), height: Math.abs(end.y - start.y),
    }
  }
}
function update(event: PointerEvent) {
  const value = gesture
  if (!value || value.pointer !== event.pointerId || ![event.clientX, event.clientY].every(Number.isFinite)) return
  value.latest = point(event)
  value.moved ||= value.mode === 'pan'
    ? Math.abs(value.latest.y - value.start.y) >= 3
    : Math.hypot(value.latest.x - value.start.x, value.latest.y - value.start.y) >= 6
  if (!value.moved) return
  suppressClick = true
  event.preventDefault()
  event.stopPropagation()
  if (frame === null) frame = requestAnimationFrame(() => {
    frame = null
    if (gesture === value) draw(value)
  })
}
async function finish(event: PointerEvent) {
  const value = gesture
  if (!value || value.pointer !== event.pointerId) return
  update(event)
  cancelFrame()
  draw(value)
  gesture = null
  // 清除本地指针后再释放 capture，正常的 lostpointercapture 不取消在途提交。
  release(value)
  if (!value.moved) { clearPreview(); emit('active', false); return }
  event.preventDefault()
  event.stopPropagation()
  const command = value.mode === 'pan'
    ? value.geometry.commandAt(value.latest.y)
    : value.geometry.selectionAt(value.start, value.latest)
  if (!command || !props.canContinueGesture) { clearPreview(); emit('active', false); return }
  committing.value = true
  try {
    if (value.mode === 'pan') await value.commit(command as TracePanMove)
    else await value.commit(command as StimulusZoomSelection)
    // commit 的 resolve 已包含权威读回；等父组件把读回值传下来再清除预览。
    await nextTick()
  } catch {
    // 统一命令入口负责显示失败；本层只清理局部手势和预览。
  } finally {
    committing.value = false
    if (!disposed) { clearPreview(); emit('active', false) }
  }
}
function cancelPointer(event: PointerEvent) {
  if (gesture?.pointer === event.pointerId) cancelGesture()
}
function click(event: MouseEvent) {
  if (!suppressClick || event.detail === 0) return
  suppressClick = false
  event.preventDefault()
  event.stopPropagation()
}
function escape(event: KeyboardEvent) {
  if (event.key !== 'Escape' || committing.value || (!gesture && !props.zoomSelecting)) return
  const zoom = gesture?.mode === 'zoom' || props.zoomSelecting
  event.preventDefault()
  event.stopPropagation()
  cancelGesture()
  if (zoom) emit('cancelZoom')
}
function resized() { cancelGesture() }
watch([
  () => props.name, () => props.format, () => props.revision, () => props.displayRevision,
  () => props.scale.min, () => props.scale.max, () => props.scale.referencePosition,
  () => props.range.min, () => props.range.max,
  () => props.rect.left, () => props.rect.top, () => props.rect.width, () => props.rect.height,
  () => props.size.width, () => props.size.height, () => props.canContinueGesture,
  () => props.zoomSelecting,
], cancelGesture, { flush: 'sync' })
onMounted(() => {
  window.addEventListener('keydown', escape, true)
  window.addEventListener('resize', resized)
  if (typeof ResizeObserver !== 'undefined' && layer.value) {
    observer = new ResizeObserver(() => {
      if (!gesture) return
      const bounds = gesture.target.getBoundingClientRect()
      if (bounds.width !== gesture.bounds.width || bounds.height !== gesture.bounds.height) cancelGesture()
    })
    observer.observe(layer.value)
  }
})
onScopeDispose(() => {
  cancelGesture()
  cancelFrame()
  disposed = true
  observer?.disconnect()
  if (typeof window !== 'undefined') {
    window.removeEventListener('keydown', escape, true)
    window.removeEventListener('resize', resized)
  }
})
</script>

<template>
  <div
    ref="layer"
    class="plot-interaction-layer"
    :style="placement"
    @pointerdown="begin"
    @pointermove="update"
    @pointerup="finish"
    @pointercancel="cancelPointer"
    @lostpointercapture="cancelPointer"
    @click="click"
    @keydown="escape"
  >
    <svg v-if="box" class="plot-zoom-preview" :viewBox="`0 0 ${size.width} ${size.height}`" preserveAspectRatio="none" aria-hidden="true">
      <rect :x="box.left" :y="box.top" :width="box.width" :height="box.height" />
    </svg>
  </div>
</template>

<style scoped>
.plot-interaction-layer { position: absolute; inset: 0; z-index: 0; touch-action: none; user-select: none; }
.plot-zoom-preview { position: absolute; inset: 0; width: 100%; height: 100%; overflow: visible; pointer-events: none; }
.plot-zoom-preview rect { fill: color-mix(in srgb, var(--color-focus) 12%, transparent); stroke: var(--color-focus); stroke-width: 1; vector-effect: non-scaling-stroke; }
</style>
