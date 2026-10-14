<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import InstrumentToolbar from './components/instrument/InstrumentToolbar.vue'
import HardkeyPanel from './components/instrument/HardkeyPanel.vue'
import InstrumentStatusBar from './components/instrument/InstrumentStatusBar.vue'
import DiagramWorkspace from './components/diagrams/DiagramWorkspace.vue'
import SofttoolHost from './components/softtools/SofttoolHost.vue'
import ChannelSettingsSofttool from './components/softtools/ChannelSettingsSofttool.vue'
import MeasurementSofttool from './components/softtools/MeasurementSofttool.vue'
import NewTraceWorkflow from './components/softtools/NewTraceWorkflow.vue'
import TraceCatalogSofttool from './components/softtools/TraceCatalogSofttool.vue'
import SweepSofttool from './components/softtools/SweepSofttool.vue'
import TraceSofttool from './components/softtools/TraceSofttool.vue'
import MarkerSofttool from './components/softtools/MarkerSofttool.vue'
import CalibrationSofttool from './components/softtools/CalibrationSofttool.vue'
import CalibrationWorkflow from './components/softtools/CalibrationWorkflow.vue'
import CalibrationAcquisition from './components/softtools/CalibrationAcquisition.vue'
import { calibrationSummary } from './domain/calibration'
import { MAX_DIAGRAMS, MAX_TRACES } from './domain/presentation'
import SystemSofttool from './components/softtools/SystemSofttool.vue'
import DisplaySofttool from './components/softtools/DisplaySofttool.vue'
import { useInstrument } from './composables/useInstrument'
import { useWorkspace } from './composables/useWorkspace'
import { enableMarkerOnEntry } from './composables/markerMenuEntry'
import { exportVisibleFrameCsv } from './plot'
import type { MarkerMove } from './plot/markerDrag'
import type { TraceDestination } from './api/types'

