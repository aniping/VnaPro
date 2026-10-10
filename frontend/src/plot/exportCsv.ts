import type { AcquiredFrame } from '../api/types'
import { defaultTracePresentation, type TracePresentation } from '../domain/presentation'
import { FORMAT_INFO } from './formatting'
import { displayValue, validAt } from './traceMath'

function csvCell(value: unknown): string {
  return `"${String(value ?? '').replaceAll('"', '""')}"`
}

/** Export only the actual displayed snapshot. Full sweep points are never synthesized. */
export function exportVisibleFrameCsv(frame: AcquiredFrame, presentations: Record<string, TracePresentation>): string {
  const lines = [
    '# VnaPro raw receiver display snapshot',
    `# frameSeq=${frame.frameSeq},runId=${frame.runId},revision=${frame.revision},channelId=${frame.channelId}`,
    `# originalPointCount=${frame.originalPointCount},displayPointCount=${frame.displayPointCount},decimated=${frame.decimated}`,
    '# Source sample indices are not supplied by this API; display_index addresses the supplied display arrays.',
    '# dB reference is 1 arbitrary amplitude unit. This is not a calibrated S-parameter export.',
    'measurement,receiver,receiver_port,source_port,display_index,frequency_hz,real,imaginary,valid,format,unit,display_value,display_imaginary',
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
        presentation.format, csvCell(FORMAT_INFO[presentation.format].unit), cell,
        presentation.format === 'realimag' && valid ? trace.imag[index] : '',
      ].join(','))
    }
  }
  return `\uFEFF${lines.join('\r\n')}\r\n`
}
