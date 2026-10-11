<script setup lang="ts">
import { computed, ref } from 'vue'
import type { InstrumentStatus, MeasurementCandidate } from '../../api/types'
import {
  cloneMeasurementCandidate,
  measurementState,
} from '../../domain/measurements'

const props = defineProps<{
  status: InstrumentStatus | null
  activeTraceName: string
  displayedNames: string[]
  canChangeMeasurements: boolean
  busy: boolean
  add: (
    candidate: MeasurementCandidate,
    expectedRevision: string,
  ) => Promise<InstrumentStatus | null>
  remove: (
    name: string,
    expectedRevision: string,
  ) => Promise<InstrumentStatus | null>
}>()
const emit = defineEmits<{
  selectTrace: [name: string]
  display: [value: { name: string; enabled: boolean }]
}>()
const working = ref(false)
const catalog = computed(() => props.status?.measurements ?? [])
const active = computed(() =>
  catalog.value.find((trace) => trace.name === props.activeTraceName),
)
const disabled = computed(
  () => !props.canChangeMeasurements || props.busy || working.value,
)

async function clone() {
  if (disabled.value || !active.value || !props.status) return
  const candidate = cloneMeasurementCandidate(active.value, catalog.value)
  if (!candidate) return
  working.value = true
  try {
    if (await props.add(candidate, props.status.revision))
      emit('selectTrace', candidate.name)
  } finally {
    working.value = false
  }
}

async function remove() {
  if (
    disabled.value ||
    !active.value ||
    !props.status ||
    catalog.value.length <= 1
  )
    return
  working.value = true
  try {
    await props.remove(active.value.name, props.status.revision)
  } finally {
    working.value = false
  }
}
</script>

<template>
  <section class="softtool-section" aria-label="Trace configuration">
    <h3 class="softtool-section-title">Traces</h3>
    <div class="trace-catalog">
      <div
        v-for="measurement in catalog"
        :key="measurement.name"
        class="trace-catalog-entry"
      >
        <button
          type="button"
          class="softtool-button"
          :class="{ 'is-active': activeTraceName === measurement.name }"
          :aria-pressed="activeTraceName === measurement.name"
          @click="emit('selectTrace', measurement.name)"
        >
          {{ measurement.name }}<small>{{ measurement.parameter }}</small>
        </button>
        <label>
          <input
            type="checkbox"
            :aria-label="`Display ${measurement.name}`"
            :checked="displayedNames.includes(measurement.name)"
            :disabled="
              !displayedNames.includes(measurement.name) &&
              displayedNames.length >= 4
            "
            @change="
              emit('display', {
                name: measurement.name,
                enabled: !displayedNames.includes(measurement.name),
              })
            "
          />
          Display
        </label>
      </div>
    </div>
    <div class="softtool-divider" />
    <button
      type="button"
      class="softtool-button"
      :disabled="disabled || !active || catalog.length >= 32"
      @click="clone"
    >
      Add Trace
    </button>
    <button
      type="button"
      class="softtool-button"
      :disabled="disabled || !active || catalog.length <= 1"
      @click="remove"
    >
      Delete Trace
    </button>
    <div v-if="active" class="softtool-readout">
      <span>{{ active.name }} · {{ active.parameter }}</span>
      <span>{{ measurementState(active) }}</span>
    </div>
    <p class="softtool-note">
      Add Trace 复制活动迹线。Meas 修改其测量量；显示最多四条，目录最多32项。
    </p>
    <p v-if="!canChangeMeasurements" class="softtool-note">
      {{
        status?.control.readOnlyReason ||
        '取得控制权并等待当前操作完成后可添加或删除迹线。'
      }}
    </p>
  </section>
</template>

<style scoped>
.trace-catalog {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 8px 5px;
}
.trace-catalog-entry {
  display: grid;
  gap: 3px;
}
.trace-catalog-entry label {
  display: flex;
  align-items: center;
  gap: 4px;
  color: #afc0c9;
  font-size: 11px;
}
</style>
