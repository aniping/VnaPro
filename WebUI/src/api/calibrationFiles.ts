import type { CalibrationCommand, CalibrationFile } from './calibrationTypes'
import { bytes, validateBase64 } from './packedArrays'

const MIB = 1024 * 1024

export function decodeCalibrationFile(value: unknown): CalibrationFile {
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw new Error('校准文件响应无效。')
  const item = value as Record<string, unknown>
  const { format, encoding, filename, byteLength, content } = item
  if ((format !== 'binary' && format !== 'csv') || typeof filename !== 'string' ||
      typeof byteLength !== 'number' || !Number.isSafeInteger(byteLength) || byteLength < 1 ||
      typeof content !== 'string' || !new RegExp(`^vnapro-[0-9]+\\.${format === 'binary' ? 'vcal' : 'csv'}$`).test(filename))
    throw new Error('校准文件响应字段无效。')
  if (format === 'binary') {
    if (encoding !== 'base64' || byteLength > 8 * MIB) throw new Error('二进制校准文件编码或大小无效。')
    validateBase64(content, byteLength, 'calibration binary')
  } else if (encoding !== 'utf8' || byteLength > 32 * MIB ||
      content.length > 32 * MIB || new TextEncoder().encode(content).length !== byteLength)
    throw new Error('校准 CSV 编码或大小无效。')
  return { format, encoding, filename, byteLength, content }
}

export function calibrationFileBlob(file: CalibrationFile): Blob {
  if (file.format === 'csv') return new Blob([file.content], { type: 'text/csv;charset=utf-8' })
  const data = bytes(file.content, file.byteLength, 'calibration binary')
  return new Blob([new Uint8Array(data)], { type: 'application/octet-stream' })
}

function base64(data: Uint8Array): string {
  const chunks: string[] = []
  // 每块为 3 字节的整倍数，只有最后一块可能有 padding；避免大数组展开溢出。
  for (let first = 0; first < data.length; first += 24576)
    chunks.push(btoa(String.fromCharCode(...data.subarray(first, first + 24576))))
  return chunks.join('')
}

export async function readCalibrationFile(file: File): Promise<CalibrationCommand> {
  const name = file.name?.toLowerCase() ?? ''
  const binary = name.endsWith('.vcal')
  if (!binary && !name.endsWith('.csv')) throw new Error('请选择 .vcal 或 .csv 校准文件。')
  if (file.size > (binary ? 8 : 32) * MIB) throw new Error('校准文件超出大小限制。')
  if (binary) {
    const data = new Uint8Array(await file.arrayBuffer())
    if (data.length > 8 * MIB) throw new Error('二进制校准文件超过 8 MiB。')
    return { action: 'importFile', format: 'binary', encoding: 'base64', content: base64(data) }
  }
  let content: string
  try {
    content = new TextDecoder('utf-8', { fatal: true }).decode(await file.arrayBuffer())
  } catch {
    throw new Error('校准 CSV 必须是有效的 UTF-8 文本。')
  }
  return { action: 'importFile', format: 'csv', encoding: 'utf8', content }
}
