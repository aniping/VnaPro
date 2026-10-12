<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref } from 'vue'
import InstrumentToolbar from './components/instrument/InstrumentToolbar.vue'
import HardkeyPanel from './components/instrument/HardkeyPanel.vue'
import InstrumentStatusBar from './components/instrument/InstrumentStatusBar.vue'
import DiagramWorkspace from './components/diagrams/DiagramWorkspace.vue'
import SofttoolHost from './components/softtools/SofttoolHost.vue'
import ChannelSettingsSofttool from './components/softtools/ChannelSettingsSofttool.vue'
import MeasurementSofttool from './components/softtools/MeasurementSofttool.vue'
import TraceCatalogSofttool from './components/softtools/TraceCatalogSofttool.vue'
import SweepSofttool from './components/softtools/SweepSofttool.vue'
import TraceSofttool from './components/softtools/TraceSofttool.vue'
import MarkerSofttool from './components/softtools/MarkerSofttool.vue'
import { MAX_TRACES } from './domain/presentation'
import SystemSofttool from './components/softtools/SystemSofttool.vue'
import { useInstrument } from './composables/useInstrument'
import { useWorkspace } from './composables/useWorkspace'
import { enableMarkerOnEntry } from './composables/markerMenuEntry'
import { exportVisibleFrameCsv } from './plot'
import type { MarkerMove } from './plot/markerDrag'

const instrument = useInstrument()
const {
  status,
  frame,
  connected,
  hasControl,
  canWrite,
  canConfigure,
  canChangeMeasurements,
  canEditDisplay,
  pendingOperation,
  writeInFlight,
  controlBusy,
  frameCondition,
  notice,
  error,
} = instrument
const measurements = computed(() => status.value?.measurements ?? [])
const workspace = useWorkspace(
  () => status.value,
  () => frame.value,
  instrument.display,
)
const {
  presentations,
  activeTraceName,
  activeMeasurement,
  activePresentation,
  activeIndex,
  layout,
  canAutoscale,
} = workspace
const { displayedNames, displayedMeasurements } = workspace
const displayedMeasurement = computed(
  () =>
    frame.value?.traces.find((trace) => trace.name === activeTraceName.value) ??
    activeMeasurement.value,
)
const activeTool = ref('start')
const softtoolVisible = ref(true)
const canvasStyle = ref({ left: '0px', top: '0px', transform: 'scale(1)' })
const busy = computed(() =>
  Boolean(
    pendingOperation.value || writeInFlight.value || status.value?.pending,
  ),
)
const group = computed(() =>
  ['start', 'stop', 'center', 'span'].includes(activeTool.value)
    ? 'stimulus'
    : ['meas', 'meas-wave'].includes(activeTool.value)
      ? 'measurement'
      : ['format', 'scale', 'marker', 'trace-config'].includes(activeTool.value)
        ? 'trace'
        : ['sweep', 'sweep-control', 'trigger'].includes(activeTool.value)
          ? 'sweep'
          : activeTool.value === 'bandwidth'
            ? 'bandwidth'
            : 'system',
)
const focusField = computed(() =>
  group.value === 'stimulus'
    ? (activeTool.value as 'start' | 'stop' | 'center' | 'span')
    : undefined,
)
const title = computed(
  () =>
    ({
      stimulus: 'Stimulus',
      measurement: 'Measurement Menu',
      trace: activeTool.value === 'marker' ? 'Markers' : 'Trace',
      sweep: 'Sweep',
      bandwidth: 'Pwr Bw Avg',
      system: 'System',
    })[group.value],
)
const tabs = computed(() =>
  group.value === 'stimulus'
    ? [{ id: 'start', label: 'Stimulus' }]
    : group.value === 'measurement'
      ? [
          { id: 'meas', label: 'S-Params' },
          {
            id: 'meas-ratios',
            label: 'Ratios',
            disabled: true,
            reason: '尚未实现任意接收波比值',
          },
          { id: 'meas-wave', label: 'Wave' },
        ]
      : group.value === 'trace'
        ? [
            { id: 'format', label: 'Format' },
            { id: 'scale', label: 'Scale' },
            { id: 'marker', label: 'Marker' },
            { id: 'trace-config', label: 'Config' },
          ]
        : group.value === 'sweep'
          ? [
              { id: 'sweep', label: 'Sweep Params' },
              { id: 'sweep-control', label: 'Sweep Control' },
              { id: 'trigger', label: 'Trigger' },
            ]
          : group.value === 'bandwidth'
            ? [{ id: 'bandwidth', label: 'Bandwidth' }]
            : [
                { id: 'setup', label: 'State' },
                { id: 'display', label: 'Display' },
                { id: 'file', label: 'File' },
                { id: 'help', label: 'Help' },
              ],
)
const activeTab = computed(() =>
  group.value === 'stimulus'
    ? 'start'
    : activeTool.value === 'tools'
      ? 'file'
      : activeTool.value,
)
const emptyConfig = { startHz: 0, stopHz: 0, points: 0, ifBandwidthHz: 0 }
function selectTool(id: string) {
  activeTool.value = id
  softtoolVisible.value = true
  if (id === 'marker') {
    enableMarkerOnEntry(
      activePresentation.value,
      canEditDisplay.value,
      (markerId) => workspace.selectMarker(activeTraceName.value, markerId),
    )
  }
  if (['start', 'stop', 'center', 'span', 'sweep', 'bandwidth'].includes(id)) {
    void nextTick(() => {
      const inputId =
        id === 'sweep'
          ? 'sweep-points'
          : id === 'bandwidth'
            ? 'channel-bandwidth'
            : `stimulus-${id}`
      const input = document.getElementById(inputId)
      input?.focus()
      if (input instanceof HTMLInputElement) input.select()
    })
  }
}
function fitCanvas() {
  const scale = Math.min(window.innerWidth / 1280, window.innerHeight / 800)
  canvasStyle.value = {
    left: `${(window.innerWidth - 1280 * scale) / 2}px`,
    top: `${(window.innerHeight - 800 * scale) / 2}px`,
    transform: `scale(${scale})`,
  }
}
function keydown(event: KeyboardEvent) {
  if (event.key === 'Escape' && !document.querySelector('dialog[open]'))
    softtoolVisible.value = false
}
function exportCsv() {
  if (!frame.value) return
  const csv = exportVisibleFrameCsv(frame.value, presentations.value)
  const url = URL.createObjectURL(
    new Blob([csv], { type: 'text/csv;charset=utf-8' }),
  )
  const link = document.createElement('a')
  link.href = url
  link.download = `vnapro-frame-${frame.value.frameSeq}.csv`
  link.click()
  setTimeout(() => URL.revokeObjectURL(url), 1000)
}
async function deleteActive() {
  if (
    canChangeMeasurements.value &&
    status.value &&
    activeMeasurement.value &&
    measurements.value.length > 1
  ) {
    await instrument.deleteMeasurement(
      activeMeasurement.value.name,
      status.value.revision,
    )
  }
}
async function addTrace() {
  if (!canChangeMeasurements.value || !status.value || !activeMeasurement.value)
    return
  await instrument.cloneMeasurement(activeMeasurement.value.name)
}
async function commitMarker(value: MarkerMove): Promise<boolean> {
  return Boolean(
    await workspace.setMarker(
      {
        enabled: true,
        markerId: value.markerId,
        frequencyHz: value.frequencyHz,
        sampleIndex: value.sampleIndex,
        step: value.step,
        discrete: value.discrete,
      },
      value.name,
    ),
  )
}
onMounted(() => {
  fitCanvas()
  window.addEventListener('resize', fitCanvas)
  window.addEventListener('keydown', keydown)
  instrument.start()
})
onBeforeUnmount(() => {
  window.removeEventListener('resize', fitCanvas)
  window.removeEventListener('keydown', keydown)
  instrument.stop()
})
</script>

