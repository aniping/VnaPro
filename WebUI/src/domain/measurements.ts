import type { Measurement } from '../api/types'

type MeasurementPort = 1 | 2 | 3 | 4
export type SParameterChoice = `S${MeasurementPort}${MeasurementPort}`
export type MeasurementChoice = SParameterChoice | 'RAW'

/** 只展开设备能力支持的规范名称，接收端口在前、源端口在后。 */
export function sParameterChoices(portCount: number): SParameterChoice[] {
  const ports = ([1, 2, 3, 4] as const).filter((port) => port <= portCount)
  return ports.flatMap((receiver) =>
    ports.map((source): SParameterChoice => `S${receiver}${source}`),
  )
}

export function measurementState(measurement: Measurement): string {
  return measurement.calibration === 'calibrated' ? 'Cal' : measurement.calibration === 'uncalibrated' ? '未校准' : 'RAW'
}
