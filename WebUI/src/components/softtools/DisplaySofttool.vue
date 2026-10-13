<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type { InstrumentStatus } from '../../api/types'
import { MAX_DIAGRAMS, MAX_TRACES } from '../../domain/presentation'
import type {
  DisplayAnnotations,
  InfoTablePosition,
  MarkerInfoLocation,
  LayoutMode,
} from '../../domain/presentation'

const props = defineProps<{
  status: InstrumentStatus | null
  canEditDisplay: boolean
  canChangeMeasurements: boolean
  busy: boolean
}>()
const emit = defineEmits<{
  layout: [value: LayoutMode]
  overlay: []
  split: []
  selectDiagram: [number: number]
  addTrace: []
  deleteDiagram: [number: number]
  infoTable: [value: { enabled?: boolean; position?: InfoTablePosition }]
  annotations: [value: Partial<DisplayAnnotations>]
  markerInfo: [value: { enabled: boolean; diagram?: number }]
  markerInfoLocation: [value: { name: string; location: MarkerInfoLocation }]
}>()
const display = computed(() => props.status?.display)
const diagrams = computed(() => display.value?.diagrams ?? [])
const canAddDiagram = computed(
  () =>
    props.canChangeMeasurements &&
    !props.busy &&
    Boolean(props.status) &&
    diagrams.value.length < MAX_DIAGRAMS &&
    (props.status?.measurements.length ?? 0) < MAX_TRACES,
)
const canDeleteDiagram = computed(
  () =>
    props.canChangeMeasurements &&
    !props.busy &&
    Boolean(selectedDiagram.value) &&
    diagrams.value.length > 1,
)
const table = computed(
  () =>
    display.value?.infoTable ?? { visible: false, position: 'bottom' as const },
)
const annotations = computed(
  () =>
    display.value?.annotations ?? {
      activeTraceOnly: false,
      stimulusInfoVisible: true,
      fontScale: 100,
    },
)
const selectedDiagram = computed(() =>
  display.value?.diagrams.find(
    (diagram) => diagram.number === display.value?.selectedDiagram,
  ),
)
const selectedName = computed(() => display.value?.selectedMeasurement ?? '')
const markerLocation = computed(
  () =>
    display.value?.presentations[selectedName.value]?.markerInfoLocation ??
    'diagram',
)
const fontScale = ref(100)
const fontError = ref('')
watch(
  () => annotations.value.fontScale,
  (value) => {
    fontScale.value = value
    fontError.value = ''
  },
  { immediate: true },
)

function applyFont() {
  if (!props.canEditDisplay) return
  if (
    !Number.isFinite(fontScale.value) ||
    fontScale.value < 80 ||
    fontScale.value > 170
  ) {
    fontError.value = '字号范围为 80%～170%。'
    return
  }
  fontError.value = ''
  emit('annotations', { fontScale: fontScale.value })
}
function moveInfo(location: MarkerInfoLocation) {
  if (
    !props.canEditDisplay ||
    !selectedName.value ||
    (location === 'table' && !table.value.visible)
  )
    return
  emit('markerInfoLocation', { name: selectedName.value, location })
}

function selectDiagram(event: Event) {
  const input = event.target as HTMLSelectElement
  const number = Number(input.value)
  // 选择由 C++ 确认；等待操作读回时不保留未生效的活动图窗。
  input.value = String(selectedDiagram.value?.number ?? '')
  if (
    !props.canEditDisplay ||
    props.busy ||
    !diagrams.value.some((diagram) => diagram.number === number) ||
    number === selectedDiagram.value?.number
  )
    return
  emit('selectDiagram', number)
}

function addDiagram() {
  if (canAddDiagram.value) emit('addTrace')
}

function deleteDiagram() {
  if (canDeleteDiagram.value && selectedDiagram.value)
    emit('deleteDiagram', selectedDiagram.value.number)
}
</script>

