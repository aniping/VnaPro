<script setup lang="ts">
import type { DisplayFormat } from '../../domain/presentation'
import type { Measurement } from '../../api/types'
import { measurementFormatInfo, frequencyText, numberText } from '../../plot/formatting'
import type { MarkerResult } from '../../plot/traceMath'
import { smithReadout } from '../../plot/smithMath'

interface MarkerReadoutItem {
  name: string
  color: string
  format: DisplayFormat
  measurement: Measurement
  result: MarkerResult
  active: boolean
}
defineProps<{ markers: MarkerReadoutItem[] }>()
const emit = defineEmits<{ select: [name: string] }>()
</script>

<template>
  <div v-if="markers.length" class="marker-readouts" aria-label="Marker readout">
    <button v-for="marker in markers" :key="marker.name" type="button" class="marker-row"
      :class="{ active: marker.active }" :style="{ '--marker-color': marker.color }"
      :aria-current="marker.active ? 'true' : undefined"
      :aria-label="`${marker.name} M1, ${frequencyText(marker.result.frequencyHz, 10)}, ${marker.result.valid ? marker.format === 'smith' ? smithReadout(marker.result.real!, marker.result.imaginary!) : numberText(marker.result.value, 8) + ' ' + measurementFormatInfo(marker.format, marker.measurement).unit : 'invalid point'}`"
      :title="`${marker.name}: real ${numberText(marker.result.real ?? NaN, 10)}, imaginary ${numberText(marker.result.imaginary ?? NaN, 10)} ${marker.measurement.unit}`"
      @click.stop="emit('select', marker.name)">
      <span class="active-dot" aria-hidden="true">{{ marker.active ? '•' : '' }}</span>
      <span class="marker-number">M1</span>
      <span class="marker-frequency">{{ frequencyText(marker.result.frequencyHz, 10) }}</span>
      <span v-if="marker.result.valid" class="marker-value">
        <template v-if="marker.format === 'smith'">{{ smithReadout(marker.result.real!, marker.result.imaginary!) }}</template>
        <template v-else>
        {{ numberText(marker.result.value, 8) }} {{ measurementFormatInfo(marker.format, marker.measurement).shortUnit }}
        <template v-if="marker.format === 'realimag'"> / {{ numberText(marker.result.imaginary ?? NaN, 8) }} {{ marker.measurement.unit }}</template>
        </template>
      </span>
      <span v-else class="marker-value invalid">Invalid point</span>
    </button>
  </div>
</template>

<style scoped>
.marker-readouts { position: absolute; z-index: 3; top: 2px; right: 3px; display: grid; gap: 0; max-width: calc(100% - 45px); padding: 1px 2px; background: #000b; border: 0; pointer-events: auto; }
.marker-row { display: grid; grid-template-columns: 7px 18px 1fr auto; align-items: center; gap: 3px; min-height: 16px; padding: 0; border: 0; color: var(--marker-color); background: transparent; font: inherit; font-size: 11px; line-height: 16px; font-variant-numeric: tabular-nums; cursor: pointer; white-space: nowrap; }
.active-dot { text-align: center; font-size: 12px; line-height: 12px; }
.marker-number { font-weight: 600; text-align: left; }
.marker-frequency { min-width: 85px; text-align: right; }
.marker-value { min-width: 75px; text-align: right; }
.invalid { color: #c6ced2; }
.marker-row:focus-visible { outline: 1px solid #fff; outline-offset: 2px; }
.marker-row:hover { background: #ffffff0c; }
</style>
