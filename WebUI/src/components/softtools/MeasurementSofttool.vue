<script setup lang="ts">
import { computed, reactive, ref, watch } from 'vue'
import type { InstrumentStatus, MeasurementCandidate } from '../../api/types'
import { measurementCandidate, measurementState, type MeasurementChoice } from '../../domain/measurements'

const props = defineProps<{
  status: InstrumentStatus | null
  canConfigure: boolean
  busy: boolean
  activeTraceName: string
  displayedNames: string[]
  add: (candidate: MeasurementCandidate, expectedRevision: string) => Promise<InstrumentStatus | null>
  remove: (name: string, expectedRevision: string) => Promise<InstrumentStatus | null>
}>()
const emit = defineEmits<{
  selectTrace: [name: string]
  display: [value: { name: string; enabled: boolean }]
}>()
const draft = reactive<MeasurementCandidate>({ name: '', kind: 'receiver', receiverKind: 'b', receiverPort: 1, sourcePort: 1 })
const choice = ref<MeasurementChoice>('S11')
const working = ref(false)
const validation = ref('')
const catalog = computed(() => props.status?.measurements ?? [])
const active = computed(() => catalog.value.find((measurement) => measurement.name === props.activeTraceName))
const disabled = computed(() => !props.canConfigure || props.busy || working.value)
const portCount = computed(() => props.status?.capabilities.portCount ?? 0)
const choices = computed<MeasurementChoice[]>(() => portCount.value >= 2
  ? ['S11', 'S21', 'S12', 'S22', 'RAW'] : ['S11', 'RAW'])
watch(portCount, count => {
  if (draft.receiverPort > count) draft.receiverPort = 1
  if (draft.sourcePort > count) draft.sourcePort = 1
  if (!choices.value.includes(choice.value)) choice.value = 'S11'
})

async function create() {
  if (disabled.value || !props.status) return
  const name = draft.name.trim()
  if (!name || name.length > 128 || /[\x00-\x1f/\\]/.test(name) || name === '.' || name === '..') {
    validation.value = 'Enter a measurement name (1–128 characters).'
    return
  }
  if (catalog.value.some((measurement) => measurement.name === name)) {
    validation.value = 'This name already exists.'
    return
  }
  const expectedRevision = props.status.revision
  const candidate = measurementCandidate(choice.value, { ...draft, name })
  working.value = true
  validation.value = ''
  try {
    const result = await props.add(candidate, expectedRevision)
    if (result) {
      if (draft.name.trim() === name) draft.name = ''
      emit('selectTrace', name)
    }
  } finally { working.value = false }
}

async function removeActive() {
  if (disabled.value || !active.value || !props.status || catalog.value.length <= 1) return
  const name = active.value.name
  const expectedRevision = props.status.revision
  working.value = true
  try {
    const result = await props.remove(name, expectedRevision)
    if (result) emit('selectTrace', result.measurements[0]?.name ?? '')
  } finally { working.value = false }
}
</script>

<template>
  <section class="softtool-section" aria-label="Measurements">
    <h3 class="softtool-section-title">Measurement traces</h3>
    <div class="softtool-grid" role="group" aria-label="Select active measurement">
      <div v-for="measurement in catalog" :key="measurement.name" class="measurement-entry">
        <button class="softtool-button"
          :class="{ 'is-active': measurement.name === activeTraceName }" type="button"
          :title="`${measurement.parameter} · ${measurementState(measurement)}`"
          :aria-pressed="measurement.name === activeTraceName" @click="emit('selectTrace', measurement.name)">
          <span>{{ measurement.name }}</span>
          <small>{{ measurement.parameter }} · {{ measurementState(measurement) }}</small>
        </button>
        <label class="measurement-display">
          <input type="checkbox" :checked="displayedNames.includes(measurement.name)"
            :disabled="!displayedNames.includes(measurement.name) && displayedNames.length >= 4"
            :aria-label="`Display ${measurement.name}`"
            @change="emit('display', { name: measurement.name, enabled: !displayedNames.includes(measurement.name) })" />
          Display
        </label>
      </div>
    </div>
    <p v-if="!catalog.length" class="softtool-note">No measurements.</p>
    <p class="softtool-note">显示 {{ displayedNames.length }} / 4 条。选择目录迹线会加入显示；满四条时替换当前显示槽位，保留测量定义。</p>
    <div class="softtool-divider" />
    <h3 class="softtool-section-title">Add measurement</h3>
    <div class="softtool-grid" role="group" aria-label="Measurement type">
      <button v-for="parameter in choices" :key="parameter"
        class="softtool-choice" type="button" :class="{ 'is-active': choice === parameter }"
        :aria-pressed="choice === parameter" :disabled="disabled" @click="choice = parameter">
        {{ parameter }}
      </button>
    </div>
    <label class="field-row" for="receiver-name">
      <span class="softtool-label">Name</span>
      <input id="receiver-name" v-model="draft.name" class="field-input" type="text" maxlength="128"
        :placeholder="choice === 'RAW' ? 'Trc2' : choice" :disabled="disabled"
        autocomplete="off" @input="validation = ''" @keydown.enter="create" />
    </label>
    <template v-if="choice === 'RAW'">
      <div class="field-row">
        <span class="softtool-label">Wave</span>
        <div class="softtool-grid" role="group" aria-label="Receiver wave">
          <button v-for="kind in (['a', 'b'] as const)" :key="kind" class="softtool-choice" type="button"
            :class="{ 'is-active': draft.receiverKind === kind }" :aria-pressed="draft.receiverKind === kind"
            :disabled="disabled"
            @click="draft.receiverKind = kind">{{ kind.toUpperCase() }}</button>
        </div>
      </div>
      <label class="field-row" for="receiver-port">
        <span class="softtool-label">Receiver port</span>
        <select id="receiver-port" v-model.number="draft.receiverPort" class="field-input" :disabled="disabled">
          <option v-for="port in portCount" :key="port" :value="port">Port {{ port }}</option>
        </select>
      </label>
      <label class="field-row" for="source-port">
        <span class="softtool-label">Source port</span>
        <select id="source-port" v-model.number="draft.sourcePort" class="field-input" :disabled="disabled">
          <option v-for="port in portCount" :key="port" :value="port">Port {{ port }}</option>
        </select>
      </label>
    </template>
    <p v-else class="softtool-note">{{ choice }} = b{{ choice[1] }}/a{{ choice[2] }} · 激励端口 {{ choice[2] }} · 未校准</p>
    <div class="softtool-apply">
      <button class="softtool-button softtool-primary" type="button" :disabled="disabled || catalog.length >= 32" @click="create">Add trace</button>
      <button class="softtool-button" type="button" :disabled="disabled || !active || catalog.length <= 1" @click="removeActive">Delete active</button>
    </div>
    <p v-if="validation" class="softtool-note" role="status">{{ validation }}</p>
    <p class="softtool-note">HOLD 下添加测量，再执行扫描。S 参数为同帧波量比值、无量纲；未应用误差校准。</p>
  </section>
</template>

<style scoped>
.measurement-entry { display: grid; gap: 3px; }
.measurement-display { display: flex; align-items: center; gap: 4px; color: #afc0c9; font-size: 11px; }
</style>
