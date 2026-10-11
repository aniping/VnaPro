<script setup lang="ts">
import { computed, ref } from 'vue'
import type { InstrumentStatus, MeasurementCandidate } from '../../api/types'
import {
  measurementCandidate,
  type MeasurementChoice,
} from '../../domain/measurements'
import WaveQuantityDialog from './WaveQuantityDialog.vue'

const props = defineProps<{
  status: InstrumentStatus | null
  canConfigure: boolean
  busy: boolean
  activeTraceName: string
  mode: 'meas' | 'meas-wave'
  update: (
    candidate: MeasurementCandidate,
    expectedRevision: string,
  ) => Promise<InstrumentStatus | null>
}>()
const working = ref(false)
const validation = ref('')
const moreWave = ref(false)
const active = computed(() =>
  props.status?.measurements.find(
    (trace) => trace.name === props.activeTraceName,
  ),
)
const disabled = computed(
  () => !props.canConfigure || props.busy || working.value || !active.value,
)
const portCount = computed(() => props.status?.capabilities.portCount ?? 0)
const parameters: MeasurementChoice[] = ['S11', 'S12', 'S21', 'S22']
const waves = [
  { receiverKind: 'a', receiverPort: 1, sourcePort: 1 },
  { receiverKind: 'b', receiverPort: 1, sourcePort: 1 },
  { receiverKind: 'b', receiverPort: 2, sourcePort: 1 },
  { receiverKind: 'a', receiverPort: 2, sourcePort: 2 },
  { receiverKind: 'b', receiverPort: 2, sourcePort: 2 },
  { receiverKind: 'b', receiverPort: 1, sourcePort: 2 },
] as const

function supports(parameter: MeasurementChoice) {
  return (
    Number(parameter[1]) <= portCount.value &&
    Number(parameter[2]) <= portCount.value
  )
}

async function change(candidate: MeasurementCandidate) {
  if (disabled.value || !props.status || !active.value) return false
  if (
    candidate.kind === active.value.kind &&
    candidate.receiverKind === active.value.receiverKind &&
    candidate.receiverPort === active.value.receiverPort &&
    candidate.sourcePort === active.value.sourcePort
  )
    return true
  working.value = true
  validation.value = ''
  try {
    const result = await props.update(candidate, props.status.revision)
    if (!result) validation.value = '测量未更新，请检查状态栏中的原因。'
    return Boolean(result)
  } finally {
    working.value = false
  }
}

function selectParameter(parameter: MeasurementChoice) {
  if (!active.value || !supports(parameter)) return
  void change(measurementCandidate(parameter, active.value))
}

function selectParameterEvent(event: Event) {
  if (event.target instanceof HTMLSelectElement) {
    const value = event.target.value
    const parameter = parameters.find((item) => item === value)
    if (parameter) selectParameter(parameter)
  }
}

function selectWave(wave: (typeof waves)[number]) {
  if (active.value)
    void change({ name: active.value.name, kind: 'receiver', ...wave })
}

async function applyWave(candidate: MeasurementCandidate) {
  if (await change(candidate)) moreWave.value = false
}
</script>

