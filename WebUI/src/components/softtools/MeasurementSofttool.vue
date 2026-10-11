<script setup lang="ts">
import { computed, reactive, ref } from 'vue'
import type { InstrumentStatus, MeasurementCandidate } from '../../api/types'

const props = defineProps<{
  status: InstrumentStatus | null
  canConfigure: boolean
  busy: boolean
  activeTraceName: string
  add: (candidate: MeasurementCandidate, expectedRevision: string) => Promise<InstrumentStatus | null>
  remove: (name: string, expectedRevision: string) => Promise<InstrumentStatus | null>
}>()
const emit = defineEmits<{ selectTrace: [name: string] }>()
const draft = reactive<MeasurementCandidate>({ name: '', receiverKind: 'b', receiverPort: 1, sourcePort: 1 })
const working = ref(false)
const validation = ref('')
const catalog = computed(() => props.status?.measurements ?? [])
const active = computed(() => catalog.value.find((measurement) => measurement.name === props.activeTraceName))
const disabled = computed(() => !props.canConfigure || props.busy || working.value)

async function create() {
  if (disabled.value || !props.status) return
  const name = draft.name.trim()
  if (!name || name.length > 128 || /[\x00-\x1f/\\]/.test(name) || name === '.' || name === '..') {
    validation.value = 'Enter a receiver name (1–128 characters).'
    return
  }
  if (catalog.value.some((measurement) => measurement.name === name)) {
    validation.value = 'This name already exists.'
    return
  }
  const expectedRevision = props.status.revision
  const candidate = { ...draft, name }
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
  <section class="softtool-section" aria-label="Receiver measurements">
    <h3 class="softtool-section-title">Receiver traces</h3>
    <div class="softtool-grid" role="group" aria-label="Select active receiver">
      <button v-for="(measurement, index) in catalog" :key="measurement.name" class="softtool-button"
        :class="{ 'is-active': measurement.name === activeTraceName }" type="button"
        :disabled="index > 3" :title="index > 3 ? 'Only the first four receivers have display frames' : measurement.parameter"
        :aria-pressed="measurement.name === activeTraceName" @click="emit('selectTrace', measurement.name)">
        <span>{{ measurement.name }}</span>
        <small>{{ measurement.parameter }}{{ index > 3 ? ' · catalog' : '' }}</small>
      </button>
    </div>
    <p v-if="!catalog.length" class="softtool-note">No receiver measurements.</p>
    <p v-if="catalog.length > 4" class="softtool-note">The first four receivers are displayed.</p>
    <div class="softtool-divider" />
    <h3 class="softtool-section-title">Add receiver</h3>
    <label class="field-row" for="receiver-name">
      <span class="softtool-label">Name</span>
      <input id="receiver-name" v-model="draft.name" class="field-input" type="text" maxlength="128"
        placeholder="Trc2" autocomplete="off" @input="validation = ''" @keydown.enter="create" />
    </label>
    <div class="field-row">
      <span class="softtool-label">Wave</span>
      <div class="softtool-grid" role="group" aria-label="Receiver wave">
        <button v-for="kind in (['a', 'b'] as const)" :key="kind" class="softtool-choice" type="button"
          :class="{ 'is-active': draft.receiverKind === kind }" :aria-pressed="draft.receiverKind === kind"
          @click="draft.receiverKind = kind">{{ kind.toUpperCase() }}</button>
      </div>
    </div>
    <label class="field-row" for="receiver-port">
      <span class="softtool-label">Receiver port</span>
      <select id="receiver-port" v-model.number="draft.receiverPort" class="field-input">
        <option v-for="port in 4" :key="port" :value="port">Port {{ port }}</option>
      </select>
    </label>
    <label class="field-row" for="source-port">
      <span class="softtool-label">Source port</span>
      <select id="source-port" v-model.number="draft.sourcePort" class="field-input">
        <option v-for="port in 4" :key="port" :value="port">Port {{ port }}</option>
      </select>
    </label>
    <div class="softtool-apply">
      <button class="softtool-button softtool-primary" type="button" :disabled="disabled || catalog.length >= 32" @click="create">Add trace</button>
      <button class="softtool-button" type="button" :disabled="disabled || !active || catalog.length <= 1" @click="removeActive">Delete active</button>
    </div>
    <p v-if="validation" class="softtool-note" role="status">{{ validation }}</p>
    <p class="softtool-note">Raw A/B receiver waves · source-referenced ports</p>
  </section>
</template>
