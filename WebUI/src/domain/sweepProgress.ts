import type { AcquiredFrame, ChannelStatus, FrameResponse, ReceiverTrace, SweepProgress } from '../api/types'

export interface SweepTraceSegment {
  frame: AcquiredFrame
  trace: ReceiverTrace
}

/** 状态/HTTP响应可能前后到达，只接受同一通道、配置、运行和投影身份的进度。 */
export function currentSweepProgress(
  response: FrameResponse | undefined,
  channel: ChannelStatus | undefined,
  connected: boolean,
  revision?: string,
  displayRevision?: string,
): SweepProgress | null {
  const progress = response?.sweepProgress
  if (!connected || !progress || !channel || channel.state !== 'Running' ||
    !channel.activeConfigValid || channel.calibration?.collecting) return null
  const frame = progress.frame
  if (frame.channelId !== channel.channelId || frame.runId !== channel.runId ||
    frame.revision !== channel.acquisitionRevision ||
    (revision !== undefined && frame.measurementRevision !== revision) ||
    (displayRevision !== undefined && frame.displayRevision !== displayRevision) ||
    (channel.calibration && frame.calibrationRevision !== channel.calibration.revision)) return null
  if (response.available && response.runId === frame.runId &&
    BigInt(response.frameSeq) >= BigInt(frame.frameSeq)) return null
  return progress
}

function sameProjection(previous: AcquiredFrame, next: AcquiredFrame): boolean {
  return previous.channelId === next.channelId && previous.revision === next.revision &&
    previous.measurementRevision === next.measurementRevision && previous.displayRevision === next.displayRevision &&
    previous.calibrationRevision === next.calibrationRevision &&
    previous.frequencyHz.length === next.frequencyHz.length &&
    previous.frequencyHz.every((frequency, index) => frequency === next.frequencyHz[index])
}

function retainedTail(trace: ReceiverTrace, completed: number): ReceiverTrace {
  const mask = <T>(values: readonly T[], empty: T): T[] => values.map((value, index) => index < completed ? empty : value)
  return { ...trace, real: mask(trace.real, null), imag: mask(trace.imag, null), valid: mask(trace.valid, false),
    projection: { ...trace.projection,
      values: mask(trace.projection.values, null), secondaryValues: mask(trace.projection.secondaryValues, null),
      valueStates: mask(trace.projection.valueStates, 'undefined'),
      secondaryStates: mask(trace.projection.secondaryStates, 'undefined') } }
}

/** 新前缀与旧尾分开绘制，避免在两扫交界处连出一条从未测到的线段。 */
export function sweepTraceSegments(
  previous: AcquiredFrame | null,
  progress: SweepProgress | null,
  name: string,
): SweepTraceSegment[] {
  const oldTrace = previous?.traces.find((trace) => trace.name === name)
  if (!progress) return previous && oldTrace ? [{ frame: previous, trace: oldTrace }] : []
  const trace = progress.frame.traces.find((item) => item.name === name)
  if (!trace) return []
  const result: SweepTraceSegment[] = []
  if (previous && oldTrace && sameProjection(previous, progress.frame) &&
    oldTrace.projection.format === trace.projection.format)
    result.push({ frame: previous, trace: retainedTail(oldTrace, progress.completedPoints) })
  result.push({ frame: progress.frame, trace })
  return result
}
