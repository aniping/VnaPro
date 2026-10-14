import { decodePackedArray } from './packedArrays'

type ObjectValue = Record<string, unknown>

export function invalid(field: string): never {
  throw new Error(`服务数据格式无效：${field}`)
}

export function object(value: unknown, field: string): ObjectValue {
  if (value === null || typeof value !== 'object' || Array.isArray(value))
    invalid(field)
  return value as ObjectValue
}

export function text(value: unknown, field: string): string {
  if (typeof value !== 'string') invalid(field)
  return value
}

export function flag(value: unknown, field: string): boolean {
  if (typeof value !== 'boolean') invalid(field)
  return value
}

export function number(
  value: unknown,
  field: string,
  minimum = -Infinity,
  maximum = Infinity,
): number {
  if (
    typeof value !== 'number' ||
    !Number.isFinite(value) ||
    value < minimum ||
    value > maximum
  )
    invalid(field)
  return value
}

export function integer(
  value: unknown,
  field: string,
  minimum: number,
  maximum: number,
): number {
  const result = number(value, field, minimum, maximum)
  if (!Number.isInteger(result)) invalid(field)
  return result
}

export function identity(value: unknown, field: string): string {
  const result = text(value, field)
  if (!/^\d{1,20}$/.test(result)) invalid(field)
  if (BigInt(result) > 18446744073709551615n) invalid(field)
  return result
}

export function choice<T extends string>(
  value: unknown,
  field: string,
  choices: readonly T[],
): T {
  const result = text(value, field)
  const match = choices.find((item) => item === result)
  if (match === undefined) invalid(field)
  return match
}

export function array(
  value: unknown,
  field: string,
  maximum: number,
): unknown[] {
  if (!Array.isArray(value)) return decodePackedArray(value, field, maximum)
  if (value.length > maximum) invalid(field)
  return value
}
