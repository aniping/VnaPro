<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import type { CalibrationPreview } from '../../api/calibrationTypes'
import type { SweepConfiguration } from '../../api/types'
import { paintDiagram, plotRect } from '../../plot/canvasRenderer'
import { frequencyText } from '../../plot/formatting'

const props = defineProps<{
  format: 'db' | 'phase'
  preview: CalibrationPreview | null
  parameter: string
  config: SweepConfiguration
  calibrationLabel: string
  emptyText: string
}>()
const area = ref<HTMLElement | null>(null)
const canvas = ref<HTMLCanvasElement | null>(null)
const scale = computed(() =>
  props.format === 'db'
    ? { min: -90, max: 10, referencePosition: 9 }
    : { min: -225, max: 225, referencePosition: 5 },
)
const ticks = computed(() =>
  Array.from({ length: 11 }, (_, index) => ({
    label:
      props.format === 'db' ? `${10 - index * 10}dB` : `${225 - index * 45}°`,
    reference: props.format === 'db' ? index === 1 : index === 5,
  })),
)
const startHz = computed(
  () => props.preview?.frequencyHz[0] ?? props.config.startHz,
)
const stopHz = computed(
  () => props.preview?.frequencyHz.at(-1) ?? props.config.stopHz,
)
const noDrawableMessage = computed(() => {
  const preview = props.preview
  if (!preview) return ''
  const states = props.format === 'db' ? preview.dbStates : preview.phaseStates
  if (states.some((state, index) => preview.valid[index] && state === 'finite'))
    return ''
  if (!preview.valid.some(Boolean)) return '标准没有有效样本。'
  if (props.format === 'db' && states.includes('negativeInfinity'))
    return '零幅值：dB = −∞，超出当前刻度。'
  return props.format === 'phase'
    ? '标准相位未定义。'
    : '标准没有可绘制的有限值。'
})
let observer: ResizeObserver | undefined
let paintRequest = 0
function paint() {
  if (!area.value || !canvas.value) return
  const width = area.value.clientWidth
  const height = area.value.clientHeight
  if (width <= 0 || height <= 0) return
  const preview = props.preview
  const values = preview
    ? props.format === 'db'
      ? preview.dbValues
      : preview.phaseValues
    : []
  const states = preview
    ? props.format === 'db'
      ? preview.dbStates
      : preview.phaseStates
    : []
  paintDiagram(
    canvas.value,
    {
      width,
      height,
      rect: plotRect(width, height),
      xMin: startHz.value,
      xMax: stopHz.value,
      references: [
        {
          scale: scale.value,
          format: props.format,
          color: '#60e600',
          active: true,
        },
      ],
      curves: preview
        ? [
            {
              frequencyHz: preview.frequencyHz,
              values: values.map((value, index) =>
                preview.valid[index] &&
                states[index] === 'finite' &&
                value !== null
                  ? value
                  : NaN,
              ),
              scale: scale.value,
              format: props.format,
              color: '#60e600',
              dashed: false,
              active: true,
            },
          ]
        : [],
    },
    area.value.getBoundingClientRect(),
  )
}
function schedulePaint() {
  cancelAnimationFrame(paintRequest)
  paintRequest = requestAnimationFrame(paint)
}
onMounted(() => {
  observer = new ResizeObserver(schedulePaint)
  if (area.value) observer.observe(area.value)
  window.addEventListener('resize', schedulePaint)
  schedulePaint()
})
onBeforeUnmount(() => {
  observer?.disconnect()
  window.removeEventListener('resize', schedulePaint)
  cancelAnimationFrame(paintRequest)
})
watch(
  () => [props.preview, props.format, props.config],
  () => void nextTick(schedulePaint),
)
</script>

