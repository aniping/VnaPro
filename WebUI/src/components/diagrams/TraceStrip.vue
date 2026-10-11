<script setup lang="ts">
import type { Measurement } from '../../api/types'
import type { DisplayScale, TracePresentation } from '../../domain/presentation'
import { FORMAT_INFO, scaleValue } from '../../plot/formatting'
import { measurementState } from '../../domain/measurements'
import { referenceLevel, usesRelativeAxis } from '../../plot/traceMath'

defineProps<{
  measurement: Measurement
  index: number
  presentation: TracePresentation
  scale: DisplayScale
  color: string
  active: boolean
}>()
const emit = defineEmits<{ select: [name: string]; toggleVisibility: [name: string] }>()
</script>

<template>
  <div class="trace-strip" :class="{ active, hidden: !presentation.visible }" :style="{ '--trace-color': color }">
    <button class="trace-select" type="button" :aria-pressed="active"
      :title="`${measurement.name} · ${measurement.parameter} · ${measurementState(measurement)} · ${scaleValue((scale.max - scale.min) / 10, presentation.format, 5, measurement)} per division · Ref ${scaleValue(referenceLevel(scale), presentation.format, 9, measurement)}${usesRelativeAxis(scale) ? ' · Δ 刻度为相对 Ref 的偏移' : ''}`"
      :aria-label="`Select ${measurement.name}, ${measurement.parameter}, ${measurementState(measurement)}`"
      @click="emit('select', measurement.name)">
      <span class="trace-number">Trc{{ index + 1 }}</span>
      <span class="measurement-chip">{{ measurement.parameter }}</span>
      <span class="trace-format">{{ FORMAT_INFO[presentation.format].label }}</span>
      <span class="trace-scale">{{ scaleValue((scale.max - scale.min) / 10, presentation.format, 5, measurement) }}/</span>
      <span class="trace-reference">Ref {{ scaleValue(referenceLevel(scale), presentation.format, 9, measurement) }}</span>
      <span class="trace-state">{{ measurementState(measurement) }}</span>
    </button>
  </div>
</template>

<style scoped>
.trace-strip { display: flex; width: max-content; max-width: 100%; min-height: 21px; border: 0; border-radius: 0; color: var(--trace-color); background: transparent; }
.trace-strip.active { color: #fff; background: #0085c5; }
.trace-strip.hidden { opacity: .5; }
.trace-select { display: flex; align-items: center; min-width: 0; gap: 0; padding: 0; border: 0; color: inherit; background: transparent; text-align: left; font: inherit; font-size: 11px; white-space: nowrap; cursor: pointer; }
.trace-number { display: flex; align-items: center; align-self: stretch; padding: 0 3px; font-weight: 400; }
.measurement-chip { display: flex; align-items: center; align-self: stretch; padding: 0 3px; color: #15202a; background: var(--trace-color); font-weight: 600; }
.trace-format { padding: 0 3px; color: inherit; font-weight: 400; }
.trace-strip.active .trace-format { color: #fff; }
.trace-scale, .trace-reference, .trace-state { padding-right: 3px; color: inherit; font-variant-numeric: tabular-nums; }
button:focus-visible { position: relative; z-index: 4; outline: 2px solid #fff; outline-offset: -2px; }
button:hover { background: #ffffff12; }
</style>
