import type { AcquiredFrame } from '../api/types'
import type { TracePresentation } from '../domain/presentation'
import { measurementFormatInfo } from './formatting'

function csvCell(value: unknown): string {
  return `"${String(value ?? '').replaceAll('"', '""')}"`
}

/** 只导出实际显示的快照，不补造完整扫描中的其他采样点。 */
export function exportVisibleFrameCsv(
  frame: AcquiredFrame,
  presentations: Record<string, TracePresentation>,
): string {
  const lines = [
    '# VnaPro 测量显示快照；CSV schema 4',
    `# frameSeq=${frame.frameSeq},runId=${frame.runId},revision=${frame.revision},channelId=${frame.channelId},measurementRevision=${frame.measurementRevision},displayRevision=${frame.displayRevision},calibrationRevision=${frame.calibrationRevision ?? 'legacy'}`,
    `# originalPointCount=${frame.originalPointCount},displayPointCount=${frame.displayPointCount},decimated=${frame.decimated}`,
    '# source_index 为后端提供的原始采样索引，display_index 为展示数组索引。',
    '# RAW dB 参考为 1 a.u.；S 参数 dB 为无量纲比值幅度的 20 log10。校准状态见每行 calibration，calibrated 表示该复数结果已应用误差校准。',
    '# 显示值、无穷与无效状态均来自后端投影，浏览器不计算测量量。',
    'measurement,receiver,receiver_port,source_port,display_index,frequency_hz,real,imaginary,valid,format,unit,display_value,display_imaginary,kind,parameter,raw_unit,calibration,source_index,display_state,secondary_state',
  ]
  for (const trace of frame.traces) {
    const presentation = presentations[trace.name]
    if (!presentation?.visible) continue
    for (let index = 0; index < frame.frequencyHz.length; ++index) {
      const valid = trace.valid[index] === true
      const projection = trace.projection
      const format = projection.format
      const cell =
        projection.valueStates[index] === 'negativeInfinity'
          ? '-Infinity'
          : (projection.values[index] ?? '')
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
          format,
          csvCell(measurementFormatInfo(format, trace).unit),
          cell,
          projection.secondaryStates[index] === 'negativeInfinity'
            ? '-Infinity'
            : (projection.secondaryValues[index] ?? ''),
          trace.kind,
          csvCell(trace.parameter),
          csvCell(trace.unit),
          trace.calibration,
          frame.sampleIndexes[index],
          projection.valueStates[index],
          projection.secondaryStates[index],
        ].join(','),
      )
    }
  }
  return `\uFEFF${lines.join('\r\n')}\r\n`
}
