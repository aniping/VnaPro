import type { AcquiredFrame, ChannelStatus, FrameCondition, FrameResponse, InstrumentStatus, Measurement } from '../api/types'

export const MAX_CHANNELS = 16

export function measurementChannel(measurement: Measurement | undefined): number {
  return measurement?.channelId ?? 1
}

export function channelStatus(status: InstrumentStatus | null, id: number): ChannelStatus | null {
  if (!status) return null
  if (status.channels) return status.channels.find((channel) => channel.channelId === id) ?? null
  return id === 1 ? { ...status, channelId: 1, name: 'Ch1' } : null
}

export function frameForMeasurement(
  measurement: Measurement | undefined,
  frames: Readonly<Record<number, FrameResponse>> | undefined,
  fallback: AcquiredFrame | null,
): AcquiredFrame | null {
  const id = measurementChannel(measurement)
  const response = frames?.[id]
  if (response) return response.available && response.channelId === id ? response : null
  return fallback?.channelId === id ? fallback : null
}

/** 比较所属通道的身份；其他通道的新计划或运行次数不能让本通道数据失效。 */
export function channelFrameCondition(
  status: InstrumentStatus | null,
  id: number,
  frame: AcquiredFrame | null,
  connected: boolean,
): FrameCondition {
  const channel = channelStatus(status, id)
  if (!connected) return { kind: 'disconnected', label: 'DISCONNECTED', detail: 'Retained frame · connection unavailable' }
  if (channel?.state === 'Faulted') return { kind: 'fault', label: 'FAULT', detail: channel.error }
  if (!frame) return { kind: 'empty', label: 'NO DATA', detail: 'Run a single or continuous sweep to acquire measurement data.' }
  if (!channel?.activeConfigValid || frame.channelId !== id || frame.revision !== channel.acquisitionRevision || frame.measurementRevision !== status?.revision)
    return { kind: 'stale', label: 'PREVIOUS CONFIG', detail: 'Retained frame · waiting for this channel configuration' }
  if (frame.displayRevision !== status?.display.revision)
    return { kind: 'stale', label: 'PREVIOUS DISPLAY', detail: 'Retained projection · waiting for shared display state' }
  if (channel.calibration && frame.calibrationRevision !== channel.calibration.revision)
    return { kind: 'stale', label: 'PREVIOUS CAL', detail: 'Retained projection · waiting for this channel calibration' }
  if (frame.runId !== channel.runId) return { kind: 'acquiring', label: 'PREVIOUS RUN', detail: 'Retained frame · waiting for this channel run' }
  return frame.current
    ? { kind: 'current', label: 'CURRENT FRAME', detail: '' }
    : { kind: 'stale', label: 'RETAINED FRAME', detail: 'Waiting for current channel data' }
}
