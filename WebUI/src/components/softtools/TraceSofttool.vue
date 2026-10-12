<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type { AcquiredFrame, Measurement } from '../../api/types'
import {
  diagramTraceColor,
  type DisplayFormat,
  type ScaleValues,
  type LayoutMode,
  type TracePresentation,
} from '../../domain/presentation'
import { displayUnit, getTraceScale, numberText } from '../../plot'
import { measurementState } from '../../domain/measurements'
import { MIN_SMITH_RADIUS, validSmithRadius } from '../../plot/smithMath'

const props = defineProps<{
  mode: 'format' | 'scale'
  measurement: Measurement | undefined
  presentation: TracePresentation | undefined
  frame: AcquiredFrame | null
  index: number
  layout: LayoutMode
  canAutoscale: boolean
  canEditDisplay: boolean
}>()

const emit = defineEmits<{
  format: [format: DisplayFormat]
  scale: [values: ScaleValues]
  autoscale: []
  smithRadius: [radius: number]
  toggleVisibility: []
}>()

const formats = computed<
  { id: DisplayFormat; label: string; description: string }[]
>(() => [
  {
    id: 'db',
    label: 'dB Mag',
    description: `20 log₁₀ |z| · ${displayUnit('db', props.measurement)}`,
  },
  {
    id: 'magnitude',
    label: 'Lin Mag',
    description: `|z| · ${props.measurement?.unit ?? 'a.u.'}`,
  },
  { id: 'phase', label: 'Phase', description: 'arg(z) · deg' },
  {
    id: 'real',
    label: 'Real',
    description: `Re(z) · ${props.measurement?.unit ?? 'a.u.'}`,
  },
  {
    id: 'imag',
    label: 'Imag',
    description: `Im(z) · ${props.measurement?.unit ?? 'a.u.'}`,
  },
  {
    id: 'realimag',
    label: 'Real / Imag',
    description: `Re(z) + Im(z) · ${props.measurement?.unit ?? 'a.u.'}`,
  },
])

const trace = computed(
  () => props.measurement && props.presentation && props.canEditDisplay,
)
const color = computed(() => diagramTraceColor(props.index, props.layout))
const unit = computed(() =>
  displayUnit(props.presentation?.format ?? 'db', props.measurement),
)
const range = computed(() =>
  getTraceScale(props.frame, props.measurement, props.presentation),
)
const perDivision = ref(10)
const reference = ref(0)
const position = ref(8)
const scaleError = ref('')
const smithRadius = ref(1)
watch(
  () => props.presentation?.smithRadius,
  (value) => {
    smithRadius.value = value ?? 1
  },
  { immediate: true },
)

watch(
  [
    () => props.measurement?.name,
    () => props.presentation?.format,
    () => props.presentation?.scale.min,
    () => props.presentation?.scale.max,
    () => props.presentation?.scale.referencePosition,
  ],
  () => {
    const { min, max } = range.value
    perDivision.value = Number(((max - min) / 10).toPrecision(8))
    position.value = props.presentation?.scale?.referencePosition ?? 8
    reference.value = Number(
      (min + position.value * perDivision.value).toPrecision(10),
    )
    scaleError.value = ''
  },
  { immediate: true },
)

function applyScale() {
  if (!trace.value) return
  if (props.presentation?.format === 'smith') {
    if (!validSmithRadius(smithRadius.value)) {
      scaleError.value = `请输入有效的外圈幅度：|Γ| ≥ ${MIN_SMITH_RADIUS}。`
      return
    }
    scaleError.value = ''
    emit('smithRadius', smithRadius.value)
    return
  }
  const perDiv = Number(perDivision.value)
  const ref = Number(reference.value)
  const pos = Number(position.value)
  if (
    !Number.isFinite(perDiv) ||
    perDiv <= 0 ||
    !Number.isFinite(ref) ||
    !Number.isFinite(pos) ||
    pos < 0 ||
    pos > 10
  ) {
    scaleError.value = '请输入有效刻度：Per Div > 0，Position 在 0–10 之间。'
    return
  }
  scaleError.value = ''
  emit('scale', {
    perDivision: perDiv,
    referenceLevel: ref,
    referencePosition: pos,
  })
}
</script>