<template>
  <section
    class="cal-preview-diagram"
    :aria-label="`${parameter} ${format === 'db' ? 'dB Mag' : 'Phase'} calibration standard preview`"
  >
    <div class="cal-preview-title">
      <span class="cal-preview-title-fields"
        ><span>Trc{{ format === 'db' ? '1' : '2' }}</span
        ><span class="cal-preview-parameter">{{ parameter }}</span
        ><span>{{
          format === 'db' ? 'dB Mag 10 dB/ Ref 0 dB' : 'Phase 45°/ Ref 0°'
        }}</span
        ><span>Cal Off</span></span
      ><span class="cal-preview-menu" aria-hidden="true" />
    </div>
    <div ref="area" class="cal-preview-plot">
      <canvas ref="canvas" aria-hidden="true" /><span
        v-for="(tick, index) in ticks"
        :key="tick.label"
        class="cal-preview-tick"
        :class="{ reference: tick.reference }"
        :style="{ top: `${index * 10}%` }"
        >{{ tick.label }}</span
      >
      <p v-if="!preview" class="cal-preview-empty">{{ emptyText }}</p>
      <p v-else-if="noDrawableMessage" class="cal-preview-empty">
        {{ noDrawableMessage }}
      </p>
      <span v-if="preview?.decimated" class="cal-preview-points"
        >{{ preview.displayPointCount }}/{{
          preview.originalPointCount
        }}
        pts</span
      >
    </div>
    <div class="cal-preview-channel">
      <span class="cal-preview-channel-name">Ch1</span
      ><span>Start&nbsp; {{ frequencyText(startHz) }}</span
      ><span
        >Bw&nbsp;
        {{
          frequencyText(preview?.ifBandwidthHz ?? config.ifBandwidthHz)
        }}</span
      ><span>{{ calibrationLabel }}</span
      ><span class="cal-preview-stop"
        >Stop&nbsp; {{ frequencyText(stopHz) }}</span
      >
    </div>
  </section>
</template>

<style scoped>
.cal-preview-diagram {
  display: grid;
  grid-template-rows: 20px minmax(0, 1fr) 21px;
  min-width: 0;
  min-height: 0;
  border: 1px solid #435862;
  background: #000;
  color: #e8eef2;
  font:
    11px Tahoma,
    Arial,
    sans-serif;
}
.cal-preview-title {
  display: flex;
  align-items: center;
  justify-content: space-between;
  min-width: 0;
  background: #101719;
  white-space: nowrap;
  overflow: hidden;
}
.cal-preview-title-fields {
  display: flex;
  align-items: center;
  height: 19px;
  min-width: 0;
  gap: 3px;
  padding-right: 4px;
  background: #0d75bf;
}
.cal-preview-title-fields > span:first-child {
  padding: 2px 3px;
}
.cal-preview-parameter {
  align-self: stretch;
  padding: 2px 3px;
  color: #000;
  background: #60e600;
}
.cal-preview-menu {
  width: 0;
  height: 0;
  flex-shrink: 0;
  margin: 0 12px;
  border-left: 7px solid transparent;
  border-right: 7px solid transparent;
  border-top: 7px solid #e2e8eb;
}
.cal-preview-plot {
  position: relative;
  min-height: 0;
  overflow: hidden;
}
.cal-preview-plot canvas {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  transform-origin: top left;
}
.cal-preview-tick {
  position: absolute;
  left: 3px;
  color: #65808f;
  line-height: 13px;
  transform: translateY(-50%);
  font-size: 11px;
}
.cal-preview-tick:first-of-type {
  transform: none;
}
.cal-preview-tick:last-of-type {
  transform: translateY(-100%);
}
.cal-preview-tick.reference {
  color: #dbe2e6;
}
.cal-preview-empty {
  position: absolute;
  top: 42%;
  left: 13%;
  right: 6%;
  margin: 0;
  padding: 6px;
  color: #aebfc9;
  background: #000;
  text-align: center;
  line-height: 1.5;
}
.cal-preview-points {
  position: absolute;
  right: 4px;
  bottom: 4px;
  color: #b7c5ce;
  font-size: 10px;
}
.cal-preview-channel {
  display: flex;
  align-items: center;
  gap: 5px;
  min-width: 0;
  overflow: hidden;
  border-top: 1px solid #536977;
  white-space: nowrap;
  font-size: 10px;
}
.cal-preview-channel-name {
  height: 19px;
  padding: 3px 2px;
  background: #0d75bf;
}
.cal-preview-stop {
  margin-left: auto;
}
</style>
