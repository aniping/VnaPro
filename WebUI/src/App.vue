<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import InstrumentToolbar from './components/instrument/InstrumentToolbar.vue'
import HardkeyPanel from './components/instrument/HardkeyPanel.vue'
import InstrumentStatusBar from './components/instrument/InstrumentStatusBar.vue'
import DiagramWorkspace from './components/diagrams/DiagramWorkspace.vue'
import SofttoolHost from './components/softtools/SofttoolHost.vue'
import ChannelSettingsSofttool from './components/softtools/ChannelSettingsSofttool.vue'
import ChannelConfigSofttool from './components/softtools/ChannelConfigSofttool.vue'
import { MAX_CHANNELS } from './domain/channels'
import ZoomStimulusSofttool from './components/softtools/ZoomStimulusSofttool.vue'
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
import { applicabilityWarnings, calibrationStateLabel, calibrationTone } from './domain/calibrationApplicability'
import { isCircularFormat, MAX_DIAGRAMS, MAX_TRACES } from './domain/presentation'
import SystemSofttool from './components/softtools/SystemSofttool.vue'
import DisplaySofttool from './components/softtools/DisplaySofttool.vue'
import { useInstrument } from './composables/useInstrument'
import { useWorkspace } from './composables/useWorkspace'
import { enableMarkerOnEntry } from './composables/markerMenuEntry'
import { exportVisibleFrameCsv } from './plot'
import type { MarkerMove } from './plot/markerDrag'
import type { ReferencePositionMove } from './plot/referencePositionDrag'
import type { StimulusZoomSelection, TraceDestination } from './api/types'
import type { TracePanMove } from './plot/plotGestures'
import type { TrashIntent } from './domain/trashDrop'
import type { TraceMoveRequest } from './domain/diagramLayout'

const instrument = useInstrument()
const {
  status,
  frame,
  channelFrames,
  connected,
  hasControl,
  canConfigure,
  canChangeMeasurements,
  canCalibrate,
  canRun,
  canStopSweep,
  canCancelCalibration,
  canEditDisplay: canWriteDisplay,
  canContinueDisplayGesture,
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
const canContinueReferenceDrag = computed(() =>
  canContinueDisplayGesture.value && !calibrationAcquisitionVisible.value,
)
const calibrationLabel = computed(() => [calibrationSummary(status.value?.calibration), status.value?.calibration?.reason, ...applicabilityWarnings(status.value?.calibration?.applicability)].filter(Boolean).join(' · '))
const calibrationState = computed(() => `${calibrationStateLabel(status.value?.calibration)}${status.value?.calibration?.applicability?.warnings.includes('ifbwChanged') ? ' · IFBW 变化' : ''}`)
const calibrationStateTone = computed(() => calibrationTone(status.value?.calibration))
const canvasStyle = ref({ left: '0px', top: '0px', transform: 'scale(1)' })
const busy = computed(() =>
  Boolean(
    pendingOperation.value || writeInFlight.value || status.value?.pending,
  ),
)
const group = computed(() =>
  ['start', 'stop', 'center', 'span', 'stimulus-zoom', 'stimulus-power'].includes(activeTool.value)
    ? 'stimulus'
    : ['meas', 'meas-wave'].includes(activeTool.value)
      ? 'measurement'
      : ['format', 'scale', 'marker', 'trace-config'].includes(activeTool.value)
        ? 'trace'
        : ['sweep', 'sweep-control', 'trigger'].includes(activeTool.value)
          ? 'sweep'
          : ['cal', 'cal-use'].includes(activeTool.value)
            ? 'calibration'
            : activeTool.value === 'channel-config' ? 'channel'
            : ['bandwidth', 'power'].includes(activeTool.value)
              ? 'bandwidth'
              : 'system',
)
const focusField = computed(() =>
  group.value === 'stimulus' && ['start', 'stop', 'center', 'span'].includes(activeTool.value)
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
      channel: 'Channel Config',
      system: 'System',
    })[group.value],
)
const tabs = computed(() =>
  group.value === 'stimulus'
    ? [{ id: 'start', label: 'Stimulus' }, { id: 'stimulus-power', label: 'Power' }, { id: 'stimulus-zoom', label: 'Zoom Stimulus' }]
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
              { id: 'trigger', label: 'Trigger In' },
            ]
          : group.value === 'calibration'
            ? [
                { id: 'cal', label: 'Start Cal' },
                { id: 'cal-use', label: 'Use Cal' },
              ]
            : group.value === 'bandwidth'
              ? [{ id: 'power', label: 'Power' }, { id: 'bandwidth', label: 'Bandwidth' }]
              : group.value === 'channel' ? [{ id: 'channel-config', label: 'Channels' }]
              : [
                  { id: 'setup', label: 'State' },
                  { id: 'display', label: 'Display' },
                  { id: 'file', label: 'File' },
                  { id: 'help', label: 'Help' },
                ],
)
const activeTab = computed(() =>
  group.value === 'stimulus'
    ? ['stimulus-zoom', 'stimulus-power'].includes(activeTool.value) ? activeTool.value : 'start'
    : activeTool.value === 'tools'
      ? 'file'
      : activeTool.value,
)
const emptyConfig = { startHz: 0, stopHz: 0, points: 0, ifBandwidthHz: 0 }
const zoomSelecting = ref(false)
const graphicalZoomSelecting = ref(false)
const graphicalZoomed = ref<Record<number, boolean>>({})
const graphicalZoomResets = ref<Record<number, number>>({})
const activeGraphicalZoom = computed(() => Boolean(graphicalZoomed.value[status.value?.display.selectedDiagram ?? 0]))
const canGraphicalZoom = computed(() => Boolean(connected.value && !busy.value && !calibrationAcquisitionVisible.value &&
  frame.value && frameCondition.value.kind === 'current' && activePresentation.value?.visible &&
  !isCircularFormat(activePresentation.value.format) && status.value &&
  status.value.config.startHz < status.value.config.stopHz))
