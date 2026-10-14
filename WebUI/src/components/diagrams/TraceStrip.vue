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
import type { TraceTitleSections } from '../../plot/titleLayout'
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
  /** 图窗保存的局部选择，非活动图窗仍显示灰色标题。 */
  remembered?: boolean
  smithRadius: number
  canEditDisplay: boolean
  sections?: TraceTitleSections
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
    :class="{
      active,
      remembered,
      hidden: !presentation.visible,
      'hide-reference': sections?.reference === false,
      'hide-scale': sections?.scale === false,
      'hide-state': sections?.state === false,
      'hide-format': sections?.format === false,
    }"
    :style="{ '--trace-color': color }"
  >
    <button
      class="trace-select"
      type="button"
      :disabled="!canEditDisplay"
      :aria-pressed="active || remembered === true"
      :title="
        isCircularFormat(presentation.format)
          ? `${measurement.name} · ${measurement.parameter} · ${circularDescription(presentation, measurement)} · ${measurementState(measurement)} · 当前外圈幅度 ${smithRadius} · 本迹线保存值 ${presentation.smithRadius}`
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
      <span class="trace-state"
        >{{ measurement.kind === 'receiver' ? 'RAW' : measurement.calibration === 'calibrated' ? 'Cal' : 'Cal Off' }}</span
      >
    </button>
  </div>
</template>

<style scoped>
.trace-strip {
  display: flex;
  width: 100%;
  max-width: 100%;
  min-height: var(--diagram-label-line-height, 18px);
  color: var(--trace-color);
  background: transparent;
}

.trace-strip.remembered {
  color: #e4edf2;
  background: #394c59;
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
  width: 100%;
  min-width: 0;
  max-width: 100%;
  gap: 0;
  padding: 0;
  border: 0;
  color: inherit;
  background: transparent;
  text-align: left;
  font:
    var(--diagram-label-font-size, 11px) /
      var(--diagram-label-line-height, 18px) Tahoma,
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
  width: var(--trace-name-width, auto);
}

.measurement-chip {
  width: var(--trace-chip-width, auto);
  color: #111518;
  background: var(--trace-color);
}

.trace-format {
  width: var(--trace-format-width, auto);
  padding: 0 4px;
  box-sizing: border-box;
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
.trace-reference,
.trace-state {
  flex-shrink: 0;
}

.hide-reference .trace-reference,
.hide-scale .trace-scale,
.hide-state .trace-state,
.hide-format .trace-format {
  display: none;
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
