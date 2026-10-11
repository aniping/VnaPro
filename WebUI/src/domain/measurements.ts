import type { Measurement, MeasurementCandidate } from '../api/types'

export type MeasurementChoice = 'S11' | 'S21' | 'RAW'

/** 选择 S 参数时固定使用端口 1 激励；RAW 保留用户选择的接收波和端口。 */
export function measurementCandidate(choice: MeasurementChoice, draft: MeasurementCandidate): MeasurementCandidate {
  if (choice === 'RAW') return { ...draft, kind: 'receiver' }
  return {
    name: draft.name,
    kind: 'sParameter',
    receiverKind: 'b',
    receiverPort: choice === 'S11' ? 1 : 2,
    sourcePort: 1,
  }
}

export function measurementState(measurement: Measurement): string {
  return measurement.calibration === 'uncalibrated' ? '未校准' : 'RAW'
}
