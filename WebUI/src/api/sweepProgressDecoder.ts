import type { FrameResponse, SweepProgress } from './types'
import { integer, invalid, object } from './decodeValues'
import { FRAME_SAMPLE_BUDGET } from './frameLimits'
import { MAX_TRACES } from '../domain/presentation'

/** 部分扫描有独立边界；未采集的后缀不能伪装成零值或完整测量。 */
export function decodeSweepProgress(
  value: unknown,
  channelId: number | undefined,
  decodeFrame: (value: unknown) => FrameResponse,
): SweepProgress {
  const item = object(value, 'sweepProgress')
  const raw = object(item.frame, 'sweepProgress.frame')
  if (raw.sweepProgress !== undefined || raw.channelFrames !== undefined)
    invalid('sweepProgress recursion')
  const frame = decodeFrame(raw)
  if (!frame.available || frame.current || frame.channelId !== channelId || frame.decimated)
    invalid('sweepProgress frame identity')
  const completedPoints = integer(item.completedPoints, 'sweepProgress.completedPoints', 1,
    frame.originalPointCount - 1)
  if (frame.displayPointCount !== frame.originalPointCount ||
    frame.sampleIndexes.some((index, position) => index !== position))
    invalid('sweepProgress sample indexes')
  for (const trace of frame.traces) {
    if ((trace.channelId ?? frame.channelId) !== frame.channelId)
      invalid('sweepProgress trace channel')
    const projection = trace.projection
    if (projection.marker.available || projection.marker.valid ||
      projection.markers?.some((marker) => marker.available || marker.valid))
      invalid('sweepProgress markers')
    for (let index = completedPoints; index < frame.displayPointCount; index++) {
      if (trace.valid[index] || trace.real[index] !== null || trace.imag[index] !== null ||
        projection.values[index] !== null || projection.secondaryValues[index] !== null)
        invalid('sweepProgress unacquired suffix')
    }
  }
  return { completedPoints, frame }
}

/** 完整帧与预览各自遵守同一多通道预算，不能通过旁带绕过总量限制。 */
export function validateProgressChannels(channels: FrameResponse[]): void {
  const frames = channels.flatMap((channel) => channel.sweepProgress ? [channel.sweepProgress.frame] : [])
  const names = frames.flatMap((frame) => frame.traces.map((trace) => trace.name))
  if (names.length > MAX_TRACES || new Set(names).size !== names.length ||
    frames.reduce((sum, frame) => sum + frame.displayPointCount * frame.traces.length, 0) > FRAME_SAMPLE_BUDGET)
    invalid('sweepProgress channel budget')
}
