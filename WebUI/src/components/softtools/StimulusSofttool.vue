<script setup lang="ts">
import { computed, nextTick, reactive, ref, watch } from 'vue'
import type { InstrumentStatus, SweepConfiguration } from '../../api/types'
import { sweepWithinCapabilities } from '../../domain/sweep'
import { frequencyText } from '../../plot/formatting'
import FrequencyField from '../shared/FrequencyField.vue'

const props = defineProps<{
  status: InstrumentStatus | null
  canConfigure: boolean
  busy: boolean
  focusField?: 'start' | 'stop' | 'center' | 'span' | 'bandwidth'
  apply: (candidate: SweepConfiguration, expectedRevision: string) => Promise<InstrumentStatus | null>
}>()

const draft = reactive<SweepConfiguration>({ startHz: 0, stopHz: 0, points: 0, ifBandwidthHz: 0 })
const dirty = ref(false)
const applying = ref(false)
const editRevision = ref('')
const validation = ref('')
let baseline: SweepConfiguration | null = null

function configuration(status: InstrumentStatus): SweepConfiguration {
  const { startHz, stopHz, points, ifBandwidthHz } = status.config
  return { startHz, stopHz, points, ifBandwidthHz }
}

function signature(value: SweepConfiguration): string {
  return JSON.stringify([value.startHz, value.stopHz, value.points, value.ifBandwidthHz])
}

function load(status: InstrumentStatus) {
  baseline = configuration(status)
  Object.assign(draft, baseline)
  editRevision.value = status.revision
  dirty.value = false
  validation.value = ''
}

watch(() => props.status, (status) => {
  if (status && !dirty.value && !applying.value) load(status)
}, { immediate: true })

watch(() => props.focusField, async (field) => {
  if (!field) return
  await nextTick()
  const input = document.getElementById(`stimulus-${field}`)
  if (input instanceof HTMLInputElement) { input.focus(); input.select() }
  else input?.focus()
}, { immediate: true })

function update() {
  dirty.value = baseline === null || signature(draft) !== signature(baseline)
  validation.value = ''
}

const centerHz = computed(() => (draft.startHz + draft.stopHz) / 2)
const spanHz = computed(() => draft.stopHz - draft.startHz)
const revisionChanged = computed(() => dirty.value && props.status?.revision !== editRevision.value)
const locked = computed(() => props.busy || applying.value)

function setCenter(value: number) {
  const halfSpan = spanHz.value / 2
  draft.startHz = value - halfSpan
  draft.stopHz = value + halfSpan
  update()
}

function setSpan(value: number) {
  const center = centerHz.value
  draft.startHz = center - value / 2
  draft.stopHz = center + value / 2
  update()
}

function discard() {
  if (props.status && !locked.value) load(props.status)
}

async function commit() {
  if (!dirty.value || !props.canConfigure || locked.value || !props.status) return
  const candidate = { ...draft }
  const expectedRevision = editRevision.value
  const limits = props.status.capabilities
  if (!sweepWithinCapabilities(candidate, limits)) {
    validation.value = `Start ≤ Stop · ${frequencyText(limits.minFrequencyHz)}–${frequencyText(limits.maxFrequencyHz)} · ${limits.minSweepPoints}–${limits.maxSweepPoints} points · Span 0 = CW`
    return
  }
  const submitted = signature(candidate)
  applying.value = true
  validation.value = ''
  try {
    const result = await props.apply(candidate, expectedRevision)
    if (result) {
      if (signature(draft) === submitted) load(result)
      else {
        baseline = configuration(result)
        editRevision.value = result.revision
        dirty.value = signature(draft) !== signature(baseline)
      }
    }
  } finally { applying.value = false }
}

function applyFromKeyboard(event: KeyboardEvent) {
  if (event.target instanceof HTMLInputElement) {
    event.preventDefault()
    void commit()
  }
}
</script>

<template>
  <section class="softtool-section" aria-label="Stimulus settings" @keydown.enter="applyFromKeyboard">
    <template v-if="status">
      <h3 class="softtool-section-title">Frequency</h3>
      <FrequencyField id="stimulus-start" label="Start" :model-value="draft.startHz"
        @update:model-value="draft.startHz = $event; update()" />
      <FrequencyField id="stimulus-stop" label="Stop" :model-value="draft.stopHz"
        @update:model-value="draft.stopHz = $event; update()" />
      <div class="softtool-divider" />
      <FrequencyField id="stimulus-center" label="Center" :model-value="centerHz" @update:model-value="setCenter" />
      <FrequencyField id="stimulus-span" label="Span" :model-value="spanHz" @update:model-value="setSpan" />
      <div class="softtool-divider" />
      <h3 class="softtool-section-title">Sweep settings</h3>
      <label class="field-row" for="stimulus-points">
        <span class="softtool-label">Points</span>
        <input id="stimulus-points" v-model.number="draft.points" data-testid="points" class="field-input"
          type="number" :min="status.capabilities.minSweepPoints" :max="status.capabilities.maxSweepPoints" step="1" @input="update" />
      </label>
      <label class="field-row" for="stimulus-bandwidth">
        <span class="softtool-label">IF bandwidth</span>
        <select id="stimulus-bandwidth" v-model.number="draft.ifBandwidthHz" class="field-input" @change="update">
          <option :value="10">10 Hz</option><option :value="100">100 Hz</option>
          <option :value="1000">1 kHz</option><option :value="10000">10 kHz</option>
          <option :value="100000">100 kHz</option>
        </select>
      </label>
      <div class="softtool-apply">
        <button class="softtool-button softtool-primary" data-testid="apply-config" type="button"
          :disabled="!dirty || !canConfigure || locked" @click="commit">{{ applying ? 'Applying…' : 'Apply' }}</button>
        <button class="softtool-button" type="button" :disabled="!dirty || locked" @click="discard">Reload</button>
      </div>
      <p v-if="validation" class="softtool-note" role="status">{{ validation }}</p>
      <p v-else-if="revisionChanged" class="softtool-note" role="status">Device configuration changed. Reload to edit current settings.</p>
      <p v-else-if="dirty" class="softtool-note">Draft · revision {{ editRevision }}</p>
      <p v-else class="softtool-note">Applied · revision {{ status.revision }}</p>
      <p v-if="!canConfigure && !busy" class="softtool-note">Hold the sweep and acquire control to apply.</p>
      <p class="softtool-note">{{ status.capabilities.portCount }} ports · {{ frequencyText(status.capabilities.minFrequencyHz) }}–{{ frequencyText(status.capabilities.maxFrequencyHz) }}</p>
    </template>
    <p v-else class="softtool-note">Waiting for instrument settings…</p>
  </section>
</template>