<template>
  <section class="measurement-menu" aria-label="Active trace measurement">
    <template v-if="mode === 'meas'">
      <div class="measurement-current">
        <span class="measurement-family">S</span>
        <select
          aria-label="Active S parameter"
          :value="active?.kind === 'sParameter' ? active.parameter : ''"
          :disabled="disabled"
          @change="selectParameterEvent"
        >
          <option value="" disabled>—</option>
          <option
            v-for="parameter in parameters"
            :key="parameter"
            :value="parameter"
            :disabled="!supports(parameter)"
          >
            {{ parameter.slice(1) }}
          </option>
        </select>
      </div>
      <div class="measurement-matrix" role="group" aria-label="S-Parameters">
        <button
          v-for="parameter in parameters"
          :key="parameter"
          type="button"
          class="measurement-key"
          :aria-pressed="
            active?.kind === 'sParameter' && active.parameter === parameter
          "
          :disabled="disabled || !supports(parameter)"
          @click="selectParameter(parameter)"
        >
          {{ parameter }}
        </button>
      </div>
      <button
        class="measurement-key measurement-wide"
        type="button"
        disabled
        title="当前仅实现双端口四项S参数；完整多端口矩阵的整组创建尚未实现"
      >
        All S-Params
      </button>
    </template>
    <template v-else>
      <div
        class="measurement-wave-list"
        role="group"
        aria-label="Wave quantities"
      >
        <button
          v-for="wave in waves"
          :key="`${wave.receiverKind}${wave.receiverPort},${wave.sourcePort}`"
          class="measurement-key"
          type="button"
          :disabled="
            disabled ||
            wave.receiverPort > portCount ||
            wave.sourcePort > portCount
          "
          :aria-pressed="
            active?.kind === 'receiver' &&
            active.receiverKind === wave.receiverKind &&
            active.receiverPort === wave.receiverPort &&
            active.sourcePort === wave.sourcePort
          "
          @click="selectWave(wave)"
        >
          {{ wave.receiverKind }}{{ wave.receiverPort
          }}<small>Src Port {{ wave.sourcePort }}</small>
        </button>
      </div>
      <button
        class="measurement-key measurement-wide"
        type="button"
        :disabled="disabled"
        @click="moreWave = true"
      >
        More Wave Quantities <span aria-hidden="true">›</span>
      </button>
    </template>
    <p class="measurement-context">
      {{ active?.name ?? 'No active trace' }} · {{ active?.parameter ?? '—' }}
    </p>
    <p v-if="!canConfigure" class="softtool-note">
      {{ status?.control.readOnlyReason || 'Hold 后可修改活动迹线。' }}
    </p>
    <p v-if="validation" class="softtool-note" role="status">
      {{ validation }}
    </p>
    <WaveQuantityDialog
      v-if="moreWave && active"
      :measurement="active"
      :port-count="portCount"
      :disabled="disabled"
      :error="validation"
      @apply="applyWave"
      @close="moreWave = false"
    />
  </section>
</template>

<style scoped>
.measurement-menu {
  display: flex;
  flex-direction: column;
  gap: 5px;
  padding: 5px 4px;
}
.measurement-current {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 3px;
  height: 40px;
}
.measurement-family {
  display: grid;
  place-items: center;
  background: #303437;
  color: #e1e8ed;
  font-size: 15px;
}
.measurement-current select {
  min-width: 0;
  border: 0;
  padding: 0 8px;
  background: #293942;
  color: white;
  font-size: 15px;
  color-scheme: dark;
}
.measurement-matrix {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 5px;
}
.measurement-key {
  position: relative;
  min-height: 40px;
  padding: 8px 8px 8px 13px;
  border: 0;
  color: white;
  background: #4a616d;
  font-size: 13px;
  text-align: left;
}
.measurement-key::before {
  position: absolute;
  top: 7px;
  bottom: 7px;
  left: 4px;
  width: 3px;
  content: '';
  background: repeating-linear-gradient(
    to bottom,
    #c8d9e2 0 3px,
    transparent 3px 6px
  );
}
.measurement-key[aria-pressed='true'] {
  background: #0d75bf;
}
.measurement-key:hover:not(:disabled) {
  background: #587788;
}
.measurement-key:disabled {
  background: #2d3d47;
  color: #7e919d;
}
.measurement-wide {
  margin-top: 3px;
  padding-left: 8px;
}
.measurement-wide::before {
  display: none;
}
.measurement-wide span {
  float: right;
  font-size: 20px;
}
.measurement-wave-list {
  display: grid;
  gap: 4px;
}
.measurement-key small {
  display: block;
  margin-top: 2px;
  font-size: 12px;
}
.measurement-context {
  margin: 8px 4px 0;
  color: #a7b9c3;
  font-size: 11px;
}
</style>
