import type { AcquiredFrame } from '../api/types'
import type { DisplayFormat, DisplayScale } from '../domain/presentation'

export type FrameTrace = AcquiredFrame['traces'][number]
export interface DisplaySeries {
  values: number[]
  component: 'primary' | 'imaginary'
}

export function validAt(trace: FrameTrace | undefined, index: number): boolean {
  return Boolean(
    trace?.available &&
      trace.valid[index] === true &&
      typeof trace.real[index] === 'number' &&
      Number.isFinite(trace.real[index]) &&
      typeof trace.imag[index] === 'number' &&
      Number.isFinite(trace.imag[index]),
  )
}

/** 格式变换由后端提供，这里只把无穷/无效状态映射为绘图库的数值占位。 */
export function projectedValue(value: number | null, state: string): number {
  return state === 'negativeInfinity'
    ? -Infinity
    : state === 'finite' && value !== null
      ? value
      : NaN
}

export function displaySeries(
  trace: FrameTrace | undefined,
  format: DisplayFormat,
): DisplaySeries[] {
  if (
    !trace?.available ||
    trace.projection.format !== format ||
    format === 'smith'
  )
    return []
  const projection = trace.projection
  const primary = projection.values.map((value, index) =>
    projectedValue(value, projection.valueStates[index]!),
  )
  if (format !== 'realimag') return [{ values: primary, component: 'primary' }]
  return [
    { values: primary, component: 'primary' },
    {
      values: projection.secondaryValues.map((value, index) =>
        projectedValue(value, projection.secondaryStates[index]!),
      ),
      component: 'imaginary',
    },
  ]
}

/** 细微变化用相对参考值的刻度显示；该判断不触发自动缩放。 */
export function usesRelativeAxis(scale: DisplayScale): boolean {
  const span = scale.max - scale.min
  return span < 0.001 || span < Math.abs(referenceLevel(scale)) * 0.0001
}

export function referenceLevel(scale: DisplayScale): number {
  const requested = scale.referencePosition ?? 8
  const position = Number.isFinite(requested)
    ? Math.max(0, Math.min(10, requested))
    : 8
  return scale.min + ((scale.max - scale.min) * position) / 10
}

export function normalizedX(
  frequencyHz: number,
  minimum: number,
  maximum: number,
): number {
  return maximum > minimum ? (frequencyHz - minimum) / (maximum - minimum) : 0.5
}

export function normalizedY(value: number, scale: DisplayScale): number {
  return (scale.max - value) / (scale.max - scale.min)
}

export interface MarkerResult {
  index: number | null
  frequencyHz: number
  valid: boolean
  value: number
  secondaryValue: number
  imaginary: number | null
  real: number | null
  impedanceReal: number | null
  impedanceImag: number | null
  impedanceState: 'finite' | 'open' | 'overflow' | 'invalid'
  interpolated: boolean
  reason: string
}

/** 标记可能位于抽取数组之外或两个频点之间，直接显示后端的独立结果。 */
export function markerResult(
  frame: AcquiredFrame | null,
  trace: FrameTrace | undefined,
  _frequencyHz: number | null,
  format: DisplayFormat,
  _sampleIndex?: number | null,
  markerId?: number,
): MarkerResult | null {
  if (!frame || !trace || trace.projection.format !== format) return null
  const marker =
    markerId === undefined || !trace.projection.markers
      ? trace.projection.marker
      : trace.projection.markers.find((marker) => marker.id === markerId)
  if (!marker) return null
  if (marker.frequencyHz === null) return null
  return {
    index: marker.sampleIndex,
    frequencyHz: marker.frequencyHz,
    valid: marker.available && marker.valid,
    value: projectedValue(marker.value, marker.valueState),
    secondaryValue: projectedValue(
      marker.secondaryValue,
      marker.secondaryState,
    ),
    imaginary: marker.imag,
    real: marker.real,
    impedanceReal: marker.impedanceReal,
    impedanceImag: marker.impedanceImag,
    impedanceState: marker.impedanceState,
    interpolated: marker.interpolated,
    reason: marker.reason,
  }
}
