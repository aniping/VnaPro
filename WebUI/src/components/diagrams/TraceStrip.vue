<script setup lang="ts">
import type { Measurement } from '../../api/types'
import type { DisplayScale, TracePresentation } from '../../domain/presentation'
import { FORMAT_INFO, scaleValue } from '../../plot/formatting'
import { measurementState } from '../../domain/measurements'
import { referenceLevel, usesRelativeAxis } from '../../plot/traceMath'

defineProps<{
  measurement: Measurement
  presentation: TracePresentation
  scale: DisplayScale
  color: string
  active: boolean
  smithRadius: number
}>()
const emit = defineEmits<{ select: [name: string]; toggleVisibility: [name: string] }>()
</script>

<template>
  <div class="trace-strip" :class="{ active, hidden: !presentation.visible }" :style="{ '--trace-color': color }">
    <button class="trace-select" type="button" :aria-pressed="active"
      :title="presentation.format === 'smith' ? `${measurement.name} · ${measurement.parameter} · 未校准 · 当前图窗外圈 |Γ| ${smithRadius} · 本迹线保存值 ${presentation.smithRadius} · z 为归一化阻抗` : `${measurement.name} · ${measurement.parameter} · ${measurementState(measurement)} · ${scaleValue((scale.max - scale.min) / 10, presentation.format, 5, measurement)} per division · Ref ${scaleValue(referenceLevel(scale), presentation.format, 9, measurement)}${usesRelativeAxis(scale) ? ' · Δ 刻度为相对 Ref 的偏移' : ''}`"
      :aria-label="`Select ${measurement.name}, ${measurement.parameter}, ${measurementState(measurement)}`"
      @click="emit('select', measurement.name)">
      <span class="trace-number">{{ measurement.name }}</span>
      <span class="measurement-chip">{{ measurement.parameter }}</span>
      <span class="trace-format">{{ FORMAT_INFO[presentation.format].label }}</span>
      <template v-if="presentation.format === 'smith'">
        <span class="trace-reference">Ref {{ smithRadius.toPrecision(4) }} U</span>
      </template>
      <template v-else>
        <span class="trace-scale">{{ scaleValue((scale.max - scale.min) / 10, presentation.format, 5, measurement) }}/</span>
        <span class="trace-reference">Ref {{ scaleValue(referenceLevel(scale), presentation.format, 9, measurement) }}</span>
      </template>
      <span v-if="measurement.kind === 'receiver'" class="trace-state">RAW</span>
    </button>
  </div>
</template>

<style scoped>
.trace-strip {
  display: flex;
  width: max-content;
  max-width: 100%;
  min-height: 18px;
  color: var(--trace-color);
  background: transparent;
}

.trace-strip.active {
  color: #fff;
  background: #0d75bf;
}

.trace-strip.hidden {
  opacity: .5;
}

.trace-select {
  display: flex;
  align-items: center;
  min-width: 0;
  gap: 0;
  padding: 0;
  border: 0;
  color: inherit;
  background: transparent;
  text-align: left;
  font: 11px/18px Tahoma, Arial, sans-serif;
  white-space: nowrap;
  cursor: pointer;
}

.trace-number,
.measurement-chip {
  display: flex;
  align-items: center;
  align-self: stretch;
  padding: 0 4px;
  font-weight: 400;
}

.measurement-chip {
  color: #111518;
  background: var(--trace-color);
}

.trace-format {
  padding: 0 4px;
  color: inherit;
  font-weight: 400;
}

.trace-scale,
.trace-reference,
.trace-state {
  padding-right: 4px;
  color: inherit;
}

button:focus-visible {
  position: relative;
  z-index: 4;
  outline: 2px solid #fff;
  outline-offset: -2px;
}

button:hover {
  background: #ffffff12;
}
</style>
