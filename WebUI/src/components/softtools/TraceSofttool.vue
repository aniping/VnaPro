<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type { AcquiredFrame, Measurement } from '../../api/types'
import { diagramTraceColor, type DisplayFormat, type DisplayScale, type LayoutMode, type TracePresentation } from '../../domain/presentation'
import { displayUnit, frequencyText, getMarkerReadout, getTraceScale, numberText } from '../../plot'
import FrequencyField from '../shared/FrequencyField.vue'

const props = defineProps<{
  mode: 'format' | 'scale' | 'marker' | 'trace-config'
  measurement: Measurement | undefined
  presentation: TracePresentation | undefined
  frame: AcquiredFrame | null
  index: number
  layout: LayoutMode
}>()

const emit = defineEmits<{
  format: [format: DisplayFormat]
  scale: [range: DisplayScale | null]
  marker: [update: { enabled?: boolean; frequencyHz?: number }]
  toggleVisibility: []
}>()

const formats: { id: DisplayFormat; label: string; description: string }[] = [
  { id: 'db', label: 'dB Mag', description: '20 log₁₀ |z| · dB re 1 a.u.' },
  { id: 'magnitude', label: 'Lin Mag', description: '|z| · a.u.' },
  { id: 'phase', label: 'Phase', description: 'arg(z) · deg' },
  { id: 'real', label: 'Real', description: 'Re(z) · a.u.' },
  { id: 'imag', label: 'Imag', description: 'Im(z) · a.u.' },
  { id: 'realimag', label: 'Real / Imag', description: 'Re(z) + Im(z) · a.u.' },
]

const trace = computed(() => props.measurement && props.presentation)
const color = computed(() => diagramTraceColor(props.index, props.layout))
const unit = computed(() => displayUnit(props.presentation?.format ?? 'db'))
const range = computed(() => getTraceScale(props.frame, props.measurement, props.presentation))
const marker = computed(() => getMarkerReadout(props.frame, props.measurement, props.presentation))
const perDivision = ref(10)
const reference = ref(0)
const position = ref(8)
const scaleError = ref('')
const markerFrequency = ref(0)
const markerError = ref('')

watch([() => props.measurement?.name, () => props.presentation?.format, () => props.presentation?.scale], () => {
  const { min, max } = range.value
  perDivision.value = Number(((max - min) / 10).toPrecision(8))
  position.value = props.presentation?.scale?.referencePosition ?? 8
  reference.value = Number((min + position.value * perDivision.value).toPrecision(10))
  scaleError.value = ''
}, { immediate: true })

watch([() => props.measurement?.name, () => props.presentation?.marker.frequencyHz,
  () => props.presentation?.marker.frequencyHz === null ? marker.value?.frequencyHz : undefined], () => {
  markerFrequency.value = props.presentation?.marker.frequencyHz ?? marker.value?.frequencyHz
    ?? props.frame?.frequencyHz[Math.floor((props.frame.frequencyHz.length - 1) / 2)] ?? 0
  markerError.value = ''
}, { immediate: true })

function applyScale() {
  if (!trace.value) return
  const perDiv = Number(perDivision.value)
  const ref = Number(reference.value)
  const pos = Number(position.value)
  const min = ref - pos * perDiv
  const max = ref + (10 - pos) * perDiv
  if (!Number.isFinite(perDiv) || perDiv <= 0 || !Number.isFinite(ref)
      || !Number.isFinite(pos) || pos < 0 || pos > 10 || !Number.isFinite(min)
      || !Number.isFinite(max) || max <= min) {
    scaleError.value = '请输入有效刻度：Per Div > 0，Position 在 0–10 之间。'
    return
  }
  scaleError.value = ''
  emit('scale', { min, max, referencePosition: pos })
}

function applyMarker() {
  if (!trace.value || !props.presentation?.marker.enabled) return
  if (!Number.isFinite(markerFrequency.value) || markerFrequency.value < 0) {
    markerError.value = '请输入有效的非负频率。'
    return
  }
  markerError.value = ''
  emit('marker', { frequencyHz: markerFrequency.value })
}
</script>

