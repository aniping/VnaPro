<script setup lang="ts">
import { computed, ref } from 'vue'
import type { InstrumentStatus, RunAction } from '../../api/types'

const props = defineProps<{
  status: InstrumentStatus | null
  mode: 'sweep-control' | 'trigger'
  canWrite: boolean
  busy: boolean
  action: (action: RunAction) => Promise<InstrumentStatus | null>
}>()
const working = ref(false)
const disabled = computed(() => !props.canWrite || props.busy || working.value)
const continuous = computed(() => props.status?.config.sweepMode === 'CONT')
const held = computed(() => props.status?.config.sweepMode === 'HOLD')
const manual = computed(() => props.status?.config.triggerSource === 'MAN')
const armed = computed(
  () => manual.value && !held.value && props.status?.state === 'Running',
)

async function run(action: RunAction) {
  if (disabled.value) return
  working.value = true
  try {
    await props.action(action)
  } finally {
    working.value = false
  }
}
</script>

<template>
  <section
    class="softtool-section"
    :aria-label="mode === 'trigger' ? 'Trigger controls' : 'Sweep controls'"
  >
    <template v-if="mode === 'sweep-control'">
      <h3 class="softtool-section-title">Sweep control</h3>
      <div class="softtool-grid">
        <button
          class="softtool-button softtool-primary"
          type="button"
          :disabled="disabled"
          @click="run('single')"
        >
          Single sweep
        </button>
        <button
          class="softtool-button"
          :class="{ 'is-active': continuous }"
          type="button"
          :disabled="disabled"
          :aria-pressed="continuous"
          @click="run('continuous')"
        >
          Continuous
        </button>
        <button
          class="softtool-button"
          :class="{ 'is-active': held }"
          type="button"
          :disabled="disabled"
          :aria-pressed="held"
          @click="run('hold')"
        >
          Hold
        </button>
      </div>
    </template>
    <template v-else>
      <h3 class="softtool-section-title">Trigger</h3>
      <div class="field-row">
        <span class="softtool-label">Source</span>
        <span class="softtool-readout">{{
          status?.config.triggerSource === 'MAN'
            ? 'Manual'
            : status
              ? 'Immediate'
              : '—'
        }}</span>
      </div>
      <button
        class="softtool-button"
        type="button"
        :disabled="disabled || !armed"
        @click="run('trigger')"
      >
        Manual trigger
      </button>
    </template>
    <div class="softtool-divider" />
    <div class="field-row">
      <span class="softtool-label">State</span
      ><span class="softtool-readout">{{ status?.state ?? '—' }}</span>
    </div>
    <div class="field-row">
      <span class="softtool-label">Mode</span
      ><span class="softtool-readout">{{
        status?.config.sweepMode ?? '—'
      }}</span>
    </div>
    <div class="field-row">
      <span class="softtool-label">Run</span
      ><span class="softtool-readout">{{ status?.runId ?? '—' }}</span>
    </div>
    <p v-if="busy || working" class="softtool-note" role="status">
      Waiting for accepted device work…
    </p>
    <p v-else-if="!canWrite" class="softtool-note">
      Acquire instrument control to run a sweep.
    </p>
  </section>
</template>
