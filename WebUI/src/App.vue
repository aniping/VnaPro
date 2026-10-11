<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import InstrumentToolbar from './components/instrument/InstrumentToolbar.vue'
import HardkeyPanel from './components/instrument/HardkeyPanel.vue'
import InstrumentStatusBar from './components/instrument/InstrumentStatusBar.vue'
import DiagramWorkspace from './components/diagrams/DiagramWorkspace.vue'
import SofttoolHost from './components/softtools/SofttoolHost.vue'
import StimulusSofttool from './components/softtools/StimulusSofttool.vue'
import MeasurementSofttool from './components/softtools/MeasurementSofttool.vue'
import SweepSofttool from './components/softtools/SweepSofttool.vue'
import TraceSofttool from './components/softtools/TraceSofttool.vue'
import SystemSofttool from './components/softtools/SystemSofttool.vue'
import { useInstrument } from './composables/useInstrument'
import { useWorkspace } from './composables/useWorkspace'
import { exportVisibleFrameCsv } from './plot'

const instrument = useInstrument()
const { status, frame, connected, hasControl, canWrite, canConfigure, pendingOperation, writeInFlight,
  controlBusy, frameCondition, notice, error } = instrument
const measurements = computed(() => status.value?.measurements ?? [])
const workspace = useWorkspace(() => measurements.value, () => frame.value)
const { presentations, activeTraceName, activeMeasurement, activePresentation, activeIndex, layout, canAutoscale } = workspace
const { displayedNames, displayedMeasurements } = workspace
watch(displayedNames, names => instrument.setDisplayedMeasurements(names), { flush: 'sync' })
const displayedMeasurement = computed(() => frame.value?.traces.find(trace => trace.name === activeTraceName.value) ?? activeMeasurement.value)
const activeTool = ref('start')
const softtoolVisible = ref(true)
const canvasStyle = ref({ left: '0px', top: '0px', transform: 'scale(1)' })
const busy = computed(() => Boolean(pendingOperation.value || writeInFlight.value || status.value?.pending))
const group = computed(() => ['start', 'stop', 'center', 'span', 'bandwidth'].includes(activeTool.value) ? 'stimulus'
  : activeTool.value === 'meas' ? 'measurement'
    : ['format', 'scale', 'marker', 'trace-config'].includes(activeTool.value) ? 'trace'
      : ['sweep', 'trigger'].includes(activeTool.value) ? 'sweep' : 'system')
const focusField = computed(() => group.value === 'stimulus' ? activeTool.value as 'start' | 'stop' | 'center' | 'span' | 'bandwidth' : undefined)
const title = computed(() => ({ stimulus: 'Stimulus', measurement: 'Measurement', trace: 'Trace', sweep: 'Sweep', system: 'System' }[group.value]))
const tabs = computed(() => group.value === 'stimulus' ? [{ id: 'start', label: 'Frequency' }, { id: 'bandwidth', label: 'Bandwidth' }]
  : group.value === 'measurement' ? [{ id: 'meas', label: 'Measure' }]
    : group.value === 'trace' ? [{ id: 'format', label: 'Format' }, { id: 'scale', label: 'Scale' }, { id: 'marker', label: 'Marker' }, { id: 'trace-config', label: 'Config' }]
      : group.value === 'sweep' ? [{ id: 'sweep', label: 'Sweep' }, { id: 'trigger', label: 'Trigger' }]
        : [{ id: 'setup', label: 'State' }, { id: 'display', label: 'Display' }, { id: 'file', label: 'File' }, { id: 'help', label: 'Help' }])
const activeTab = computed(() => group.value === 'stimulus' ? activeTool.value === 'bandwidth' ? 'bandwidth' : 'start'
  : activeTool.value === 'tools' ? 'file' : activeTool.value)