<template>
  <main class="instrument-viewport" aria-label="VnaPro network analyzer">
    <div
      class="instrument-canvas"
      :style="{ position: 'absolute', ...canvasStyle }"
    >
      <div class="instrument-shell">
        <div
          class="instrument-workspace"
          :class="{ 'softtool-hidden': !softtoolVisible }"
        >
          <section class="measurement-stage" aria-label="Measurement display">
            <InstrumentToolbar
              :layout="layout"
              :has-trace="Boolean(activeMeasurement)"
              :can-autoscale="canEditDisplay && canAutoscale"
              :can-edit-display="canEditDisplay"
              :marker-enabled="activePresentation?.marker.enabled ?? false"
              :can-export="Boolean(frame)"
              :can-write="canWrite"
              :busy="busy"
              :can-delete="canChangeMeasurements && measurements.length > 1"
              :can-add-trace="
                canChangeMeasurements && measurements.length < MAX_TRACES
              "
              @select-tool="selectTool"
              @toggle-layout="
                workspace.setLayout(layout === 'grid' ? 'maximized' : 'grid')
              "
              @autoscale="workspace.autoscale"
              @toggle-marker="
                workspace.setMarker({
                  enabled: !activePresentation?.marker.enabled,
                })
              "
              @export-csv="exportCsv"
              @single="instrument.run('single')"
              @add-trace="addTrace"
              @delete-trace="deleteActive"
            />
            <DiagramWorkspace
              :diagrams="status?.display.diagrams ?? []"
              :frame="frame"
              :measurements="displayedMeasurements"
              :presentations="presentations"
              :active-trace-name="activeTraceName"
              :layout="layout"
              :config="status?.config ?? emptyConfig"
              :frame-condition="frameCondition"
              :can-edit-display="canEditDisplay"
              :commit-marker="commitMarker"
              @select-trace="workspace.selectTrace"
              @toggle-visibility="workspace.toggleVisibility"
              @set-layout="workspace.setLayout"
              @add-marker="workspace.addMarker($event)"
              @select-marker="
                workspace.selectMarker($event.name, $event.markerId)
              "
              @move-marker="
                workspace.setMarker(
                  {
                    enabled: true,
                    markerId: $event.markerId,
                    frequencyHz: $event.frequencyHz,
                    sampleIndex: $event.sampleIndex,
                    step: $event.step,
                    discrete: $event.discrete,
                  },
                  $event.name,
                )
              "
            />
          </section>
          <SofttoolHost
            v-show="softtoolVisible"
            :title="title"
            :tabs="tabs"
            :active-tab="activeTab"
            @select="selectTool"
            @close="softtoolVisible = false"
          >
            <ChannelSettingsSofttool
              v-show="group === 'stimulus'"
              mode="frequency"
              :status="status"
              :can-configure="canConfigure"
              :busy="busy"
              :focus-field="focusField"
              :apply="instrument.configure"
            />
            <ChannelSettingsSofttool
              v-show="activeTool === 'sweep'"
              mode="points"
              :status="status"
              :can-configure="canConfigure"
              :busy="busy"
              :apply="instrument.configure"
            />
            <ChannelSettingsSofttool
              v-show="group === 'bandwidth'"
              mode="bandwidth"
              :status="status"
              :can-configure="canConfigure"
              :busy="busy"
              :apply="instrument.configure"
            />
            <MeasurementSofttool
              v-if="group === 'measurement'"
              :status="status"
              :can-change-measurements="canChangeMeasurements"
              :busy="busy"
              :active-trace-name="activeTraceName"
              :mode="activeTool as 'meas' | 'meas-wave'"
              :update="instrument.updateMeasurement"
            />
            <TraceCatalogSofttool
              v-if="activeTool === 'trace-config'"
              :status="status"
              :can-change-measurements="canChangeMeasurements"
              :busy="busy"
              :active-trace-name="activeTraceName"
              :displayed-names="displayedNames"
              :can-edit-display="canEditDisplay"
              :clone="instrument.cloneMeasurement"
              :remove="instrument.deleteMeasurement"
              @select-trace="workspace.selectTrace"
              @display="workspace.setDisplayed($event.name, $event.enabled)"
            />
            <TraceSofttool
              v-if="activeTool === 'format' || activeTool === 'scale'"
              :mode="activeTool as 'format' | 'scale'"
              :measurement="displayedMeasurement"
              :presentation="activePresentation"
              :frame="frame"
              :index="activeIndex"
              :layout="layout"
              :can-autoscale="canEditDisplay && canAutoscale"
              :can-edit-display="canEditDisplay"
              @format="workspace.setFormat"
              @scale="workspace.setScale"
              @smith-radius="workspace.setSmithRadius"
              @autoscale="workspace.autoscale"
              @toggle-visibility="workspace.toggleVisibility()"
            />
            <MarkerSofttool
              v-if="activeTool === 'marker'"
              :measurement="displayedMeasurement"
              :presentation="activePresentation"
              :frame="frame"
              :index="activeIndex"
              :can-edit-display="canEditDisplay"
              @marker="workspace.setMarker"
            />
            <SweepSofttool
              v-show="group === 'sweep' && activeTool !== 'sweep'"
              :mode="activeTool === 'trigger' ? 'trigger' : 'sweep-control'"
              :status="status"
              :can-write="canWrite"
              :busy="busy"
              :action="instrument.run"
            />
            <SystemSofttool
              v-if="group === 'system'"
              :mode="activeTool"
              :status="status"
              :frame="frame"
              :layout="layout"
              :connected="connected"
              :has-control="hasControl"
              :control-busy="controlBusy"
              :can-edit-display="canEditDisplay"
              @layout="workspace.setLayout"
              @export="exportCsv"
              @acquire="instrument.claim()"
              @release="instrument.release()"
            />
          </SofttoolHost>
          <HardkeyPanel
            :active-key="
              softtoolVisible
                ? activeTool === 'sweep-control'
                  ? 'sweep'
                  : activeTool
                : ''
            "
            :has-trace="Boolean(activeMeasurement)"
            @select="selectTool"
          />
        </div>
        <InstrumentStatusBar
          :connected="connected"
          :core-state="status?.state ?? ''"
          :mode="status?.config.sweepMode ?? ''"
          :controller="status?.control.owner ?? 'none'"
          :has-control="hasControl"
          :claim-busy="controlBusy"
          :frame-seq="frame?.frameSeq ?? null"
          :run-id="status?.runId ?? null"
          :revision="status?.revision ?? null"
          :notice="
            pendingOperation
              ? `${pendingOperation.label} · ${pendingOperation.readingBack ? 'Readback' : 'Pending'}`
              : (notice?.text ?? '')
          "
          :notice-kind="notice?.tone ?? 'info'"
          :error="error ?? undefined"
          @select-tool="selectTool"
          @acquire-control="instrument.claim()"
          @release-control="instrument.release()"
        />
      </div>
    </div>
  </main>
</template>
