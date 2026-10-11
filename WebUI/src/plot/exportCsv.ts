import type { AcquiredFrame } from '../api/types'
import { defaultTracePresentation, type TracePresentation } from '../domain/presentation'
import { measurementFormatInfo } from './formatting'
import { displayValue, validAt } from './traceMath'

function csvCell(value: unknown): string {
  return `"${String(value ?? '').replaceAll('"', '""')}"`
}

/** 只导出实际显示的快照，不补造完整扫描中的其他采样点。 */
export function exportVisibleFrameCsv(frame: AcquiredFrame, presentations: Record<string, TracePresentation>): string {
  const lines = [
    '# VnaPro measurement display snapshot; CSV schema 2',
    `# frameSeq=${frame.frameSeq},runId=${frame.runId},revision=${frame.revision},channelId=${frame.channelId}`,
    `# originalPointCount=${frame.originalPointCount},displayPointCount=${frame.displayPointCount},decimated=${frame.decimated}`,
    '# Source sample indices are not supplied by this API; display_index addresses the supplied display arrays.',
    '# RAW dB reference is 1 a.u.; S-parameter dB is 20 log10 magnitude of the dimensionless ratio. S-parameters are uncalibrated.',
    'measurement,receiver,receiver_port,source_port,display_index,frequency_hz,real,imaginary,valid,format,unit,display_value,display_imaginary,kind,parameter,raw_unit,calibration',
  ]
  for (const trace of frame.traces) {
    const presentation = presentations[trace.name] ?? defaultTracePresentation()
    if (!presentation.visible) continue
    for (let index = 0; index < frame.frequencyHz.length; ++index) {
      const valid = validAt(trace, index)
      const value = valid ? displayValue(trace.real[index]!, trace.imag[index]!, presentation.format) : NaN
      const cell = Number.isFinite(value) ? value : value === -Infinity ? '-Infinity' : ''
      lines.push([
        csvCell(trace.name), trace.receiverKind, trace.receiverPort, trace.sourcePort, index, frame.frequencyHz[index],
        valid ? trace.real[index] : '', valid ? trace.imag[index] : '', valid,
        presentation.format, csvCell(measurementFormatInfo(presentation.format, trace).unit), cell,
        presentation.format === 'realimag' && valid ? trace.imag[index] : '',
        trace.kind, csvCell(trace.parameter), csvCell(trace.unit), trace.calibration,
      ].join(','))
    }
  }
  return `\uFEFF${lines.join('\r\n')}\r\n`
}
