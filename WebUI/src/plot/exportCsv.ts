import type { AcquiredFrame } from '../api/types'
import {
  defaultTracePresentation,
  type TracePresentation,
} from '../domain/presentation'
import { measurementFormatInfo } from './formatting'
import { displayValue, validAt } from './traceMath'
import { normalizedImpedance } from './smithMath'

function csvCell(value: unknown): string {
  return `"${String(value ?? '').replaceAll('"', '""')}"`
}

/** 只导出实际显示的快照，不补造完整扫描中的其他采样点。 */
export function exportVisibleFrameCsv(
  frame: AcquiredFrame,
  presentations: Record<string, TracePresentation>,
): string {
  const lines = [
    '# VnaPro 测量显示快照；CSV schema 3',
    `# frameSeq=${frame.frameSeq},runId=${frame.runId},revision=${frame.revision},channelId=${frame.channelId},measurementRevision=${frame.measurementRevision}`,
    `# originalPointCount=${frame.originalPointCount},displayPointCount=${frame.displayPointCount},decimated=${frame.decimated}`,
    '# 此 API 未提供原始采样索引；display_index 对应返回的实际展示数组。',
    '# RAW dB 参考为 1 a.u.；S 参数 dB 为无量纲比值幅度的 20 log10，未应用误差校准。',
    '# Smith 数据为无量纲 Γ 与归一化阻抗 z=(1+Γ)/(1-Γ)，不假定物理参考阻抗。',
    'measurement,receiver,receiver_port,source_port,display_index,frequency_hz,real,imaginary,valid,format,unit,display_value,display_imaginary,kind,parameter,raw_unit,calibration,gamma_real,gamma_imaginary,normalized_r,normalized_x,impedance_state',
  ]
  for (const trace of frame.traces) {
    const presentation = presentations[trace.name] ?? defaultTracePresentation()
    if (!presentation.visible) continue
    for (let index = 0; index < frame.frequencyHz.length; ++index) {
      const valid = validAt(trace, index)
      const value = valid
        ? displayValue(
            trace.real[index]!,
            trace.imag[index]!,
            presentation.format,
          )
        : NaN
      const cell = Number.isFinite(value)
        ? value
        : value === -Infinity
          ? '-Infinity'
          : ''
      const smith = presentation.format === 'smith' && valid
      const impedance = smith
        ? normalizedImpedance(trace.real[index]!, trace.imag[index]!)
        : null
      lines.push(
        [
          csvCell(trace.name),
          trace.receiverKind,
          trace.receiverPort,
          trace.sourcePort,
          index,
          frame.frequencyHz[index],
          valid ? trace.real[index] : '',
          valid ? trace.imag[index] : '',
          valid,
          presentation.format,
          csvCell(measurementFormatInfo(presentation.format, trace).unit),
          cell,
          presentation.format === 'realimag' && valid ? trace.imag[index] : '',
          trace.kind,
          csvCell(trace.parameter),
          csvCell(trace.unit),
          trace.calibration,
          smith ? trace.real[index] : '',
          smith ? trace.imag[index] : '',
          impedance?.real ?? '',
          impedance?.imaginary ?? '',
          impedance?.state ?? '',
        ].join(','),
      )
    }
  }
  return `\uFEFF${lines.join('\r\n')}\r\n`
}
