<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type { AcquiredFrame, Measurement } from '../../api/types'
import type { MarkerCommand } from '../../composables/useWorkspace'
import type { TracePresentation } from '../../domain/presentation'
import { diagramTraceColor } from '../../domain/presentation'
import {
  activeMarker,
  activeMarkerId,
  MARKER_IDS,
  traceMarkers,
} from '../../domain/markers'
import { getMarkerReadout, frequencyText, numberText } from '../../plot'
import FrequencyField from '../shared/FrequencyField.vue'

const props = defineProps<{
  measurement: Measurement | undefined
  presentation: TracePresentation | undefined
  frame: AcquiredFrame | null
  index: number
  canEditDisplay: boolean
}>()
const emit = defineEmits<{ marker: [value: MarkerCommand] }>()
const editable = computed(() =>
  Boolean(props.measurement && props.presentation && props.canEditDisplay),
)
const selectedId = computed(() => activeMarkerId(props.presentation))
const selected = computed(() => activeMarker(props.presentation))
const markers = computed(() => traceMarkers(props.presentation))
const readout = computed(() =>
  getMarkerReadout(props.frame, props.measurement, props.presentation),
)
const color = computed(() => diagramTraceColor(props.index, 'single'))
const frequency = ref(0)
const error = ref('')

watch(
  [
    () => props.measurement?.name,
    selectedId,
    () => selected.value?.frequencyHz,
    () =>
      selected.value?.frequencyHz === null
        ? readout.value?.frequencyHz
        : undefined,
  ],
  () => {
    frequency.value =
      selected.value?.frequencyHz ??
      readout.value?.frequencyHz ??
      props.frame?.frequencyHz[
        Math.floor((props.frame.frequencyHz.length - 1) / 2)
      ] ??
      0
    error.value = ''
  },
  { immediate: true },
)

function update(value: MarkerCommand) {
  if (!editable.value) return
  emit(
    'marker',
    value.markerAction === 'add' || value.markerAction === 'allOff'
      ? value
      : { markerId: selectedId.value, ...value },
  )
}

function select(id: number) {
  update({ markerAction: 'select', markerId: id })
}

function applyFrequency() {
  if (!editable.value || !selected.value?.enabled) return
  if (!Number.isFinite(frequency.value) || frequency.value < 0) {
    error.value = '请输入有效的非负频率。'
    return
  }
  error.value = ''
  update({ frequencyHz: frequency.value })
}
</script>

<template>
  <section class="softtool-section" aria-label="Markers">
    <div
      class="softtool-readout"
      data-testid="marker-scope"
      :style="{ borderLeft: `3px solid ${color}`, paddingLeft: '7px' }"
    >
      <span>Active Trace · Ch{{ measurement?.channelId ?? 1 }}</span>
      <strong>{{ measurement?.name ?? 'No active trace' }}</strong>
      <span
        >{{ measurement?.parameter ?? '请选择测量' }} · M{{ selectedId }}</span
      >
    </div>
    <h3 class="softtool-section-title">Markers</h3>
    <form @submit.prevent="applyFrequency">
      <FrequencyField
        id="marker-frequency"
        :model-value="frequency"
        label="Stimulus"
        :disabled="!editable || !selected?.enabled"
        @update:model-value="frequency = $event"
      />
      <button
        class="softtool-button softtool-primary softtool-apply"
        type="submit"
        :disabled="!editable || !selected?.enabled"
        data-testid="apply-marker"
      >
        Set Marker
      </button>
    </form>
    <p v-if="error" class="softtool-note" role="alert">{{ error }}</p>
    <div class="softtool-grid">
      <button
        class="softtool-button"
        type="button"
        :disabled="!editable"
        :aria-pressed="selected?.enabled"
        data-testid="marker-enabled"
        @click="update({ enabled: !selected?.enabled })"
      >
        Marker <strong>{{ selected?.enabled ? 'On' : 'Off' }}</strong>
      </button>
      <button
        class="softtool-button"
        type="button"
        :disabled="!editable || !markers.some((marker) => marker.enabled)"
        data-testid="markers-all-off"
        :title="`Turn off all markers on ${measurement?.name ?? 'the active trace'}`"
        @click="update({ markerAction: 'allOff' })"
      >
        All Off
      </button>
    </div>
    <div class="marker-selector" aria-label="Marker selection">
      <button
        v-for="id in MARKER_IDS"
        :key="id"
        class="softtool-choice"
        type="button"
        :disabled="!editable"
        :aria-pressed="selectedId === id"
        :class="{
          'is-active': selectedId === id,
          'is-enabled': markers.some(
            (marker) => marker.id === id && marker.enabled,
          ),
        }"
        :data-marker-id="id"
        :title="`${measurement?.name ?? 'Active trace'} · M${id}`"
        @click="select(id)"
      >
        Mkr {{ id }}
      </button>
    </div>
    <button
      class="softtool-button"
      type="button"
      :disabled="
        !editable ||
        markers.filter((marker) => marker.enabled).length >= MARKER_IDS.length
      "
      data-testid="add-marker"
      @click="update({ markerAction: 'add' })"
    >
      Add Marker
    </button>
    <button
      class="softtool-button"
      type="button"
      :disabled="!editable || !selected?.enabled"
      data-testid="remove-marker"
      @click="update({ enabled: false })"
    >
      Remove Marker
    </button>
    <div class="softtool-divider" aria-hidden="true" />
    <button
      class="softtool-button"
      type="button"
      :disabled="!editable"
      :aria-pressed="selected?.discrete"
      data-testid="marker-discrete"
      @click="update({ discrete: !selected?.discrete })"
    >
      Discrete <strong>{{ selected?.discrete ? 'On' : 'Off' }}</strong>
    </button>
    <h3 class="softtool-section-title">Actual Sample · M{{ selectedId }}</h3>
    <div class="softtool-readout" data-testid="marker-value" :style="{ color }">
      <template v-if="readout">
        <span>{{ frequencyText(readout.frequencyHz, 9) }}</span>
        <strong :style="{ color }">{{ readout.valueText }}</strong>
        <span
          >Re {{ readout.real === null ? '—' : numberText(readout.real, 8) }}
          {{ measurement?.unit }}</span
        >
        <span
          >Im {{ readout.imag === null ? '—' : numberText(readout.imag, 8) }}
          {{ measurement?.unit }}</span
        >
        <span
          >Sample
          {{
            readout.interpolated
              ? 'Interpolated'
              : readout.index === null
                ? '—'
                : readout.index + 1
          }}</span
        >
      </template>
      <span v-else>{{
        selected?.enabled ? 'Waiting for data' : 'Marker off'
      }}</span>
    </div>
    <p class="softtool-note">
      M1–M10 属于当前迹线；All Off 只关闭该迹线标记并保留位置。
      同频联动由 Coupled Markers 控制，当前尚未实现。
    </p>
  </section>
</template>

<style scoped>
.marker-selector {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 4px;
  margin: 8px 0;
}
.marker-selector .softtool-choice {
  min-width: 0;
  min-height: 34px;
  padding: 5px 2px;
  font-size: 11px;
}
.marker-selector .is-enabled {
  border-bottom: 2px solid #60e600;
}
</style>