const instrument = useInstrument()
const {
  status,
  frame,
  connected,
  hasControl,
  canConfigure,
  canChangeMeasurements,
  canCalibrate,
  canRun,
  canCancelCalibration,
  canEditDisplay: canWriteDisplay,
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
const { displayedNames } = workspace
const displayedMeasurement = computed(
  () =>
    frame.value?.traces.find((trace) => trace.name === activeTraceName.value) ??
    activeMeasurement.value,
)
const activeTool = ref('start')
const softtoolVisible = ref(true)
const newTraceDestination = ref<TraceDestination | null>(null)
const calibrationWorkflowOpen = ref(false)
const calibrationAcquisitionVisible = computed(() => Boolean(calibrationWorkflowOpen.value && status.value?.calibration?.collecting))
// 结束会话会先卸载采集组件；不能等待它的异步 close 事件来关闭向导。
watch(() => status.value?.calibration?.collecting, (collecting, previous) => {
  if (previous === true && collecting === false) calibrationWorkflowOpen.value = false
})
const canEditDisplay = computed(() => canWriteDisplay.value && !calibrationAcquisitionVisible.value)
const calibrationLabel = computed(() => calibrationSummary(status.value?.calibration))
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
          : ['cal', 'cal-use'].includes(activeTool.value)
            ? 'calibration'
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
      calibration: 'Calibration',
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
          : group.value === 'calibration'
            ? [
                { id: 'cal', label: 'Start Cal' },
                { id: 'cal-use', label: 'Use Cal' },
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
async function addTrace(destination?: {
  diagram?: number
  newDiagram?: boolean
}) {
  if (
    !canChangeMeasurements.value ||
    !status.value ||
    measurements.value.length >= MAX_TRACES ||
    (destination?.newDiagram &&
      status.value.display.diagrams.length >= MAX_DIAGRAMS)
  )
    return
  await instrument.createTrace(destination)
}

function openNewTrace(diagram: number) {
  if (
    !canChangeMeasurements.value ||
    busy.value ||
    !status.value ||
    measurements.value.length >= MAX_TRACES ||
    !status.value.display.diagrams.some((item) => item.number === diagram)
  )
    return
  newTraceDestination.value = { diagram }
}

async function copyActiveTrace() {
  if (
    canChangeMeasurements.value &&
    !busy.value &&
    activeMeasurement.value &&
    measurements.value.length < MAX_TRACES
  ) {
    await instrument.cloneMeasurement(activeMeasurement.value.name)
  }
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
              :backend="status?.backend"
              :calibration="status?.calibration"
              :layout="layout"
              :has-trace="Boolean(activeMeasurement) && !calibrationAcquisitionVisible"
              :can-autoscale="canEditDisplay && canAutoscale"
              :can-edit-display="canEditDisplay"
              :marker-enabled="activePresentation?.marker.enabled ?? false"
              :can-export="Boolean(frame) && !calibrationAcquisitionVisible"
              :can-write="canRun"
              :busy="busy"
              :can-delete="canChangeMeasurements && measurements.length > 1"
              :can-add-trace="
                canChangeMeasurements && measurements.length < MAX_TRACES
              "
              @select-tool="selectTool"
              @toggle-layout="
                workspace.setLayout(
                  layout === 'maximized' ? 'grid' : 'maximized',
                )
              "
              @autoscale="workspace.autoscale"
              @toggle-marker="
                workspace.setMarker({
                  enabled: !activePresentation?.marker.enabled,
                })
              "
              @export-csv="exportCsv"
              @single="instrument.run('single')"
              @add-trace="copyActiveTrace"
              @delete-trace="deleteActive"
            />
            <CalibrationAcquisition
              v-if="calibrationAcquisitionVisible && status"
              :status="status"
              :disabled="!canCalibrate || busy"
              :can-cancel="canCancelCalibration"
              :command="instrument.calibrate"
              :load-preview="instrument.calibrationPreview"
              @close="calibrationWorkflowOpen = false"
            />
            <DiagramWorkspace
              v-else
              :diagrams="status?.display.diagrams ?? []"
              :frame="frame"
              :measurements="measurements"
              :presentations="presentations"
              :active-trace-name="activeTraceName"
              :selected-diagram="status?.display.selectedDiagram"
              :can-add-trace="
                canChangeMeasurements && measurements.length < MAX_TRACES
              "
              :can-add-diagram="
                canChangeMeasurements &&
                (status?.display.diagrams.length ?? 0) < MAX_DIAGRAMS
              "
              :layout="layout"
              :config="status?.config ?? emptyConfig"
              :grid-columns="status?.display.gridColumns ?? 1"
              :grid-rows="status?.display.gridRows ?? 1"
              :frame-condition="frameCondition"
              :can-edit-display="canEditDisplay"
              :commit-marker="commitMarker"
              :info-table="status?.display.infoTable"
              :annotations="status?.display.annotations"
              @relocate-marker-info="
                workspace.setMarkerInfoLocation($event.name, $event.location)
              "
              @select-trace="workspace.selectTrace"
              @toggle-visibility="workspace.toggleVisibility"
              @set-layout="workspace.setLayout($event.layout, $event.diagram)"
              @select-diagram="workspace.selectDiagram"
              @add-trace="addTrace($event)"
              @new-trace="openNewTrace"
              @delete-diagram="instrument.deleteDiagram($event)"
              @arrange="
                $event.action === 'overlay'
                  ? workspace.overlayAll($event.diagram)
                  : workspace.splitAll()
              "
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
              :all-s-parameters="instrument.createAllSParameters"
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
              :can-write="canRun"
              :busy="busy"
              :action="instrument.run"
            />
            <CalibrationSofttool
              v-if="group === 'calibration'"
              :mode="activeTool as 'cal' | 'cal-use'"
              :status="status"
              :can-calibrate="canCalibrate"
              :busy="busy"
              :command="instrument.calibrate"
              :export-set="instrument.exportCalibration"
              @start="calibrationWorkflowOpen = true"
            />
            <SystemSofttool
              v-if="group === 'system' && activeTool !== 'display'"
              :mode="activeTool"
              :status="status"
              :frame="calibrationAcquisitionVisible ? null : frame"
              :connected="connected"
              :has-control="hasControl"
              :control-busy="controlBusy"
              @export="exportCsv"
              @acquire="instrument.claim()"
              @release="instrument.release()"
            />
            <DisplaySofttool
              v-if="activeTool === 'display'"
              :status="status"
              :can-edit-display="canEditDisplay"
              :can-change-measurements="canChangeMeasurements"
              :busy="busy"
              @select-diagram="workspace.selectDiagram"
              @add-trace="addTrace({ newDiagram: true })"
              @delete-diagram="instrument.deleteDiagram"
              @layout="workspace.setLayout"
              @overlay="workspace.overlayAll()"
              @split="workspace.splitAll"
              @info-table="workspace.setInfoTable"
              @annotations="workspace.setAnnotations"
              @marker-info="
                workspace.setMarkerInfo($event.enabled, $event.diagram)
              "
              @marker-info-location="
                workspace.setMarkerInfoLocation($event.name, $event.location)
              "
            />
          </SofttoolHost>
          <HardkeyPanel
            :backend="status?.backend"
            :active-key="
              softtoolVisible
                ? activeTool === 'sweep-control'
                  ? 'sweep'
                  : activeTool === 'cal-use' ? 'cal' : activeTool
                : ''
            "
            :has-trace="Boolean(activeMeasurement) && !calibrationAcquisitionVisible"
            :calibration-supported="Boolean(status?.calibration)"
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
          :run-id="frame?.runId ?? status?.runId ?? null"
          :revision="frame?.revision ?? status?.acquisitionRevision ?? null"
          :calibration-label="calibrationLabel"
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
    <NewTraceWorkflow
      v-if="newTraceDestination && status"
      :status="status"
      :destination="newTraceDestination"
      :disabled="!canChangeMeasurements || busy"
      :create="instrument.createTrace"
      @close="newTraceDestination = null"
    />
    <CalibrationWorkflow
      v-if="calibrationWorkflowOpen && status?.calibration && !status.calibration.collecting"
      :status="status"
      :disabled="!canCalibrate || busy"
      :command="instrument.calibrate"
      @close="calibrationWorkflowOpen = false"
    />
  </main>
</template>
