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
  db: {
    label: 'dB Mag',
    unit: 'dB re 1 a.u.',
    shortUnit: 'dB',
    minimumSpan: 10,
    fallback: { min: -90, max: 10, referencePosition: 9 },
  },
  magnitude: {
    label: 'Lin Mag',
    unit: 'a.u.',
    shortUnit: 'a.u.',
    minimumSpan: 0.2,
    fallback: { min: 0, max: 2 },
  },
  phase: {
    label: 'Phase',
    unit: 'deg',
    shortUnit: '°',
    minimumSpan: 10,
    fallback: { min: -180, max: 180 },
  },
  real: {
    label: 'Real',
    unit: 'a.u.',
    shortUnit: 'a.u.',
    minimumSpan: 0.2,
    fallback: { min: -1, max: 1 },
  },
  imag: {
    label: 'Imag',
    unit: 'a.u.',
    shortUnit: 'a.u.',
    minimumSpan: 0.2,
    fallback: { min: -1, max: 1 },
  },
  realimag: {
    label: 'Real / Imag',
    unit: 'a.u.',
    shortUnit: 'a.u.',
    minimumSpan: 0.2,
    fallback: { min: -1, max: 1 },
  },
  swr: {
    label: 'SWR',
    unit: '1',
    shortUnit: '1',
    minimumSpan: 1,
    fallback: { min: 1, max: 11 },
  },
  unwrappedPhase: {
    label: 'Unwr Phase',
    unit: 'deg',
    shortUnit: '°',
    minimumSpan: 10,
    fallback: { min: -180, max: 180 },
  },
  logMagnitude: {
    label: 'Log Mag',
    unit: 'a.u.',
    shortUnit: 'a.u.',
    minimumSpan: 1,
    fallback: { min: 0.001, max: 10 },
  },
  delay: {
    label: 'Delay',
    unit: 's',
    shortUnit: 's',
    minimumSpan: 1e-9,
    fallback: { min: -1e-8, max: 1e-8 },
  },
  delayDerivation: {
    label: 'Delay Derivation',
    unit: 's²',
    shortUnit: 's²',
    minimumSpan: 1e-18,
    fallback: { min: -1e-17, max: 1e-17 },
  },
  polar: {
    label: 'Polar',
    unit: '1',
    shortUnit: '1',
    minimumSpan: 1,
    fallback: { min: -1, max: 1 },
  },
  invSmith: {
    label: 'Inv Smith',
    unit: '1',
    shortUnit: '1',
    minimumSpan: 1,
    fallback: { min: -1, max: 1 },
  },
  smith: {
    label: 'Smith',
    unit: '1',
    shortUnit: '1',
    minimumSpan: 1,
    fallback: { min: -1, max: 1 },
  },
}

export function numberText(value: number, significantDigits = 6): string {
  if (value === -Infinity) return '−∞'
  if (!Number.isFinite(value)) return '—'
  const normalized = Object.is(value, -0) ? 0 : value
  const rounded = Number(normalized.toPrecision(significantDigits))
  return rounded !== 0 && Math.abs(rounded) < 0.001
    ? rounded.toExponential()
    : rounded.toString()
}

export function frequencyText(value: number, significantDigits = 7): string {
  if (!Number.isFinite(value)) return '—'
  const magnitude = Math.abs(value)
  if (magnitude >= 1e9)
    return `${numberText(value / 1e9, significantDigits)} GHz`
  if (magnitude >= 1e6)
    return `${numberText(value / 1e6, significantDigits)} MHz`
  if (magnitude >= 1e3)
    return `${numberText(value / 1e3, significantDigits)} kHz`
  return `${numberText(value, significantDigits)} Hz`
}

/** 标记读数以工程单位和六位小数对齐；实际采样位置与高精度提示保持原值。 */
export function markerFrequencyText(value: number): string {
  if (!Number.isFinite(value)) return '—'
  const magnitude = Math.abs(value)
  if (magnitude >= 1e9) return `${(value / 1e9).toFixed(6)} GHz`
  if (magnitude >= 1e6) return `${(value / 1e6).toFixed(6)} MHz`
  if (magnitude >= 1e3) return `${(value / 1e3).toFixed(6)} kHz`
  return `${value.toFixed(6)} Hz`
}

/** 对数标记采用仪器常见的四位小数；高精度数值仍保留在提示和导出中。 */
export function markerValueText(value: number, format: DisplayFormat): string {
  if (!Number.isFinite(value) || format !== 'db') return numberText(value, 8)
  const rounded = Number(value.toFixed(4))
  return (Object.is(rounded, -0) ? 0 : rounded).toFixed(4)
}

/** 比值测量使用无量纲幅度；相位与对数值的显示单位保持度和 dB。 */
export function measurementFormatInfo(
  format: DisplayFormat,
  measurement?: Measurement,
): FormatDefinition {
  const definition = FORMAT_INFO[format]
  if (measurement?.kind !== 'sParameter') return definition
  if (format === 'db') return { ...definition, unit: 'dB', shortUnit: 'dB' }
  if (['phase', 'unwrappedPhase', 'delay', 'delayDerivation'].includes(format))
    return definition
  return { ...definition, unit: '1', shortUnit: '1' }
}

export function scaleValue(
  value: number,
  format: DisplayFormat,
  significantDigits = 5,
  measurement?: Measurement,
): string {
  return `${numberText(value, significantDigits)} ${measurementFormatInfo(format, measurement).shortUnit}`
}

/** 刻度设置按每格约 1% 的十进制分辨率显示，避免暴露自动缩放的浮点尾数。 */
export function scaleSettingNumber(value: number, perDivision: number): string {
  if (!Number.isFinite(value) || !(perDivision > 0) || !Number.isFinite(perDivision)) return numberText(value, 5)
  const resolution = 10 ** Math.floor(Math.log10(perDivision / 100))
  const rounded = Math.round(value / resolution) * resolution
  return numberText(Number.isFinite(rounded) ? rounded : value, 12)
}

export function scaleSettingText(value: number, perDivision: number, format: DisplayFormat, measurement?: Measurement): string {
  return format === 'logMagnitude' ? scaleValue(value, format, 5, measurement)
    : `${scaleSettingNumber(value, perDivision)} ${measurementFormatInfo(format, measurement).shortUnit}`
}
