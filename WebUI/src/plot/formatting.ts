import type { DisplayFormat, DisplayScale } from '../domain/presentation'
import type { Measurement } from '../api/types'

export interface FormatDefinition {
  label: string
  unit: string
  shortUnit: string
  minimumSpan: number
  fallback: DisplayScale
}

export const FORMAT_INFO: Record<DisplayFormat, FormatDefinition> = {
  db: { label: 'dB Mag', unit: 'dB re 1 a.u.', shortUnit: 'dB', minimumSpan: 10, fallback: { min: -90, max: 10, referencePosition: 9 } },
  magnitude: { label: 'Lin Mag', unit: 'a.u.', shortUnit: 'a.u.', minimumSpan: 0.2, fallback: { min: 0, max: 2 } },
  phase: { label: 'Phase', unit: 'deg', shortUnit: '°', minimumSpan: 10, fallback: { min: -180, max: 180 } },
  real: { label: 'Real', unit: 'a.u.', shortUnit: 'a.u.', minimumSpan: 0.2, fallback: { min: -1, max: 1 } },
  imag: { label: 'Imag', unit: 'a.u.', shortUnit: 'a.u.', minimumSpan: 0.2, fallback: { min: -1, max: 1 } },
  realimag: { label: 'Real / Imag', unit: 'a.u.', shortUnit: 'a.u.', minimumSpan: 0.2, fallback: { min: -1, max: 1 } },
}

export function numberText(value: number, significantDigits = 6): string {
  if (value === -Infinity) return '−∞'
  if (!Number.isFinite(value)) return '—'
  const normalized = Object.is(value, -0) ? 0 : value
  const rounded = Number(normalized.toPrecision(significantDigits))
  return rounded !== 0 && Math.abs(rounded) < 0.001 ? rounded.toExponential() : rounded.toString()
}

export function frequencyText(value: number, significantDigits = 7): string {
  if (!Number.isFinite(value)) return '—'
  const magnitude = Math.abs(value)
  if (magnitude >= 1e9) return `${numberText(value / 1e9, significantDigits)} GHz`
  if (magnitude >= 1e6) return `${numberText(value / 1e6, significantDigits)} MHz`
  if (magnitude >= 1e3) return `${numberText(value / 1e3, significantDigits)} kHz`
  return `${numberText(value, significantDigits)} Hz`
}

/** 比值测量使用无量纲幅度；相位与对数值的显示单位保持度和 dB。 */
export function measurementFormatInfo(format: DisplayFormat, measurement?: Measurement): FormatDefinition {
  const definition = FORMAT_INFO[format]
  if (measurement?.kind !== 'sParameter') return definition
  if (format === 'db') return { ...definition, unit: 'dB', shortUnit: 'dB' }
  if (format === 'phase') return definition
  return { ...definition, unit: '1', shortUnit: '1' }
}

export function scaleValue(value: number, format: DisplayFormat, significantDigits = 5, measurement?: Measurement): string {
  return `${numberText(value, significantDigits)} ${measurementFormatInfo(format, measurement).shortUnit}`
}