const graphicalZoomReason = computed(() => canGraphicalZoom.value
  ? 'Zoom Select · 框选放大显示区域，扫频与测量数据保持不变'
  : 'Zoom Select · 请在当前完整帧的可见直角坐标图中操作')
const canZoom = computed(() => Boolean(canConfigure.value && canEditDisplay.value &&
  !activeGraphicalZoom.value && status.value?.stimulusZoom?.available && activePresentation.value?.visible &&
  !isCircularFormat(activePresentation.value.format) && (!frame.value || frameCondition.value.kind === 'current')))
const canZoomOut = computed(() => Boolean(canConfigure.value && status.value?.stimulusZoom?.canZoomOut))
const zoomReason = computed(() => !status.value?.stimulusZoom
  ? '当前服务未提供刺激缩放，请更新服务。'
  : activeGraphicalZoom.value ? '请先点击工具栏 1:1 复位显示缩放。'
  : !canConfigure.value ? '请取得控制权并等待当前操作完成。'
  : !activePresentation.value?.visible || isCircularFormat(activePresentation.value.format)
    ? '请选择可见的矩形坐标迹线。'
    : frame.value && frameCondition.value.kind !== 'current' ? '请等待当前扫频范围的数据。'
    : status.value.stimulusZoom.reason)
watch([() => status.value?.revision, () => status.value?.display.revision, activeTraceName, group], () => { zoomSelecting.value = false })
watch([() => status.value?.revision, () => status.value?.display.revision,
  () => status.value?.display.selectedDiagram, activeTraceName, activeTool], () => { graphicalZoomSelecting.value = false })
