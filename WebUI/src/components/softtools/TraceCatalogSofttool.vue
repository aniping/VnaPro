<script setup lang="ts">
import { computed, ref } from 'vue'
import type { InstrumentStatus, TraceDestination } from '../../api/types'
import { measurementState } from '../../domain/measurements'
import { MAX_DIAGRAMS, MAX_TRACES } from '../../domain/presentation'
import { beginNewTraceDrag } from '../instrument/newTraceDrag'

const props = defineProps<{
  status: InstrumentStatus | null
  activeTraceName: string
  displayedNames: string[]
  canChangeMeasurements: boolean
  busy: boolean
  canEditDisplay: boolean
  clone: (
    sourceName: string,
    destination?: TraceDestination,
  ) => Promise<InstrumentStatus | null>
  remove: (
    name: string,
    expectedRevision: string,
  ) => Promise<InstrumentStatus | null>
}>()
const emit = defineEmits<{
  selectTrace: [name: string, diagram?: number]
  display: [value: { name: string; enabled: boolean }]
}>()
const working = ref(false)
const catalog = computed(() => props.status?.measurements ?? [])
const active = computed(() =>
  catalog.value.find((trace) => trace.name === props.activeTraceName),
)
const diagrams = computed(() => props.status?.display.diagrams ?? [])
const traceDiagram = computed(() =>
  diagrams.value.find((diagram) =>
    diagram.measurementNames.includes(props.activeTraceName),
  ),
)
const disabled = computed(
  () => !props.canChangeMeasurements || props.busy || working.value,
)
const canClone = computed(
  () =>
    !disabled.value &&
    Boolean(active.value) &&
    catalog.value.length < MAX_TRACES,
)
const canCloneDiagram = computed(
  () => canClone.value && diagrams.value.length < MAX_DIAGRAMS,
)

async function clone(newDiagram = false) {
  if (
    !canClone.value ||
    (newDiagram && !canCloneDiagram.value) ||
    !active.value
  )
    return
  working.value = true
  try {
    await props.clone(
      active.value.name,
      newDiagram ? { newDiagram: true } : undefined,
    )
  } finally {
    working.value = false
  }
}

function moveTrace(event: Event) {
  const input = event.target as HTMLSelectElement
  const number = Number(input.value)
  // 原生下拉先恢复设备读回值，归属变化完成后再由后端快照更新。
  input.value = String(traceDiagram.value?.number ?? '')
  if (
    !props.canEditDisplay ||
    props.busy ||
    working.value ||
    !active.value ||
    !diagrams.value.some((diagram) => diagram.number === number) ||
    number === traceDiagram.value?.number
  )
    return
  emit('selectTrace', active.value.name, number)
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
    <h3 class="softtool-section-title">Active Trace</h3>
    <div v-if="active" class="softtool-readout">
      <span>{{ active.name }} · {{ active.parameter }}</span>
      <span>{{ measurementState(active) }}</span>
    </div>
    <button
      type="button"
      class="softtool-button"
      :disabled="!canClone"
      data-testid="trace-add"
      :draggable="canClone"
      title="单击复制活动迹线；拖到图窗选择新迹线的 S 参数"
      @dragstart="beginNewTraceDrag($event, canClone)"
      @click="clone(false)"
    >
      Add Trace
    </button>
    <button
      type="button"
      class="softtool-button"
      :disabled="!canCloneDiagram"
      data-testid="trace-add-diagram"
      @click="clone(true)"
    >
      Add Trace + Diagram
    </button>
    <button
      type="button"
      class="softtool-button"
      :disabled="disabled || !active || catalog.length <= 1"
      @click="remove"
    >
      Delete Trace
    </button>
    <label class="field-row">
      <span class="softtool-label">Diagram</span>
      <select
        class="field-input"
        :value="traceDiagram?.number ?? ''"
        :disabled="!canEditDisplay || busy || working || !active"
        aria-label="Trace diagram"
        data-testid="trace-diagram"
        @change="moveTrace"
      >
        <option v-if="!traceDiagram" value="" disabled>Not displayed</option>
        <option
          v-for="diagram in diagrams"
          :key="diagram.number"
          :value="diagram.number"
        >
          {{ diagram.number }}
        </option>
      </select>
    </label>
    <p class="softtool-note">
      Add Trace 复制活动迹线；Add Trace + Diagram 将副本放入新图窗。 Diagram
      移动当前迹线，保留其格式和标记。编号由后端分配。
    </p>
    <p v-if="!canChangeMeasurements" class="softtool-note">
      {{
        status?.control.readOnlyReason ||
        '取得控制权并等待当前操作完成后可添加或删除迹线。'
      }}
    </p>
    <div class="softtool-divider" />
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
          :disabled="!canEditDisplay || busy || working"
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
              !canEditDisplay ||
              busy ||
              working ||
              (!displayedNames.includes(measurement.name) &&
                displayedNames.length >= MAX_TRACES)
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
