import { computed, reactive, ref, watch } from 'vue'
import type { AcquiredFrame, Measurement } from '../api/types'
import { defaultTracePresentation } from '../domain/presentation'
import type { DisplayFormat, DisplayScale, LayoutMode, TracePresentation } from '../domain/presentation'
import { FORMAT_INFO } from '../plot/formatting'
import { displaySeries, traceScale } from '../plot/traceMath'
import { smithAutoscale, supportsSmith, validSmithRadius } from '../plot/smithMath'

/** 显示设置归各条轨迹独立管理，不写入采集配置。 */
export function useWorkspace(measurements: () => Measurement[], frame: () => AcquiredFrame | null) {
  const presentations = reactive<Record<string, TracePresentation>>({})
  const activeTraceName = ref('')
  const layout = ref<LayoutMode>('single')
  const displayedNames = ref<string[]>([])
  let displayInitialized = false
  watch(measurements, catalog => {
    for (const [index, trace] of catalog.entries()) {
      presentations[trace.name] ??= defaultTracePresentation(index)
      const presentation = presentations[trace.name]!
      if (presentation.format === 'smith' && !supportsSmith(trace)) presentation.format = presentation.cartesianFormat
    }
    for (const name of Object.keys(presentations)) {
      if (!catalog.some(trace => trace.name === name)) delete presentations[name]
    }
    if (!displayInitialized && catalog.length) {
      displayedNames.value = catalog.slice(0, 4).map(trace => trace.name)
      displayInitialized = true
    } else {
      displayedNames.value = displayedNames.value.filter(name => catalog.some(trace => trace.name === name))
    }
    if (!displayedNames.value.includes(activeTraceName.value)) {
      activeTraceName.value = displayedNames.value[0] ?? ''
    }
  }, { immediate: true })
  const activeMeasurement = computed(() => measurements().find(trace => trace.name === activeTraceName.value))
  const activePresentation = computed(() => presentations[activeTraceName.value])
  const activeIndex = computed(() => displayedNames.value.indexOf(activeTraceName.value))
  const displayedMeasurements = computed(() => displayedNames.value.flatMap(name => {
    const measurement = measurements().find(trace => trace.name === name)
    return measurement ? [measurement] : []
  }))
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
  const smithAutoscaleRadius = computed(() => smithAutoscale(activeFrameTrace.value))
  const canAutoscale = computed(() => activePresentation.value?.format === 'smith'
    ? smithAutoscaleRadius.value !== null : autoscaleRange.value !== null)
  function selectTrace(name: string) {
    if (!measurements().some(trace => trace.name === name)) return
    if (!displayedNames.value.includes(name)) {
      const next = [...displayedNames.value]
      if (next.length < 4) next.push(name)
      else next[Math.max(0, next.indexOf(activeTraceName.value))] = name
      displayedNames.value = next
    }
    activeTraceName.value = name
  }
  function setDisplayed(name: string, enabled: boolean) {
    if (enabled) {
      if (displayedNames.value.length < 4) selectTrace(name)
    } else {
      displayedNames.value = displayedNames.value.filter(item => item !== name)
      if (activeTraceName.value === name) activeTraceName.value = displayedNames.value[0] ?? ''
    }
  }
  function setFormat(format: DisplayFormat) {
    const trace = activePresentation.value
    if (!trace) return
    if (format === 'smith') {
      if (supportsSmith(activeMeasurement.value)) trace.format = format
      return
    }
    const restore = trace.format === 'smith' && trace.cartesianFormat === format
    trace.format = format
    trace.cartesianFormat = format
    if (!restore) trace.scale = { ...FORMAT_INFO[format].fallback }
  }
  function setScale(scale: DisplayScale) {
    if (activePresentation.value) activePresentation.value.scale = { ...scale }
  }
  /** 仅按点击时的显示帧适配一次；后续采集帧不能改变已应用的刻度。 */
  function autoscale() {
    if (activePresentation.value?.format === 'smith') {
      if (smithAutoscaleRadius.value !== null) activePresentation.value.smithRadius = smithAutoscaleRadius.value
    } else if (autoscaleRange.value) setScale(autoscaleRange.value)
  }
  function setSmithRadius(radius: number) {
    if (activePresentation.value && validSmithRadius(radius)) activePresentation.value.smithRadius = radius
  }
  function setMarker(value: { enabled?: boolean; frequencyHz?: number; sampleIndex?: number }, name = activeTraceName.value) {
    const marker = presentations[name]?.marker
    if (!marker) return
    if (value.enabled !== undefined) marker.enabled = value.enabled
    if (value.frequencyHz !== undefined && Number.isFinite(value.frequencyHz)) {
      marker.frequencyHz = value.frequencyHz
      marker.sampleIndex = value.sampleIndex !== undefined && Number.isInteger(value.sampleIndex)
        && value.sampleIndex >= 0 ? value.sampleIndex : null
    }
  }
  function toggleVisibility(name = activeTraceName.value) {
    const trace = presentations[name]
    if (trace) trace.visible = !trace.visible
  }
  return { presentations, activeTraceName, layout, activeMeasurement, activePresentation, activeIndex,
    displayedNames, displayedMeasurements, canAutoscale, selectTrace, setDisplayed, setFormat, setScale,
    setSmithRadius, autoscale, setMarker, toggleVisibility }
}
