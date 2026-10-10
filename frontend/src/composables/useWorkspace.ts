import { computed, reactive, ref, watch } from 'vue'
import type { Measurement } from '../api/types'
import { defaultTracePresentation } from '../domain/presentation'
import type { DisplayFormat, DisplayScale, LayoutMode, TracePresentation } from '../domain/presentation'
import { FORMAT_INFO } from '../plot/formatting'

/** Presentation belongs to individual traces; it never writes acquisition configuration. */
export function useWorkspace(measurements: () => Measurement[]) {
  const presentations = reactive<Record<string, TracePresentation>>({})
  const activeTraceName = ref('')
  const layout = ref<LayoutMode>('single')
  watch(measurements, catalog => {
    for (const [index, trace] of catalog.entries()) {
      presentations[trace.name] ??= defaultTracePresentation(index)
    }
    for (const name of Object.keys(presentations)) {
      if (!catalog.some(trace => trace.name === name)) delete presentations[name]
    }
    if (!catalog.slice(0, 4).some(trace => trace.name === activeTraceName.value)) {
      activeTraceName.value = catalog[0]?.name ?? ''
    }
  }, { immediate: true })
  const activeMeasurement = computed(() => measurements().find(trace => trace.name === activeTraceName.value))
  const activePresentation = computed(() => presentations[activeTraceName.value])
  const activeIndex = computed(() => measurements().findIndex(trace => trace.name === activeTraceName.value))
  function selectTrace(name: string) {
    if (measurements().slice(0, 4).some(trace => trace.name === name)) activeTraceName.value = name
  }
  function setFormat(format: DisplayFormat) {
    const trace = activePresentation.value
    if (!trace) return
    trace.format = format
    trace.scale = { ...FORMAT_INFO[format].fallback }
  }
  function setScale(scale: DisplayScale | null) {
    if (activePresentation.value) activePresentation.value.scale = scale
  }
  function setMarker(value: { enabled?: boolean; frequencyHz?: number }, name = activeTraceName.value) {
    const marker = presentations[name]?.marker
    if (!marker) return
    if (value.enabled !== undefined) marker.enabled = value.enabled
    if (value.frequencyHz !== undefined && Number.isFinite(value.frequencyHz)) marker.frequencyHz = value.frequencyHz
  }
  function toggleVisibility(name = activeTraceName.value) {
    const trace = presentations[name]
    if (trace) trace.visible = !trace.visible
  }
  return { presentations, activeTraceName, layout, activeMeasurement, activePresentation, activeIndex,
    selectTrace, setFormat, setScale, setMarker, toggleVisibility }
}
