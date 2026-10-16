import type { InstrumentCapabilities, SweepConfiguration } from '../api/types'

/** 草稿以服务端能力为边界，跨度为零仍是有效的定频扫描。 */
export function sweepWithinCapabilities(value: SweepConfiguration, limits: InstrumentCapabilities): boolean {
  return [value.startHz, value.stopHz, value.points, value.ifBandwidthHz].every(Number.isFinite)
    && value.startHz >= limits.minFrequencyHz && value.stopHz <= limits.maxFrequencyHz
    && value.startHz <= value.stopHz && Number.isInteger(value.points)
    && value.points >= limits.minSweepPoints && value.points <= limits.maxSweepPoints
    && value.ifBandwidthHz >= 10
    && (value.powerDbm === undefined || (Number.isFinite(value.powerDbm) && value.powerDbm >= (limits.minPowerDbm ?? -120) && value.powerDbm <= (limits.maxPowerDbm ?? 100)))
}
