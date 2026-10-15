<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import type { MarkerInfoLocation } from '../../domain/presentation'
import { isCircularFormat } from '../../domain/presentation'
import {
  measurementFormatInfo,
  frequencyText,
  markerFrequencyText,
  markerValueText,
  numberText,
} from '../../plot/formatting'
import { circularReadout } from '../../plot/smithMath'
import {
  MARKER_INFO_DRAG_TYPE,
  type MarkerReadoutItem,
} from '../../domain/markerReadouts'
const props = defineProps<{
  markers: MarkerReadoutItem[]
  canEditDisplay: boolean
  inline?: boolean
  showTraceNames?: boolean
  stimulusInfoVisible?: boolean
  fontScale?: number
  location?: MarkerInfoLocation
  infoTableVisible?: boolean
}>()
// 多迹线可重复使用 M1..M10；归属列辅助区分，不重新编号或排序后端结果。
const showTraceNames = computed(
  () =>
    props.showTraceNames ||
    new Set(props.markers.map((marker) => marker.name)).size > 1,
)
const stimulusColumns = computed(() =>
  [
    '5px',
    'var(--marker-number-width)',
    ...(showTraceNames.value ? ['minmax(0, var(--marker-trace-width))'] : []),
    ...(props.stimulusInfoVisible !== false ? ['minmax(0, max-content)'] : []),
  ].join(' '),
)
const emit = defineEmits<{
  select: [value: { name: string; markerId: number }]
  relocate: [value: { name: string; location: MarkerInfoLocation }]
}>()
const root = ref<HTMLElement>()
const contextTrace = ref('')
const targetLocation = computed(() =>
  props.location === 'table' ? 'diagram' : 'table',
)
const canRelocate = computed(
  () =>
    props.canEditDisplay &&
    (targetLocation.value !== 'table' || props.infoTableVisible),
)

function openContext(event: MouseEvent, name: string) {
  event.preventDefault()
  event.stopPropagation()
  contextTrace.value = name
}
function relocate() {
  if (contextTrace.value && canRelocate.value)
    emit('relocate', {
      name: contextTrace.value,
      location: targetLocation.value,
    })
  contextTrace.value = ''
}
function beginInfoDrag(event: DragEvent) {
  const name = props.markers[0]?.name
  if (!props.canEditDisplay || !name || !event.dataTransfer) {
    event.preventDefault()
    return
  }
  event.dataTransfer.effectAllowed = 'move'
  event.dataTransfer.setData(MARKER_INFO_DRAG_TYPE, name)
  contextTrace.value = ''
}
function closeOutside(event: PointerEvent) {
  if (event.target instanceof Node && !root.value?.contains(event.target))
    contextTrace.value = ''
}
onMounted(() => document.addEventListener('pointerdown', closeOutside))
onBeforeUnmount(() => document.removeEventListener('pointerdown', closeOutside))
</script>

