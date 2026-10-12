import type { AcquiredFrame, Measurement } from '../api/types'
import type {
  DisplayFormat,
  DisplayScale,
  TracePresentation,
} from '../domain/presentation'
import { FORMAT_INFO, measurementFormatInfo, numberText } from './formatting'
import { markerResult } from './traceMath'
import { smithReadout } from './smithMath'

export { exportVisibleFrameCsv } from './exportCsv'
export {
  FORMAT_INFO,
  measurementFormatInfo,
  frequencyText,
  numberText,
  scaleValue,
} from './formatting'
export { referenceLevel } from './traceMath'

export function displayUnit(
  format: DisplayFormat,
  measurement?: Measurement,
): string {
  return measurementFormatInfo(format, measurement).unit
}

export function getTraceScale(
  _frame: AcquiredFrame | null,
  _measurement: Measurement | undefined,
  presentation: TracePresentation | undefined,
): DisplayScale {
  if (!presentation) return { ...FORMAT_INFO.db.fallback }
  return presentation.scale
}

export function getMarkerReadout(
  frame: AcquiredFrame | null,
  measurement: Measurement | undefined,
  presentation: TracePresentation | undefined,
): {
  frequencyHz: number
  index: number | null
  value: number
  valueText: string
  real: number | null
  imag: number | null
  valid: boolean
  interpolated: boolean
} | null {
  if (!presentation?.marker.enabled || !measurement) return null
  const trace = frame?.traces.find((item) => item.name === measurement.name)
  const result = markerResult(
    frame,
    trace,
    presentation.marker.frequencyHz,
    presentation.format,
    presentation.marker.sampleIndex,
    presentation.activeMarkerId,
  )
  if (!result) return null
  const displayed = trace ?? measurement
  const suffix =
    presentation.format === 'realimag'
      ? ` / ${numberText(result.secondaryValue, 8)} ${displayed.unit}`
      : ''
  return {
    frequencyHz: result.frequencyHz,
    index: result.index,
    value: result.value,
    valueText: !result.valid
      ? 'Invalid point'
      : presentation.format === 'smith'
        ? smithReadout(result)
        : `${numberText(result.value, 8)} ${measurementFormatInfo(presentation.format, displayed).shortUnit}${suffix}`,
    real: result.real,
    imag: result.imaginary,
    valid: result.valid,
    interpolated: result.interpolated,
  }
}