<template>
  <section class="softtool-section" :aria-label="mode">
    <div
      class="softtool-readout"
      :style="{ borderLeft: `3px solid ${color}`, paddingLeft: '7px' }"
    >
      <strong>{{ measurement?.name ?? 'No active trace' }}</strong>
      <span
        >{{ measurement?.parameter ?? '请选择测量'
        }}{{ measurement ? ` · ${measurementState(measurement)}` : '' }}</span
      >
    </div>

    <template v-if="mode === 'format'">
      <h3 class="softtool-section-title">Trace Format</h3>
      <div class="softtool-grid" aria-label="Measurement display format">
        <button
          v-for="format in formats"
          :key="format.id"
          type="button"
          class="softtool-choice"
          :class="{ 'is-active': presentation?.format === format.id }"
          :disabled="
            !trace || !presentation?.allowedFormats.includes(format.id)
          "
          :aria-pressed="presentation?.format === format.id"
          :title="format.description"
          :data-format="format.id"
          @click="emit('format', format.id)"
        >
          {{ format.label }}
        </button>
      </div>
      <div class="softtool-divider" aria-hidden="true" />
      <h3 class="softtool-section-title">Complex Formats</h3>
      <div class="softtool-grid">
        <button
          class="softtool-choice"
          type="button"
          :disabled="!trace || !presentation?.allowedFormats.includes('smith')"
          :class="{ 'is-active': presentation?.format === 'smith' }"
          :aria-pressed="presentation?.format === 'smith'"
          title="仅反射 S11/S22：Γ 与归一化阻抗；未校准"
          data-format="smith"
          @click="emit('format', 'smith')"
        >
          Smith
        </button>
        <button
          class="softtool-choice"
          type="button"
          disabled
          title="尚未实现极坐标绘图"
        >
          Polar
        </button>
      </div>
      <p class="softtool-note">
        {{
          measurement?.kind === 'sParameter'
            ? '显示同帧波量的比值，幅度无量纲，dB = 20 log₁₀ |S|；未应用误差校准。'
            : '仅转换当前接收波的显示格式。幅度未经功率或电压标定；dB 参考 1 a.u.。'
        }}
      </p>
    </template>

    <template v-else-if="mode === 'scale'">
      <h3 class="softtool-section-title">Scale Values</h3>
      <form @submit.prevent="applyScale">
        <template v-if="presentation?.format === 'smith'">
          <label class="softtool-label" for="trace-smith-radius"
            >Outer Magnitude |Γ|</label
          >
          <input
            id="trace-smith-radius"
            v-model.number="smithRadius"
            class="field-input"
            type="number"
            :min="MIN_SMITH_RADIUS"
            step="any"
            :disabled="!trace"
            required
            data-testid="smith-radius"
          />
          <p class="softtool-note">
            圆心保持 Γ = 0；外圈幅度只改变显示，单位圆与阻抗网格按相同投影绘制。
          </p>
        </template>
        <template v-else>
          <label class="softtool-label" for="trace-scale-division"
            >Per Div</label
          >
          <div class="field-row trace-scale-field">
            <input
              id="trace-scale-division"
              v-model.number="perDivision"
              class="field-input"
              type="number"
              step="any"
              :disabled="!trace"
              required
              data-testid="scale-per-division"
            />
            <span>{{ unit }}</span>
          </div>
          <label class="softtool-label" for="trace-scale-reference"
            >Reference Value</label
          >
          <div class="field-row trace-scale-field">
            <input
              id="trace-scale-reference"
              v-model.number="reference"
              class="field-input"
              type="number"
              step="any"
              :disabled="!trace"
              required
              data-testid="scale-reference"
            />
            <span>{{ unit }}</span>
          </div>
          <label class="softtool-label" for="trace-scale-position"
            >Reference Position</label
          >
          <div class="field-row trace-scale-field">
            <input
              id="trace-scale-position"
              v-model.number="position"
              class="field-input"
              type="number"
              min="0"
              max="10"
              step="1"
              :disabled="!trace"
              title="0 = 下边缘，10 = 上边缘"
              data-testid="scale-position"
            />
            <span>div</span>
          </div>
        </template>
        <button
          class="softtool-button softtool-primary softtool-apply"
          type="submit"
          :disabled="!trace"
          data-testid="apply-scale"
        >
          Apply Scale
        </button>
      </form>
      <p v-if="scaleError" class="softtool-note" role="alert">
        {{ scaleError }}
      </p>
      <button
        class="softtool-button"
        type="button"
        :disabled="!canAutoscale"
        data-testid="autoscale-active"
        @click="emit('autoscale')"
      >
        Autoscale · Active Trace
      </button>
      <div class="softtool-readout">
        <span>{{
          presentation?.format === 'smith' ? 'Outer |Γ|' : 'Range'
        }}</span
        ><span>{{
          presentation?.format === 'smith'
            ? numberText(presentation.smithRadius)
            : `${numberText(range.min)} … ${numberText(range.max)}`
        }}</span>
      </div>
      <p class="softtool-note">
        Autoscale
        按当前显示数据适配一次并固定刻度；再次点击才重新适配。后端保存当前迹线的显示设置，所有观察窗口共享。
      </p>
    </template>
  </section>
</template>

<style scoped>
.trace-scale-field {
  display: flex;
  align-items: center;
  gap: 7px;
  margin-bottom: 10px;
}
.trace-scale-field .field-input {
  min-width: 0;
}
.trace-scale-field > span {
  flex: 0 0 58px;
  color: #b8cad4;
  font-size: 10px;
}
form .softtool-apply {
  width: 100%;
}
</style>
