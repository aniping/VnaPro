type PackedArray = Record<string, unknown>

const BASE64_ALPHABET =
  'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'

function invalid(field: string): never {
  throw new Error(`服务数据格式无效：${field}`)
}

/** 只检查编码，不为大型文件提前分配解码副本。 */
export function validateBase64(value: unknown, byteCount: number, field: string): asserts value is string {
  const padding = (3 - byteCount % 3) % 3
  if (
    typeof value !== 'string' ||
    value.length !== Math.ceil(byteCount / 3) * 4 ||
    value.slice(value.length - padding) !== '='.repeat(padding) ||
    /[^A-Za-z0-9+/]/.test(value.slice(0, value.length - padding))
  ) {
    invalid(field)
  }
  if (
    (value.endsWith('==') &&
      (BASE64_ALPHABET.indexOf(value.at(-3)!) & 15) !== 0) ||
    (value.endsWith('=') &&
      !value.endsWith('==') &&
      (BASE64_ALPHABET.indexOf(value.at(-2)!) & 3) !== 0)
  ) {
    invalid(field)
  }
}

/** 严格验证长度与 Base64 尾位后再分配，损坏容量不能触发无界分配。 */
export function bytes(value: unknown, byteCount: number, field: string): Uint8Array {
  validateBase64(value, byteCount, field)
  let decoded: string
  try {
    decoded = atob(value)
  } catch {
    invalid(field)
  }
  if (decoded.length !== byteCount) invalid(field)
  const result = new Uint8Array(byteCount)
  for (let index = 0; index < byteCount; index++) {
    result[index] = decoded.charCodeAt(index)
  }
  return result
}

function doubles(
  data: Uint8Array,
  count: number,
  field: string,
): (number | null)[] {
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength)
  const result: (number | null)[] = new Array(count)
  for (let index = 0; index < count; index++) {
    const value = view.getFloat64(index * 8, true)
    if (Number.isNaN(value)) result[index] = null
    else if (Number.isFinite(value)) result[index] = value
    else invalid(field)
  }
  return result
}

function booleans(data: Uint8Array, field: string): boolean[] {
  const result: boolean[] = new Array(data.length)
  for (let index = 0; index < data.length; index++) {
    const value = data[index]!
    if (value > 1) invalid(field)
    result[index] = value === 1
  }
  return result
}

function states(item: PackedArray, data: Uint8Array, field: string): string[] {
  const dictionary = item.dictionary
  if (
    !Array.isArray(dictionary) ||
    dictionary.length > 256 ||
    dictionary.some((value) => typeof value !== 'string') ||
    new Set(dictionary).size !== dictionary.length
  ) {
    invalid(field)
  }
  const result: string[] = new Array(data.length)
  for (let index = 0; index < data.length; index++) {
    const value = data[index]!
    if (value >= dictionary.length) invalid(field)
    result[index] = dictionary[value] as string
  }
  return result
}

/** 只还原传输数组；每个字段的状态与数值规则仍交给原 DTO 校验。 */
export function decodePackedArray(
  value: unknown,
  field: string,
  maximum: number,
): unknown[] {
  if (value === null || typeof value !== 'object' || Array.isArray(value))
    invalid(field)
  const item = value as PackedArray
  const count = item.count
  if (
    typeof count !== 'number' ||
    !Number.isSafeInteger(count) ||
    count < 0 ||
    count > maximum
  ) {
    invalid(field)
  }
  if (item.codec !== 'f64le' && item.codec !== 'u8' && item.codec !== 'enum8')
    invalid(field)
  const data = bytes(item.data, count * (item.codec === 'f64le' ? 8 : 1), field)
  if (item.codec === 'f64le') return doubles(data, count, field)
  if (item.codec === 'u8') return booleans(data, field)
  return states(item, data, field)
}