<template>
  <div
    v-if="markers.length"
    ref="root"
    class="marker-readouts"
    :class="{
      'multiple-traces': showTraceNames,
      inline,
      'without-stimulus': stimulusInfoVisible === false,
    }"
    :style="{
      '--marker-font-size': `${(11 * (fontScale ?? 100)) / 100}px`,
      '--marker-line-height': `${(14 * (fontScale ?? 100)) / 100}px`,
      '--marker-number-width': `${(25 * (fontScale ?? 100)) / 100}px`,
      '--marker-trace-width': `${(42 * (fontScale ?? 100)) / 100}px`,
      '--marker-stimulus-columns': stimulusColumns,
    }"
    aria-label="Marker readout"
    :title="
      canEditDisplay
        ? 'Drag to move Marker info; right-click for options'
        : undefined
    "
    :draggable="canEditDisplay"
    @dragstart="beginInfoDrag"
    @keydown.esc.stop="contextTrace = ''"
  >
    <button
      v-for="marker in markers"
      :key="`${marker.name}-M${marker.markerId}`"
      type="button"
      class="marker-row"
      :disabled="!canEditDisplay"
      :class="{ active: marker.active }"
      :style="{ '--marker-color': marker.color, color: marker.color }"
      :aria-current="marker.active ? 'true' : undefined"
      :data-marker-preview="marker.preview ? 'true' : undefined"
      :aria-label="`${marker.name} M${marker.markerId}, ${frequencyText(marker.result.frequencyHz, 10)}, ${marker.result.valid ? (isCircularFormat(marker.format) ? circularReadout(marker.format, marker.result, marker.measurement.unit) : numberText(marker.result.value, 8) + ' ' + measurementFormatInfo(marker.format, marker.measurement).unit) : 'invalid point'}`"
      :title="`${marker.name}: real ${numberText(marker.result.real ?? NaN, 10)}, imaginary ${numberText(marker.result.imaginary ?? NaN, 10)} ${marker.measurement.unit}`"
      @click.stop="
        emit('select', { name: marker.name, markerId: marker.markerId })
      "
      @contextmenu="openContext($event, marker.name)"
    >
      <span class="marker-stimulus">
        <span class="active-dot" aria-hidden="true">{{
          marker.active ? '•' : ''
        }}</span>
        <span class="marker-number">M{{ marker.markerId }}</span>
        <span v-if="showTraceNames" class="marker-trace">{{ marker.name }}</span>
        <span v-if="stimulusInfoVisible !== false" class="marker-frequency">{{
          markerFrequencyText(marker.result.frequencyHz)
        }}</span>
      </span>
      <span v-if="marker.result.valid" class="marker-value">
        <template v-if="isCircularFormat(marker.format)">{{
          circularReadout(marker.format, marker.result, marker.measurement.unit)
        }}</template>
        <template v-else>
          {{ markerValueText(marker.result.value, marker.format) }}
          {{
            measurementFormatInfo(marker.format, marker.measurement).shortUnit
          }}
          <template v-if="marker.format === 'realimag'">
            / {{ numberText(marker.result.secondaryValue, 8) }}
            {{ marker.measurement.unit }}</template
          >
        </template>
      </span>
      <span v-else class="marker-value invalid">Invalid point</span>
    </button>
    <div
      v-if="contextTrace"
      class="marker-info-menu"
      role="menu"
      aria-label="Marker info field"
    >
      <button
        type="button"
        role="menuitem"
        :disabled="!canRelocate"
        :title="
          targetLocation === 'table' && !infoTableVisible
            ? '先在 Display 中开启 Info Table'
            : undefined
        "
        @click.stop="relocate"
      >
        {{
          targetLocation === 'table'
            ? 'Show Info in Table'
            : 'Show Info in Diagram'
        }}
      </button>
    </div>
  </div>
</template>

<style scoped>
.marker-readouts {
  position: absolute;
  z-index: 3;
  top: 2px;
  right: 2px;
  display: grid;
  gap: 0;
  max-width: calc(100% - 45px);
  padding: 1px 2px;
  background: transparent;
  pointer-events: auto;
}
.marker-readouts.inline {
  position: relative;
  top: auto;
  right: auto;
  min-width: 0;
  max-width: 100%;
  align-self: stretch;
}

.marker-row {
  display: flow-root;
  /* 仅在刺激信息与响应之间换行，避免 flex-wrap 缩窄自身的内在宽度。 */
  text-align: right;
  min-height: var(--marker-line-height, 14px);
  padding: 0;
  border: 0;
  color: inherit;
  background: transparent;
  font:
    var(--marker-font-size, 11px) / var(--marker-line-height, 14px) Tahoma,
    Arial,
    sans-serif;
  font-variant-numeric: tabular-nums;
  cursor: pointer;
  min-width: 0;
  max-width: 100%;
}
.marker-stimulus {
  float: left;
  display: inline-grid;
  grid-template-columns: var(--marker-stimulus-columns);
  align-items: baseline;
  column-gap: 3px;
  max-width: 100%;
  padding-right: 8px;
  vertical-align: top;
}
.marker-info-menu {
  position: absolute;
  z-index: 65;
  right: 0;
  top: 100%;
  padding: 3px;
  border: 1px solid #667d87;
  background: #33424b;
  box-shadow: 1px 3px 7px #0008;
}
.marker-info-menu button {
  padding: 5px 8px;
  white-space: nowrap;
  color: #edf3f5;
  border: 0;
  background: transparent;
}
.marker-info-menu button:disabled {
  color: #80929c;
}
.marker-info-menu button:not(:disabled):hover {
  background: #4a606d;
}

.active-dot {
  color: var(--marker-color);
  text-align: center;
  font-size: var(--marker-font-size, 12px);
  line-height: var(--marker-line-height, 12px);
}

.marker-number {
  font-weight: 400;
  text-align: left;
}

.marker-trace {
  min-width: 0;
  overflow-wrap: anywhere;
  text-align: left;
}

.marker-frequency {
  min-width: 0;
  overflow-wrap: anywhere;
  text-align: right;
}

.marker-value {
  display: inline-block;
  max-width: 100%;
  overflow-wrap: anywhere;
  text-align: right;
  vertical-align: top;
}

.invalid {
  font-style: italic;
}

.marker-row:focus-visible {
  outline: 1px solid #fff;
  outline-offset: 2px;
}

.marker-row:hover {
  background: #ffffff0c;
}
</style>
