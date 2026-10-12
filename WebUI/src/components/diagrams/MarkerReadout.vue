<script setup lang="ts">
import type { DisplayFormat } from '../../domain/presentation'
import type { Measurement } from '../../api/types'
import {
  measurementFormatInfo,
  frequencyText,
  markerFrequencyText,
  markerValueText,
  numberText,
} from '../../plot/formatting'
import type { MarkerResult } from '../../plot/traceMath'
import { smithReadout } from '../../plot/smithMath'

interface MarkerReadoutItem {
  name: string
  markerId: number
  color: string
  format: DisplayFormat
  measurement: Measurement
  result: MarkerResult
  active: boolean
  preview?: boolean
}
defineProps<{ markers: MarkerReadoutItem[]; canEditDisplay: boolean }>()
const emit = defineEmits<{
  select: [value: { name: string; markerId: number }]
}>()
</script>

<template>
  <div
    v-if="markers.length"
    class="marker-readouts"
    aria-label="Marker readout"
  >
    <button
      v-for="marker in markers"
      :key="`${marker.name}-M${marker.markerId}`"
      type="button"
      class="marker-row"
      :disabled="!canEditDisplay"
      :class="{ active: marker.active }"
      :style="{ '--marker-color': marker.color }"
      :aria-current="marker.active ? 'true' : undefined"
      :data-marker-preview="marker.preview ? 'true' : undefined"
      :aria-label="`${marker.name} M${marker.markerId}, ${frequencyText(marker.result.frequencyHz, 10)}, ${marker.result.valid ? (marker.format === 'smith' ? smithReadout(marker.result) : numberText(marker.result.value, 8) + ' ' + measurementFormatInfo(marker.format, marker.measurement).unit) : 'invalid point'}`"
      :title="`${marker.name}: real ${numberText(marker.result.real ?? NaN, 10)}, imaginary ${numberText(marker.result.imaginary ?? NaN, 10)} ${marker.measurement.unit}`"
      @click.stop="
        emit('select', { name: marker.name, markerId: marker.markerId })
      "
    >
      <span class="active-dot" aria-hidden="true">{{
        marker.active ? '•' : ''
      }}</span>
      <span class="marker-number">M{{ marker.markerId }}</span>
      <span class="marker-frequency">{{
        markerFrequencyText(marker.result.frequencyHz)
      }}</span>
      <span v-if="marker.result.valid" class="marker-value">
        <template v-if="marker.format === 'smith'">{{
          smithReadout(marker.result)
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
  max-height: 45%;
  overflow: auto;
  scrollbar-width: thin;
  padding: 1px 2px;
  background: #323f48;
  border: 1px solid #546a78;
  pointer-events: auto;
}

.marker-row {
  display: grid;
  grid-template-columns: 5px 21px 1fr auto;
  align-items: center;
  gap: 3px;
  min-height: 14px;
  padding: 0;
  border: 0;
  color: #fff;
  background: transparent;
  font:
    11px/14px Tahoma,
    Arial,
    sans-serif;
  font-variant-numeric: tabular-nums;
  cursor: pointer;
  white-space: nowrap;
}

.active-dot {
  color: var(--marker-color);
  text-align: center;
  font-size: 12px;
  line-height: 12px;
}

.marker-number {
  font-weight: 400;
  text-align: left;
}

.marker-frequency {
  min-width: 85px;
  text-align: right;
}

.marker-value {
  min-width: 75px;
  text-align: right;
}

.invalid {
  color: #c6ced2;
}

.marker-row:focus-visible {
  outline: 1px solid #fff;
  outline-offset: 2px;
}

.marker-row:hover {
  background: #ffffff0c;
}
</style>