const emptyConfig = { startHz: 0, stopHz: 0, points: 0, ifBandwidthHz: 0 }
function selectTool(id: string) {
  activeTool.value = id
  softtoolVisible.value = true
  if (['start', 'stop', 'center', 'span', 'bandwidth'].includes(id)) {
    void nextTick(() => {
      const input = document.getElementById(`stimulus-${id}`)
      input?.focus()
      if (input instanceof HTMLInputElement) input.select()
    })
  }
}
function fitCanvas() {
  const scale = Math.min(window.innerWidth / 1280, window.innerHeight / 800)
  canvasStyle.value = { left: `${(window.innerWidth - 1280 * scale) / 2}px`, top: `${(window.innerHeight - 800 * scale) / 2}px`, transform: `scale(${scale})` }
}
function keydown(event: KeyboardEvent) {
  if (event.key === 'Escape') softtoolVisible.value = false
}
function exportCsv() {
  if (!frame.value) return
  const csv = exportVisibleFrameCsv(frame.value, presentations)
  const url = URL.createObjectURL(new Blob([csv], { type: 'text/csv;charset=utf-8' }))
  const link = document.createElement('a')
  link.href = url
  link.download = `vnapro-frame-${frame.value.frameSeq}.csv`
  link.click()
  setTimeout(() => URL.revokeObjectURL(url), 1000)
}
async function deleteActive() {
  if (canConfigure.value && status.value && activeMeasurement.value && measurements.value.length > 1) {
    await instrument.deleteMeasurement(activeMeasurement.value.name, status.value.revision)
  }
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
    <div class="instrument-canvas" :style="{ position: 'absolute', ...canvasStyle }">
      <div class="instrument-shell">
        <div class="instrument-workspace" :class="{ 'softtool-hidden': !softtoolVisible }">
          <section class="measurement-stage" aria-label="Measurement display">
            <InstrumentToolbar :layout="layout" :has-trace="Boolean(activeMeasurement)" :can-autoscale="canAutoscale" :marker-enabled="activePresentation?.marker.enabled ?? false" :can-export="Boolean(frame)" :can-write="canWrite" :busy="busy" :can-delete="canConfigure && measurements.length > 1"
              @select-tool="selectTool" @toggle-layout="layout = layout === 'grid' ? 'maximized' : 'grid'" @autoscale="workspace.autoscale"
              @toggle-marker="workspace.setMarker({ enabled: !activePresentation?.marker.enabled })" @export-csv="exportCsv" @single="instrument.run('single')"
              @add-trace="selectTool('meas')" @delete-trace="deleteActive" />
            <DiagramWorkspace :frame="frame" :measurements="displayedMeasurements" :presentations="presentations" :active-trace-name="activeTraceName" :layout="layout"
              :config="status?.config ?? emptyConfig" :frame-condition="frameCondition" @select-trace="workspace.selectTrace" @toggle-visibility="workspace.toggleVisibility"
              @set-layout="layout = $event" @add-marker="workspace.setMarker({ enabled: true })"
              @move-marker="workspace.setMarker({ enabled: true, frequencyHz: $event.frequencyHz, sampleIndex: $event.sampleIndex }, $event.name)" />
          </section>
          <SofttoolHost v-show="softtoolVisible" :title="title" :tabs="tabs" :active-tab="activeTab" @select="selectTool" @close="softtoolVisible = false">
            <StimulusSofttool v-show="group === 'stimulus'" :status="status" :can-configure="canConfigure" :busy="busy" :focus-field="focusField" :apply="instrument.configure" />
            <MeasurementSofttool v-show="group === 'measurement'" :status="status" :can-configure="canConfigure" :busy="busy" :active-trace-name="activeTraceName" :displayed-names="displayedNames" :add="instrument.addMeasurement" :remove="instrument.deleteMeasurement" @select-trace="workspace.selectTrace" @display="workspace.setDisplayed($event.name, $event.enabled)" />
            <TraceSofttool v-if="group === 'trace'" :mode="activeTool as 'format' | 'scale' | 'marker' | 'trace-config'" :measurement="activeTool === 'trace-config' ? activeMeasurement : displayedMeasurement" :presentation="activePresentation" :frame="frame" :index="activeIndex" :layout="layout" :can-autoscale="canAutoscale"
              @format="workspace.setFormat" @scale="workspace.setScale" @smith-radius="workspace.setSmithRadius" @autoscale="workspace.autoscale" @marker="workspace.setMarker" @toggle-visibility="workspace.toggleVisibility()" />
            <SweepSofttool v-show="group === 'sweep'" :status="status" :can-write="canWrite" :busy="busy" :action="instrument.run" />
            <SystemSofttool v-if="group === 'system'" :mode="activeTool" :status="status" :frame="frame" :layout="layout" :connected="connected" :has-control="hasControl" :control-busy="controlBusy"
              @layout="layout = $event" @export="exportCsv" @acquire="instrument.claim()" @release="instrument.release()" />
          </SofttoolHost>
          <HardkeyPanel :active-key="softtoolVisible ? activeTool : ''" :has-trace="Boolean(activeMeasurement)" @select="selectTool" />
        </div>
        <InstrumentStatusBar :connected="connected" :core-state="status?.state ?? ''" :mode="status?.config.sweepMode ?? ''" :controller="status?.control.owner ?? 'none'"
          :has-control="hasControl" :claim-busy="controlBusy" :frame-seq="frame?.frameSeq ?? null" :run-id="status?.runId ?? null" :revision="status?.revision ?? null"
          :notice="pendingOperation ? `${pendingOperation.label} · ${pendingOperation.readingBack ? 'Readback' : 'Pending'}` : notice?.text ?? ''" :notice-kind="notice?.tone ?? 'info'" :error="error ?? undefined"
          @select-tool="selectTool" @acquire-control="instrument.claim()" @release-control="instrument.release()" />
      </div>
    </div>
  </main>
</template>
