import type { AcquiredFrame, Measurement } from '../api/types'
import type { DisplayFormat, DisplayScale, TracePresentation } from '../domain/presentation'
import { FORMAT_INFO, measurementFormatInfo, numberText } from './formatting'
import { markerResult, traceScale } from './traceMath'

export { exportVisibleFrameCsv } from './exportCsv'
export { FORMAT_INFO, measurementFormatInfo, frequencyText, numberText, scaleValue } from './formatting'
export { referenceLevel, traceScale } from './traceMath'

export function displayUnit(format: DisplayFormat, measurement?: Measurement): string {
  return measurementFormatInfo(format, measurement).unit
}

export function getTraceScale(frame: AcquiredFrame | null, measurement: Measurement | undefined,
  presentation: TracePresentation | undefined): DisplayScale {
  if (!presentation) return { ...FORMAT_INFO.db.fallback }
  return traceScale(frame?.traces.find(trace => trace.name === measurement?.name), presentation.format, presentation.scale)
}

export function getMarkerReadout(frame: AcquiredFrame | null, measurement: Measurement | undefined,
  presentation: TracePresentation | undefined): {
    frequencyHz: number; index: number; value: number; valueText: string; real: number | null; imag: number | null; valid: boolean
  } | null {
  if (!presentation?.marker.enabled || !measurement) return null
  const trace = frame?.traces.find(item => item.name === measurement.name)
  const result = markerResult(frame, trace, presentation.marker.frequencyHz, presentation.format)
  if (!result) return null
  const displayed = trace ?? measurement
  const suffix = presentation.format === 'realimag' ? ` / ${numberText(result.imaginary ?? NaN, 8)} ${displayed.unit}` : ''
  return { frequencyHz: result.frequencyHz, index: result.index, value: result.value,
    valueText: result.valid ? `${numberText(result.value, 8)} ${measurementFormatInfo(presentation.format, displayed).shortUnit}${suffix}` : 'Invalid point',
    real: result.real, imag: result.imaginary, valid: result.valid }
}
