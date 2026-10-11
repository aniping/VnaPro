import type { Measurement, MeasurementCandidate } from '../api/types'

export type MeasurementChoice = 'S11' | 'S21' | 'S12' | 'S22' | 'RAW'

/** Sij 的接收端为 i、激励端为 j；RAW 保留用户选择的接收波和端口。 */
export function measurementCandidate(choice: MeasurementChoice, draft: MeasurementCandidate): MeasurementCandidate {
  if (choice === 'RAW') return { ...draft, kind: 'receiver' }
  return {
    name: draft.name,
    kind: 'sParameter',
    receiverKind: 'b',
    receiverPort: Number(choice[1]),
    sourcePort: Number(choice[2]),
  }
}

export function measurementState(measurement: Measurement): string {
  return measurement.calibration === 'uncalibrated' ? '未校准' : 'RAW'
}

/** 新迹线复制活动测量，名称取目录内尚未使用的 Trc<n>。 */
export function cloneMeasurementCandidate(active: Measurement, catalog: Measurement[]): MeasurementCandidate | null {
  if (catalog.length >= 32) return null
  let index = 1
  while (catalog.some(measurement => measurement.name === `Trc${index}`)) ++index
  return {
    name: `Trc${index}`, kind: active.kind, receiverKind: active.receiverKind,
    receiverPort: active.receiverPort, sourcePort: active.sourcePort,
  }
}