<template>
  <section class="softtool-section" aria-label="Display settings">
    <h2 class="softtool-section-title">Diagrams</h2>
    <label class="field-row">
      <span class="softtool-label">Active Diagram</span>
      <select
        class="field-input"
        :value="selectedDiagram?.number ?? ''"
        :disabled="!canEditDisplay || busy || !selectedDiagram"
        aria-label="Active diagram"
        data-testid="active-diagram"
        @change="selectDiagram"
      >
        <option
          v-for="diagram in diagrams"
          :key="diagram.number"
          :value="diagram.number"
        >
          {{ diagram.number }}
        </option>
      </select>
    </label>
    <button
      type="button"
      class="softtool-button"
      :disabled="!canAddDiagram"
      data-testid="display-add-diagram"
      @click="addDiagram"
    >
      Add Trace + Diagram
    </button>
    <button
      type="button"
      class="softtool-button"
      :disabled="!canDeleteDiagram"
      data-testid="display-delete-diagram"
      @click="deleteDiagram"
    >
      Delete Diagram
    </button>
    <p class="softtool-note">
      新图窗创建默认迹线。Delete Diagram
      删除该图窗及其所属迹线；最后一个图窗保留。
    </p>
    <div class="softtool-divider" />
    <h2 class="softtool-section-title">Diagram Layout</h2>
    <div class="softtool-grid">
      <button
        class="softtool-button"
        :disabled="!canEditDisplay"
        :aria-pressed="display?.layout === 'grid'"
        @click="emit('layout', 'grid')"
      >
        All Diagrams
      </button>
      <button
        class="softtool-button"
        :disabled="!canEditDisplay"
        :aria-pressed="display?.layout === 'maximized'"
        @click="emit('layout', 'maximized')"
      >
        Maximized
      </button>
      <button
        class="softtool-button"
        :disabled="!canEditDisplay"
        @click="emit('overlay')"
      >
        Overlay All
      </button>
      <button
        class="softtool-button"
        :disabled="!canEditDisplay"
        @click="emit('split')"
      >
        Split All
      </button>
    </div>
    <div class="softtool-divider" />
    <h2 class="softtool-section-title">Info Table</h2>
    <button
      class="softtool-choice"
      :disabled="!canEditDisplay"
      :class="{ 'is-active': table.visible }"
      :aria-pressed="table.visible"
      data-testid="info-table-visible"
      @click="emit('infoTable', { enabled: !table.visible })"
    >
      Show Table <strong>{{ table.visible ? 'On' : 'Off' }}</strong>
    </button>
    <div class="softtool-grid" role="group" aria-label="Info Table position">
      <button
        v-for="position in ['bottom', 'left', 'right'] as const"
        :key="position"
        class="softtool-button"
        :disabled="!canEditDisplay"
        :aria-pressed="table.position === position"
        :class="{ 'is-active': table.position === position }"
        @click="emit('infoTable', { position })"
      >
        {{ position[0]!.toUpperCase() + position.slice(1) }}
      </button>
    </div>
    <p class="softtool-note">
      将图内 Marker 信息框拖入表格。表格隐藏后保留字段归属，不自动移回图内。
    </p>
    <div class="softtool-divider" />
    <h2 class="softtool-section-title">Marker Info</h2>
    <div class="softtool-readout">
      <span>Diagram</span><strong>{{ selectedDiagram?.number ?? '—' }}</strong>
    </div>
    <button
      class="softtool-choice"
      :disabled="!canEditDisplay || !selectedDiagram"
      :aria-pressed="selectedDiagram?.markerInfoVisible !== false"
      @click="
        emit('markerInfo', {
          enabled: selectedDiagram?.markerInfoVisible === false,
          diagram: selectedDiagram?.number,
        })
      "
    >
      Marker Info
      <strong>{{
        selectedDiagram?.markerInfoVisible === false ? 'Off' : 'On'
      }}</strong>
    </button>
    <div class="softtool-readout">
      <span>Trace</span><strong>{{ selectedName || '—' }}</strong>
    </div>
    <div class="softtool-grid">
      <button
        class="softtool-button"
        :disabled="!canEditDisplay || !selectedName"
        :aria-pressed="markerLocation === 'diagram'"
        @click="moveInfo('diagram')"
      >
        In Diagram
      </button>
      <button
        class="softtool-button"
        :disabled="!canEditDisplay || !selectedName || !table.visible"
        :aria-pressed="markerLocation === 'table'"
        :title="!table.visible ? '先开启 Show Table' : undefined"
        @click="moveInfo('table')"
      >
        In Info Table
      </button>
    </div>
    <button
      class="softtool-choice"
      :disabled="!canEditDisplay"
      :aria-pressed="annotations.activeTraceOnly"
      @click="
        emit('annotations', { activeTraceOnly: !annotations.activeTraceOnly })
      "
    >
      Active Trace Only
      <strong>{{ annotations.activeTraceOnly ? 'On' : 'Off' }}</strong>
    </button>
    <button
      class="softtool-choice"
      :disabled="!canEditDisplay"
      :aria-pressed="annotations.stimulusInfoVisible"
      @click="
        emit('annotations', {
          stimulusInfoVisible: !annotations.stimulusInfoVisible,
        })
      "
    >
      Marker Stimulus Info
      <strong>{{ annotations.stimulusInfoVisible ? 'On' : 'Off' }}</strong>
    </button>
    <div class="softtool-divider" />
    <h2 class="softtool-section-title">Font Size</h2>
    <form @submit.prevent="applyFont">
      <label class="field-row"
        ><span class="softtool-label">Size (%)</span
        ><input
          class="field-input"
          v-model.number="fontScale"
          type="number"
          min="80"
          max="170"
          step="0.1"
          :disabled="!canEditDisplay"
          aria-label="Annotation font size"
      /></label>
      <button
        class="softtool-button softtool-primary softtool-apply"
        :disabled="!canEditDisplay"
        type="submit"
      >
        Apply
      </button>
    </form>
    <p v-if="fontError" class="softtool-note" role="alert">{{ fontError }}</p>
  </section>
</template>
