<script setup lang="ts">
import type { Measurement } from '../../api/types'
import type {
  DisplayFormat,
  DisplayScale,
  TracePresentation,
} from '../../domain/presentation'
import { isCircularFormat } from '../../domain/presentation'
import { FORMAT_INFO, numberText, scaleValue } from '../../plot/formatting'
import { measurementState } from '../../domain/measurements'
import {
  referenceLevel,
  usesRelativeAxis,
  scaleCoordinate,
} from '../../plot/traceMath'

defineProps<{
  measurement: Measurement
  presentation: TracePresentation
  scale: DisplayScale
  color: string
  active: boolean
  smithRadius: number
  canEditDisplay: boolean
}>()
const emit = defineEmits<{
  select: [name: string]
  toggleVisibility: [name: string]
}>()

function divisionText(
  scale: DisplayScale,
  format: DisplayFormat,
  measurement: Measurement,
): string {
  const division =
    (scaleCoordinate(scale.max, format) - scaleCoordinate(scale.min, format)) /
    10
  return format === 'logMagnitude'
    ? `${numberText(division, 5)} dec`
    : scaleValue(division, format, 5, measurement)
}

function circularDescription(
  presentation: TracePresentation,
  measurement: Measurement,
): string {
  if (presentation.format === 'polar') return 'Polar · 复数 Re/Im'
  if (
    measurement.kind !== 'sParameter' ||
    measurement.receiverPort !== measurement.sourcePort
  )
    return `${FORMAT_INFO[presentation.format].label} · 复数 Re/Im`
  return presentation.format === 'invSmith'
    ? 'Inv Smith · 归一化导纳 y'
    : 'Smith · Γ 与归一化阻抗 z'
}
</script>

<template>
  <div
    class="trace-strip"
    :class="{ active, hidden: !presentation.visible }"
    :style="{ '--trace-color': color }"
  >
    <button
      class="trace-select"
      type="button"
      :disabled="!canEditDisplay"
      :aria-pressed="active"
      :title="
        isCircularFormat(presentation.format)
          ? `${measurement.name} · ${measurement.parameter} · ${circularDescription(presentation, measurement)} · 未校准 · 当前外圈幅度 ${smithRadius} · 本迹线保存值 ${presentation.smithRadius}`
          : `${measurement.name} · ${measurement.parameter} · ${measurementState(measurement)} · ${divisionText(scale, presentation.format, measurement)} per division · Ref ${scaleValue(referenceLevel(scale, presentation.format), presentation.format, 9, measurement)}${usesRelativeAxis(scale, presentation.format) ? ' · Δ 刻度为相对 Ref 的偏移' : ''}`
      "
      :aria-label="`Select ${measurement.name}, ${measurement.parameter}, ${measurementState(measurement)}`"
      @click="emit('select', measurement.name)"
    >
      <span class="trace-number">{{ measurement.name }}</span>
      <span class="measurement-chip">{{ measurement.parameter }}</span>
      <span class="trace-format">{{
        FORMAT_INFO[presentation.format].label
      }}</span>
      <template v-if="isCircularFormat(presentation.format)">
        <span class="trace-reference"
          >Ref {{ smithRadius.toPrecision(4) }} U</span
        >
      </template>
      <template v-else>
        <span class="trace-scale"
          >{{ divisionText(scale, presentation.format, measurement) }}/</span
        >
        <span class="trace-reference"
          >Ref
          {{
            scaleValue(
              referenceLevel(scale, presentation.format),
              presentation.format,
              9,
              measurement,
            )
          }}</span
        >
      </template>
      <span v-if="measurement.kind === 'receiver'" class="trace-state"
        >RAW</span
      >
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
  opacity: 0.5;
}

.trace-select {
  display: flex;
  align-items: center;
  min-width: 0;
  max-width: 100%;
  gap: 0;
  padding: 0;
  border: 0;
  color: inherit;
  background: transparent;
  text-align: left;
  font:
    11px/18px Tahoma,
    Arial,
    sans-serif;
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
  flex-shrink: 0;
  box-sizing: border-box;
}

.trace-number {
  min-width: 6ch;
}

.measurement-chip {
  min-width: 5ch;
  color: #111518;
  background: var(--trace-color);
}

.trace-format {
  padding: 0 4px;
  color: inherit;
  font-weight: 400;
  flex-shrink: 0;
}

.trace-scale,
.trace-reference,
.trace-state {
  padding-right: 4px;
  color: inherit;
}

.trace-scale,
.trace-state {
  flex-shrink: 0;
}

.trace-reference {
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
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