function chooseZoom() {
  if (!canZoom.value) return
  graphicalZoomSelecting.value = false
  zoomSelecting.value = !zoomSelecting.value
}
function chooseGraphicalZoom() {
  if (!canGraphicalZoom.value) return
  zoomSelecting.value = false
  graphicalZoomSelecting.value = !graphicalZoomSelecting.value
}
function resetGraphicalZoom() {
  const diagram = status.value?.display.selectedDiagram
  if (!diagram) return
  graphicalZoomSelecting.value = false
  graphicalZoomResets.value = { ...graphicalZoomResets.value, [diagram]: (graphicalZoomResets.value[diagram] ?? 0) + 1 }
}
function graphicalZoomChanged(value: { diagram: number; zoomed: boolean }) {
  graphicalZoomed.value = { ...graphicalZoomed.value, [value.diagram]: value.zoomed }
}
async function commitStimulusZoom(value: StimulusZoomSelection): Promise<boolean> {
  zoomSelecting.value = false
  if (!canZoom.value) return false
  const { revision, displayRevision, ...rectangle } = value
  return Boolean(await instrument.stimulusZoom({ action: 'apply', ...rectangle }, revision, displayRevision))
}
async function zoomOut(): Promise<boolean> {
  zoomSelecting.value = false
  const snapshot = status.value
  return Boolean(snapshot && canZoomOut.value && await instrument.stimulusZoom(
    { action: 'out' }, snapshot.revision, snapshot.display.revision))
}
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
  if (event.defaultPrevented) return
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
  link.download = `vnapro-ch${frame.value.channelId}-frame-${frame.value.frameSeq}.csv`
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
async function deleteTrash(value: TrashIntent): Promise<boolean> {
  const snapshot = status.value
  if (!snapshot || busy.value || value.displayRevision !== snapshot.display.revision) return false
  if (value.kind === 'trace' || value.kind === 'diagram') {
    if (!canChangeMeasurements.value || value.revision !== snapshot.revision) return false
    if (value.kind === 'diagram')
      return Boolean(snapshot.display.diagrams.length > 1 && snapshot.display.diagrams.some(d => d.number === value.diagram) &&
        await instrument.deleteDiagram(value.diagram, value.revision, value.displayRevision))
    return Boolean(snapshot.measurements.length > 1 && snapshot.measurements.some(m => m.name === value.name) &&
      await instrument.deleteMeasurement(value.name, value.revision))
  }
  if (!canEditDisplay.value || !snapshot.measurements.some(m => m.name === value.name)) return false
  return Boolean(await instrument.display(value.kind === 'marker'
    ? { action: 'marker', traceName: value.name, markerId: value.markerId, enabled: false }
    : { action: 'marker', traceName: value.name, markerAction: 'allOff' }, value.displayRevision))
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

function openNewTrace(destination: TraceDestination) {
  if (
    !canChangeMeasurements.value ||
    busy.value ||
    !status.value ||
    measurements.value.length >= MAX_TRACES ||
    !status.value.display.diagrams.some((item) => item.number === destination.diagram)
  )
    return
  newTraceDestination.value = destination
}

function moveTrace(request: TraceMoveRequest) {
  if (!canEditDisplay.value || busy.value) return
  const { name, diagram, position, displayRevision } = request
  return instrument.display({ action: 'traceMove', traceName: name, diagram, position }, displayRevision)
}

async function copyActiveTrace() {
  if (
    canChangeMeasurements.value &&
    !busy.value &&
    measurements.value.length < MAX_TRACES
  ) {
    if (activeMeasurement.value) await instrument.cloneMeasurement(activeMeasurement.value.name)
    else await instrument.createTrace()
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
async function commitReferencePosition(
  value: ReferencePositionMove,
): Promise<boolean> {
  if (!canContinueReferenceDrag.value || busy.value) return false
  // 松手恰逢自动续租时等当前轮询完成，仍使用手势开始时的版本进行 CAS。
  if (controlBusy.value) await instrument.refresh()
  if (!canEditDisplay.value || busy.value) return false
  return Boolean(
    await instrument.display(
      {
        action: 'scale',
        traceName: value.name,
        referencePosition: value.referencePosition,
      },
      value.displayRevision,
    ),
  )
}
async function commitTracePan(value: TracePanMove): Promise<boolean> {
  if (!canContinueReferenceDrag.value || busy.value) return false
  if (controlBusy.value) await instrument.refresh()
  return Boolean(canEditDisplay.value && !busy.value && await instrument.display(
    { action: 'scale', traceName: value.name, referenceLevel: value.reference }, value.displayRevision))
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
              :sweep-supported="Boolean(status?.config.selectedSweepMode)"
              :can-graphical-zoom="canGraphicalZoom"
              :graphical-zoom-selecting="graphicalZoomSelecting"
              :can-reset-graphical-zoom="activeGraphicalZoom || graphicalZoomSelecting"
              :graphical-zoom-reason="graphicalZoomReason"
              :layout="layout"
              :has-trace="Boolean(activeMeasurement) && !calibrationAcquisitionVisible"
              :can-autoscale="canEditDisplay && canAutoscale"
              :can-edit-display="canEditDisplay"
              :marker-enabled="activePresentation?.marker.enabled ?? false"
              :can-export="Boolean(frame) && !calibrationAcquisitionVisible"
              :can-write="canRun"
              :busy="busy"
              :can-delete="canChangeMeasurements && measurements.length > 1"
              :revision="status?.revision"
              :display-revision="status?.display.revision"
              :can-add-trace="
                canChangeMeasurements && measurements.length < MAX_TRACES
              "
              :can-add-channel="canChangeMeasurements && Boolean(status?.channels) && (status?.channels?.length ?? MAX_CHANNELS) < MAX_CHANNELS && measurements.length < MAX_TRACES && (status?.display.diagrams.length ?? MAX_DIAGRAMS) < MAX_DIAGRAMS"
              @select-tool="selectTool"
              @choose-graphical-zoom="chooseGraphicalZoom"
              @reset-graphical-zoom="resetGraphicalZoom"
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
              @restart="instrument.sweepControl({ action: 'start' })"
              @add-trace="copyActiveTrace"
              @add-channel="instrument.channel({ action: 'new', newDiagram: true })"
              @delete-trace="deleteActive"
              @trash-drop="deleteTrash"
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
              :channel-frames="channelFrames"
              :channels="status?.channels"
              :active-channel-id="status?.activeChannelId"
              :connected="connected"
              :measurements="measurements"
              :presentations="presentations"
              :active-trace-name="activeTraceName"
              :selected-diagram="status?.display.selectedDiagram"
              :display-revision="status?.display.revision"
              :revision="status?.revision"
              :zoom-selecting="zoomSelecting"
              :graphical-zoom-selecting="graphicalZoomSelecting"
              :graphical-zoom-resets="graphicalZoomResets"
              :can-zoom="canZoom"
              :commit-stimulus-zoom="commitStimulusZoom"
              :commit-trace-pan="commitTracePan"
              :commit-trash="deleteTrash"
              @cancel-zoom="zoomSelecting = false"
              @cancel-graphical-zoom="graphicalZoomSelecting = false"
              @graphical-zoom-changed="graphicalZoomChanged"
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
              :can-continue-reference-drag="canContinueReferenceDrag"
              :commit-reference-position="commitReferencePosition"
              :info-table="status?.display.infoTable"
              :annotations="status?.display.annotations"
              @relocate-marker-info="
                workspace.setMarkerInfoLocation($event.name, $event.location, $event.area)
              "
              @select-trace="workspace.selectTrace"
              @toggle-visibility="workspace.toggleVisibility"
              @set-layout="workspace.setLayout($event.layout, $event.diagram)"
              @select-diagram="workspace.selectDiagram"
              @add-trace="addTrace($event)"
              @new-trace="openNewTrace"
              @move-trace="moveTrace"
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
            <ChannelConfigSofttool v-if="activeTool === 'channel-config'" :status="status" :can-write="canChangeMeasurements" :busy="busy" :perform="instrument.channel" />
            <ChannelSettingsSofttool
              v-show="group === 'stimulus' && !['stimulus-zoom', 'stimulus-power'].includes(activeTool)"
              mode="frequency"
              :status="status"
              :can-configure="canConfigure"
              :busy="busy"
              :focus-field="focusField"
              :apply="instrument.configure"
            />
            <ZoomStimulusSofttool
              v-if="activeTool === 'stimulus-zoom'"
              :can-zoom="canZoom"
              :can-zoom-out="canZoomOut"
              :busy="busy"
              :selecting="zoomSelecting"
              :reason="zoomReason"
              :choose="chooseZoom"
              :zoom-out="zoomOut"
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
              v-show="activeTool === 'bandwidth'"
              mode="bandwidth"
              :status="status"
              :can-configure="canConfigure"
              :busy="busy"
              :apply="instrument.configure"
            />
            <ChannelSettingsSofttool v-show="activeTool === 'power' || activeTool === 'stimulus-power'" mode="power" :status="status" :can-configure="canConfigure" :busy="busy" :apply="instrument.configure" />
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
              :can-stop="canStopSweep"
              :busy="busy"
              :action="instrument.sweepControl"
            />
            <CalibrationSofttool
              v-if="group === 'calibration'"
              :mode="activeTool as 'cal' | 'cal-use'"
              :status="status"
              :can-calibrate="canCalibrate"
              :busy="busy"
              :command="instrument.calibrate"
              :operation="pendingOperation"
              :operation-error="error"
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
          :active-channel-id="status?.activeChannelId"
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
          :calibration-state="calibrationState"
          :calibration-tone="calibrationStateTone"
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
