import { computed, reactive, ref, watch } from 'vue'
import type { AcquiredFrame, Measurement } from '../api/types'
import { defaultTracePresentation } from '../domain/presentation'
import type { DisplayFormat, DisplayScale, LayoutMode, TracePresentation } from '../domain/presentation'
import { FORMAT_INFO } from '../plot/formatting'
import { displaySeries, traceScale } from '../plot/traceMath'

/** 显示设置归各条轨迹独立管理，不写入采集配置。 */
export function useWorkspace(measurements: () => Measurement[], frame: () => AcquiredFrame | null) {
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
  const activeFrameTrace = computed(() => frame()?.traces.find(trace => trace.name === activeTraceName.value))
  const autoscaleRange = computed(() => {
    const presentation = activePresentation.value
    const trace = activeFrameTrace.value
    if (!presentation || !displaySeries(trace, presentation.format)
      .some(series => series.values.some(Number.isFinite))) return null
    const range = traceScale(trace, presentation.format, null)
    return Number.isFinite(range.min) && Number.isFinite(range.max)
      && Number.isFinite(range.max - range.min) && range.max > range.min ? range : null
  })
  const canAutoscale = computed(() => autoscaleRange.value !== null)
  function selectTrace(name: string) {
    if (measurements().slice(0, 4).some(trace => trace.name === name)) activeTraceName.value = name
  }
  function setFormat(format: DisplayFormat) {
    const trace = activePresentation.value
    if (!trace) return
    trace.format = format
    trace.scale = { ...FORMAT_INFO[format].fallback }
  }
  function setScale(scale: DisplayScale) {
    if (activePresentation.value) activePresentation.value.scale = { ...scale }
  }
  /** 仅按点击时的显示帧适配一次；后续采集帧不能改变已应用的刻度。 */
  function autoscale() {
    if (autoscaleRange.value) setScale(autoscaleRange.value)
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
    canAutoscale, selectTrace, setFormat, setScale, autoscale, setMarker, toggleVisibility }
}
