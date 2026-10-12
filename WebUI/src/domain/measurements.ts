import type { Measurement } from '../api/types'

export type MeasurementChoice = 'S11' | 'S21' | 'S12' | 'S22' | 'RAW'

export function measurementState(measurement: Measurement): string {
  return measurement.calibration === 'uncalibrated' ? '未校准' : 'RAW'
}
