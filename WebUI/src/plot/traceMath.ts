import type { AcquiredFrame } from '../api/types'
import type { DisplayFormat, DisplayScale } from '../domain/presentation'
import { FORMAT_INFO } from './formatting'

export type FrameTrace = AcquiredFrame['traces'][number]
export interface DisplaySeries { values: number[]; component: 'primary' | 'imaginary' }

export function validAt(trace: FrameTrace | undefined, index: number): boolean {
  return Boolean(trace?.available && trace.valid[index] === true
    && typeof trace.real[index] === 'number' && Number.isFinite(trace.real[index])
    && typeof trace.imag[index] === 'number' && Number.isFinite(trace.imag[index]))
}

export function displayValue(real: number, imaginary: number, format: DisplayFormat): number {
  if (!Number.isFinite(real) || !Number.isFinite(imaginary)) return NaN
  switch (format) {
    case 'db': {
      // 先缩放复数分量，避免可表示的 dB 因中间线性幅度溢出而丢失。
      const maximum = Math.max(Math.abs(real), Math.abs(imaginary))
      if (maximum === 0) return -Infinity
      const scaledReal = real / maximum
      const scaledImaginary = imaginary / maximum
      return 20 * (Math.log10(maximum) + 0.5 * Math.log10(scaledReal ** 2 + scaledImaginary ** 2))
    }
    case 'magnitude': return Math.hypot(real, imaginary)
    case 'phase': return real === 0 && imaginary === 0 ? NaN : Math.atan2(imaginary, real) * 180 / Math.PI
    case 'imag': return imaginary
    default: return real
  }
}

export function displaySeries(trace: FrameTrace | undefined, format: DisplayFormat): DisplaySeries[] {
  if (!trace) return []
  const primary = trace.real.map((real, index) => validAt(trace, index)
    ? displayValue(real!, trace.imag[index]!, format) : NaN)
  if (format !== 'realimag') return [{ values: primary, component: 'primary' }]
  return [
    { values: primary, component: 'primary' },
    { values: trace.imag.map((imaginary, index) => validAt(trace, index) ? imaginary! : NaN), component: 'imaginary' },
  ]
}

function niceCeiling(value: number): number {
  const power = 10 ** Math.floor(Math.log10(value))
  const normalized = value / power
  const step = [1, 2, 2.5, 5, 10].find(candidate => normalized <= candidate) ?? 10
  return step * power
}

/** 主动执行自动缩放时按真实采样值适配范围；初始显示采用常规仪器刻度。 */
export function traceScale(trace: FrameTrace | undefined, format: DisplayFormat, requested: DisplayScale | null): DisplayScale {
  if (requested && Number.isFinite(requested.min) && Number.isFinite(requested.max) && requested.max > requested.min) {
    return { ...requested }
  }
  let minimum = Infinity
  let maximum = -Infinity
  for (const series of displaySeries(trace, format)) {
    for (const value of series.values) {
      if (!Number.isFinite(value)) continue
      minimum = Math.min(minimum, value)
      maximum = Math.max(maximum, value)
    }
  }
  if (!Number.isFinite(minimum)) return { ...FORMAT_INFO[format].fallback }
  if (maximum === minimum) {
    const span = FORMAT_INFO[format].minimumSpan
    const center = minimum
    return { min: center - span / 2, max: center + span / 2 }
  }
  const span = (maximum - minimum) * 1.25
  const division = niceCeiling(span / 10)
  const center = (minimum + maximum) / 2
  const min = Math.floor((center - division * 5) / division) * division
  return { min, max: min + division * 10 }
}

/** 细微变化用相对参考值的刻度显示；该判断不触发自动缩放。 */
export function usesRelativeAxis(scale: DisplayScale): boolean {
  const span = scale.max - scale.min
  return span < 0.001 || span < Math.abs(referenceLevel(scale)) * 0.0001
}

export function referenceLevel(scale: DisplayScale): number {
  const requested = scale.referencePosition ?? 8
  const position = Number.isFinite(requested) ? Math.max(0, Math.min(10, requested)) : 8
  return scale.min + (scale.max - scale.min) * position / 10
}

/** 依据已提供的采样点进行吸附，不重建缺失或非均匀分布的原始采样点。 */
export function nearestIndex(axis: readonly number[], requested: number | null): number {
  if (axis.length === 0) return -1
  if (requested === null || !Number.isFinite(requested)) return Math.floor((axis.length - 1) / 2)
  let low = 0
  let high = axis.length - 1
  while (low < high) {
    const middle = Math.floor((low + high) / 2)
    if (axis[middle]! < requested) low = middle + 1
    else high = middle
  }
  if (low > 0 && requested - axis[low - 1]! <= axis[low]! - requested) return low - 1
  return low
}

export function normalizedX(frequencyHz: number, minimum: number, maximum: number): number {
  return maximum > minimum ? (frequencyHz - minimum) / (maximum - minimum) : 0.5
}

export function normalizedY(value: number, scale: DisplayScale): number {
  return (scale.max - value) / (scale.max - scale.min)
}

export interface MarkerResult {
  index: number
  frequencyHz: number
  valid: boolean
  value: number
  imaginary: number | null
  real: number | null
}

export function markerResult(frame: AcquiredFrame | null, trace: FrameTrace | undefined,
  frequencyHz: number | null, format: DisplayFormat): MarkerResult | null {
  if (!frame || !trace) return null
  const index = nearestIndex(frame.frequencyHz, frequencyHz)
  if (index < 0) return null
  const valid = validAt(trace, index)
  return {
    index, frequencyHz: frame.frequencyHz[index]!, valid,
    value: valid ? displayValue(trace.real[index]!, trace.imag[index]!, format) : NaN,
    imaginary: valid ? trace.imag[index]! : null,
    real: valid ? trace.real[index]! : null,
  }
}
