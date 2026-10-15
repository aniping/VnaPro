<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import {
  isCircularFormat,
  type DisplayFormat,
  type DisplayScale,
} from '../../domain/presentation'
import type { PlotRect } from '../../plot/canvasRenderer'
import {
  createReferencePositionGeometry,
  previewReferenceScale,
  ReferencePositionDragController,
  type ReferencePositionMove,
} from '../../plot/referencePositionDrag'

const props = defineProps<{
  name: string
  format: DisplayFormat
  scale: DisplayScale
  color: string
  active: boolean
  canEditDisplay: boolean
  canContinueGesture?: boolean
  displayRevision: string
  rect: PlotRect
  plotSize: { width: number; height: number }
  commit: (move: ReferencePositionMove) => Promise<boolean>
}>()
const emit = defineEmits<{
  preview: [value: { name: string; referencePosition: number } | null]
  select: [name: string]
  active: [value: boolean]
}>()
const preview = ref<number | null>(null)
const committing = ref(false)
const gestureActive = ref(false)
let pointer: number | null = null
let capture: HTMLElement | null = null
let draggingName = ''
let disposed = false
const controller = new ReferencePositionDragController({
  requestFrame: (callback) => requestAnimationFrame(callback),
  cancelFrame: (id) => cancelAnimationFrame(id),
  preview(position, pending) {
    if (disposed) return
    preview.value = position
    committing.value = pending
    emit(
      'preview',
      position === null
        ? null
        : { name: draggingName, referencePosition: position },
    )
  },
})
const position = computed(
  () => preview.value ?? props.scale.referencePosition ?? 8,
)
const ready = computed(
  () =>
    props.canEditDisplay &&
    !isCircularFormat(props.format) &&
    !!props.name &&
    /^[0-9]+$/.test(props.displayRevision) &&
    !!previewReferenceScale(
      props.scale,
      props.format,
      props.scale.referencePosition ?? 8,
    ) &&
    [
      props.rect.left,
      props.rect.top,
      props.rect.width,
      props.rect.height,
      props.plotSize.width,
      props.plotSize.height,
    ].every(Number.isFinite) &&
    props.rect.width > 0 &&
    props.rect.height > 0 &&
    props.plotSize.width > 0 &&
    props.plotSize.height > 0,
)
const placement = computed(() => {
  const width = Math.min(24, props.plotSize.width)
  const height = Math.min(24, props.plotSize.height)
  const y = props.rect.top + (1 - position.value / 10) * props.rect.height
  return {
    left: `${Math.max(0, props.rect.left + props.rect.width - width)}px`,
    top: `${Math.max(0, Math.min(props.plotSize.height - height, y - height / 2))}px`,
    width: `${width}px`,
    height: `${height}px`,
    color: props.color,
    // 信息字段和 Marker 保持可点击；同位置参考把手仍由本图活动迹线优先。
    zIndex: props.active ? 2 : 1,
  }
})

function releaseCapture() {
  const target = capture
  const id = pointer
  capture = null
  pointer = null
  gestureActive.value = false
  if (id !== null && target?.hasPointerCapture?.(id))
    target.releasePointerCapture(id)
}
function cancelGesture() {
  if (!controller.dragging) return
  controller.cancel()
  releaseCapture()
  emit('active', false)
}
function begin(event: PointerEvent) {
  if (
    event.button !== 0 ||
    event.isPrimary === false ||
    !ready.value ||
    controller.pending
  )
    return
  const target = event.currentTarget as HTMLElement | null
  const bounds = target?.parentElement?.getBoundingClientRect()
  if (!target || !bounds) return
  const geometry = createReferencePositionGeometry({
    name: props.name,
    displayRevision: props.displayRevision,
    startClientY: event.clientY,
    startPosition: props.scale.referencePosition ?? 8,
    plotHeightCSS: (bounds.height * props.rect.height) / props.plotSize.height,
  })
  if (!geometry || !controller.begin(event.pointerId, event.clientY, geometry))
    return
  try {
    target.setPointerCapture(event.pointerId)
  } catch {
    controller.cancel()
    return
  }
  event.preventDefault()
  event.stopPropagation()
  draggingName = props.name
  pointer = event.pointerId
  capture = target
  gestureActive.value = true
  emit('active', true)
}
function move(event: PointerEvent) {
  if (event.pointerId !== pointer) return
  event.preventDefault()
  event.stopPropagation()
  controller.move(event.pointerId, event.clientY)
}
async function finish(event: PointerEvent) {
  if (event.pointerId !== pointer) return
  event.preventDefault()
  event.stopPropagation()
  const pending = controller.finish(
    event.pointerId,
    event.clientY,
    async (command) => {
      const result = await props.commit(command)
      // commit 已等后端读回；再让父组件将权威快照刷新到本组件。
      await nextTick()
      return result
    },
    () => emit('select', draggingName),
  )
  releaseCapture()
  emit('active', false)
  // 统一命令入口负责显示失败信息；事件处理器只负责回滚局部预览。
  await pending.catch(() => undefined)
}
function cancelPointer(event: PointerEvent) {
  if (event.pointerId === pointer) cancelGesture()
}
function escape(event: KeyboardEvent) {
  if (event.key !== 'Escape' || !controller.dragging) return
  event.preventDefault()
  event.stopPropagation()
  cancelGesture()
}
function resized() {
  cancelGesture()
}
function activate(event: MouseEvent) {
  // 键盘或辅助技术触发的 click 没有对应 pointerup；鼠标完成手势后不重复选择。
  if (event.detail === 0 && ready.value && !controller.pending)
    emit('select', props.name)
}
watch(
  [
    () => props.name,
    () => props.format,
    () => props.displayRevision,
    () => props.canContinueGesture ?? props.canEditDisplay,
    () => props.scale.min,
    () => props.scale.max,
    () => props.scale.referencePosition,
    () => props.rect.left,
    () => props.rect.top,
    () => props.rect.width,
    () => props.rect.height,
    () => props.plotSize.width,
    () => props.plotSize.height,
  ],
  cancelGesture,
  { flush: 'sync' },
)
onMounted(() => {
  window.addEventListener('keydown', escape)
  window.addEventListener('resize', resized)
})
onBeforeUnmount(() => {
  cancelGesture()
  controller.cancel()
  disposed = true
  window.removeEventListener('keydown', escape)
  window.removeEventListener('resize', resized)
})
</script>

<template>
  <button
    v-if="!isCircularFormat(format)"
    type="button"
    class="reference-position-handle"
    :class="{ active, committing }"
    :style="placement"
    :disabled="(!ready && !gestureActive) || committing"
    :aria-label="`${name} reference position ${position} divisions. Drag vertically to move.`"
    :title="`${name} Ref Pos ${position.toFixed(3)} div`"
    @pointerdown="begin"
    @pointermove="move"
    @pointerup="finish"
    @pointercancel="cancelPointer"
    @lostpointercapture="cancelPointer"
    @keydown="escape"
    @click.stop.prevent="activate"
  />
</template>

<style scoped>
.reference-position-handle {
  position: absolute;
  padding: 0;
  border: 0;
  background: transparent;
  cursor: ns-resize;
  touch-action: none;
  user-select: none;
}
.reference-position-handle:disabled {
  cursor: default;
}
.reference-position-handle:focus-visible {
  outline: 1px solid currentColor;
  outline-offset: -1px;
}
</style>