<template>
  <section class="softtool-section" :aria-label="mode">
    <div class="softtool-readout" :style="{ borderLeft: `3px solid ${color}`, paddingLeft: '7px' }">
      <strong>{{ measurement?.name ?? 'No active trace' }}</strong>
      <span>{{ measurement?.parameter ?? '请选择接收波测量' }}</span>
    </div>

    <template v-if="mode === 'format'">
      <h3 class="softtool-section-title">Trace Format</h3>
      <div class="softtool-grid" aria-label="Receiver wave display format">
        <button
          v-for="format in formats" :key="format.id" type="button"
          class="softtool-choice" :class="{ 'is-active': presentation?.format === format.id }"
          :disabled="!trace" :aria-pressed="presentation?.format === format.id"
          :title="format.description" :data-format="format.id"
          @click="emit('format', format.id)"
        >{{ format.label }}</button>
      </div>
      <div class="softtool-divider" aria-hidden="true" />
      <h3 class="softtool-section-title">Complex Formats</h3>
      <div class="softtool-grid">
        <button class="softtool-choice" type="button" disabled title="尚未实现归一化反射系数">Smith</button>
        <button class="softtool-choice" type="button" disabled title="尚未实现归一化网络参数">Polar</button>
      </div>
      <p class="softtool-note">仅转换当前接收波的显示格式。幅度未经功率或电压标定；dB 参考 1 a.u.。</p>
    </template>

    <template v-else-if="mode === 'scale'">
      <h3 class="softtool-section-title">Scale Values</h3>
      <form @submit.prevent="applyScale">
        <label class="softtool-label" for="trace-scale-division">Per Div</label>
        <div class="field-row trace-scale-field">
          <input id="trace-scale-division" v-model.number="perDivision" class="field-input" type="number" step="any" :disabled="!trace" required data-testid="scale-per-division" />
          <span>{{ unit }}</span>
        </div>
        <label class="softtool-label" for="trace-scale-reference">Reference Value</label>
        <div class="field-row trace-scale-field">
          <input id="trace-scale-reference" v-model.number="reference" class="field-input" type="number" step="any" :disabled="!trace" required data-testid="scale-reference" />
          <span>{{ unit }}</span>
        </div>
        <label class="softtool-label" for="trace-scale-position">Reference Position</label>
        <div class="field-row trace-scale-field">
          <input id="trace-scale-position" v-model.number="position" class="field-input" type="number" min="0" max="10" step="1" :disabled="!trace" title="0 = 下边缘，10 = 上边缘" data-testid="scale-position" />
          <span>div</span>
        </div>
        <button class="softtool-button softtool-primary softtool-apply" type="submit" :disabled="!trace" data-testid="apply-scale">Apply Scale</button>
      </form>
      <p v-if="scaleError" class="softtool-note" role="alert">{{ scaleError }}</p>
      <button class="softtool-button" type="button" :disabled="!trace" data-testid="autoscale-active" @click="emit('scale', null)">Autoscale · Active Trace</button>
      <div class="softtool-readout"><span>{{ presentation?.scale ? 'Manual' : 'Auto' }}</span><span>{{ numberText(range.min) }} … {{ numberText(range.max) }}</span></div>
      <p class="softtool-note">仅改变当前迹线的显示刻度。Autoscale 按实际数据缩放，不修改仪器配置。</p>
    </template>

    <template v-else-if="mode === 'marker'">
      <h3 class="softtool-section-title">Marker</h3>
      <div class="softtool-grid" aria-label="Marker selection">
        <button class="softtool-choice is-active" type="button" :disabled="!trace" title="Marker 1" @click="emit('marker', { enabled: true })">M1</button>
        <button v-for="number in [2, 3, 4]" :key="number" class="softtool-choice" type="button" disabled title="本期仅实现 M1">M{{ number }}</button>
      </div>
      <button class="softtool-button" type="button" :disabled="!trace" :aria-pressed="presentation?.marker.enabled" data-testid="marker-enabled" @click="emit('marker', { enabled: !presentation?.marker.enabled })">Marker State <strong>{{ presentation?.marker.enabled ? 'On' : 'Off' }}</strong></button>
      <form @submit.prevent="applyMarker">
        <FrequencyField
          id="marker-frequency" :model-value="markerFrequency" label="Stimulus"
          :disabled="!trace || !presentation?.marker.enabled"
          @update:model-value="markerFrequency = $event"
        />
        <button class="softtool-button softtool-primary softtool-apply" type="submit" :disabled="!trace || !presentation?.marker.enabled" data-testid="apply-marker">Set Marker</button>
      </form>
      <p v-if="markerError" class="softtool-note" role="alert">{{ markerError }}</p>
      <div class="softtool-divider" aria-hidden="true" />
      <h3 class="softtool-section-title">Actual Sample</h3>
      <div class="softtool-readout" data-testid="marker-value" :style="{ color }">
        <template v-if="marker">
          <span>{{ frequencyText(marker.frequencyHz, 9) }}</span>
          <strong :style="{ color }">{{ marker.valueText }}</strong>
          <span>Re {{ marker.real === null ? '—' : numberText(marker.real, 8) }} a.u.</span>
          <span>Im {{ marker.imag === null ? '—' : numberText(marker.imag, 8) }} a.u.</span>
          <span>Sample {{ marker.index + 1 }}</span>
        </template>
        <span v-else>{{ presentation?.marker.enabled ? 'Waiting for data' : 'Marker off' }}</span>
      </div>
      <p class="softtool-note">标记对齐真实频点；可拖动图上 M1，或使用方向键移动。抽样帧不插值补造数据。</p>
    </template>

    <template v-else>
      <h3 class="softtool-section-title">Trace Configuration</h3>
      <div class="softtool-readout">
        <span>Trace Name</span><strong>{{ measurement?.name ?? '—' }}</strong>
      </div>
      <div class="softtool-readout"><span>Receiver Wave</span><strong>{{ measurement ? `${measurement.receiverKind}${measurement.receiverPort}` : '—' }}</strong></div>
      <div class="softtool-readout"><span>Source Port</span><strong>{{ measurement?.sourcePort ?? '—' }}</strong></div>
      <button class="softtool-button" type="button" :disabled="!trace" :aria-pressed="presentation?.visible" data-testid="trace-visibility" @click="emit('toggleVisibility')">Trace Display <strong>{{ presentation?.visible ? 'On' : 'Off' }}</strong></button>
      <p class="softtool-note">显示开关仅影响当前迹线。测量定义与删除在 Meas 中操作。</p>
    </template>
  </section>
</template>

<style scoped>
.trace-scale-field { display: flex; align-items: center; gap: 7px; margin-bottom: 10px; }
.trace-scale-field .field-input { min-width: 0; }
.trace-scale-field > span { flex: 0 0 58px; color: #b8cad4; font-size: 10px; }
form .softtool-apply { width: 100%; }
</style>
