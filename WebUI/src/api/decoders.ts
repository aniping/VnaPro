import type {
  AcceptedOperation, AcquiredFrame, ClaimResponse, CoreState, FrameResponse,
  InstrumentStatus, Measurement, OperationSnapshot, ReceiverTrace,
} from './types'

type ObjectValue = Record<string, unknown>

function invalid(field: string): never {
  throw new Error(`服务数据格式无效：${field}`)
}

function object(value: unknown, field: string): ObjectValue {
  if (value === null || typeof value !== 'object' || Array.isArray(value)) invalid(field)
  return value as ObjectValue
}

function text(value: unknown, field: string): string {
  if (typeof value !== 'string') invalid(field)
  return value
}

function flag(value: unknown, field: string): boolean {
  if (typeof value !== 'boolean') invalid(field)
  return value
}

function number(value: unknown, field: string, minimum = -Infinity, maximum = Infinity): number {
  if (typeof value !== 'number' || !Number.isFinite(value) || value < minimum || value > maximum) invalid(field)
  return value
}

function integer(value: unknown, field: string, minimum: number, maximum: number): number {
  const result = number(value, field, minimum, maximum)
  if (!Number.isInteger(result)) invalid(field)
  return result
}

function identity(value: unknown, field: string): string {
  const result = text(value, field)
  if (!/^\d{1,20}$/.test(result)) invalid(field)
  return result
}

function choice<T extends string>(value: unknown, field: string, choices: readonly T[]): T {
  const result = text(value, field)
  const match = choices.find((item) => item === result)
  if (match === undefined) invalid(field)
  return match
}

function array(value: unknown, field: string, maximum: number): unknown[] {
  if (!Array.isArray(value) || value.length > maximum) invalid(field)
  return value
}

function measurement(value: unknown): Measurement {
  const item = object(value, 'measurement')
  const name = text(item.name, 'measurement.name')
  if (!name || name.length > 128) invalid('measurement.name')
  return {
    name,
    receiverKind: choice(item.receiverKind, 'measurement.receiverKind', ['a', 'b']),
    receiverPort: integer(item.receiverPort, 'measurement.receiverPort', 1, 4),
    sourcePort: integer(item.sourcePort, 'measurement.sourcePort', 1, 4),
    parameter: text(item.parameter, 'measurement.parameter'),
  }
}

export function decodeStatus(value: unknown): InstrumentStatus {
  const item = object(value, 'status')
  const config = object(item.config, 'config')
  const control = object(item.control, 'control')
  const startHz = number(config.startHz, 'config.startHz', 1, 1e9)
  const stopHz = number(config.stopHz, 'config.stopHz', 1, 1e9)
  if (startHz >= stopHz) invalid('config.startHz / stopHz')
  const measurements = array(item.measurements, 'measurements', 32).map(measurement)
  if (new Set(measurements.map((entry) => entry.name)).size !== measurements.length) invalid('measurement names')
  return {
    identity: text(item.identity, 'identity'),
    backend: text(item.backend, 'backend'),
    state: choice<CoreState>(item.state, 'state', ['Uninitialized', 'Ready', 'Running', 'Faulted', 'Closed']),
    error: text(item.error, 'error'),
    pending: flag(item.pending, 'pending'),
    revision: identity(item.revision, 'revision'),
    runId: identity(item.runId, 'runId'),
    activeConfigValid: flag(item.activeConfigValid, 'activeConfigValid'),
    config: {
      startHz, stopHz,
      points: integer(config.points, 'config.points', 2, 10001),
      ifBandwidthHz: number(config.ifBandwidthHz, 'config.ifBandwidthHz', 10),
      triggerSource: choice(config.triggerSource, 'config.triggerSource', ['IMM', 'MAN']),
      sweepMode: choice(config.sweepMode, 'config.sweepMode', ['HOLD', 'SING', 'CONT']),
    },
    measurements,
    control: {
      owner: choice(control.owner, 'control.owner', ['none', 'scpi', 'web']),
      hasControl: flag(control.hasControl, 'control.hasControl'),
      releasing: flag(control.releasing, 'control.releasing'),
      leaseRemainingMs: number(control.leaseRemainingMs, 'control.leaseRemainingMs', 0),
      readOnlyReason: text(control.readOnlyReason, 'control.readOnlyReason'),
    },
  }
}

function trace(value: unknown, count: number): ReceiverTrace {
  const item = object(value, 'trace')
  const real = array(item.real, 'trace.real', 2001).map((entry) => entry === null ? null : number(entry, 'trace.real'))
  const imag = array(item.imag, 'trace.imag', 2001).map((entry) => entry === null ? null : number(entry, 'trace.imag'))
  const valid = array(item.valid, 'trace.valid', 2001).map((entry) => flag(entry, 'trace.valid'))
  if ([real.length, imag.length, valid.length].some((length) => length !== count)) invalid('trace point count')
  if (valid.some((entry, index) => entry && (real[index] === null || imag[index] === null))) invalid('trace valid data')
  return { ...measurement(item), available: flag(item.available, 'trace.available'), real, imag, valid }
}

export function decodeFrame(value: unknown): FrameResponse {
  const item = object(value, 'frame')
  if (!flag(item.available, 'frame.available')) {
    if (flag(item.current, 'frame.current')) invalid('unavailable frame.current')
    return { available: false, current: false, reason: text(item.reason, 'frame.reason') }
  }
  const count = integer(item.displayPointCount, 'frame.displayPointCount', 1, 2001)
  const originalCount = integer(item.originalPointCount, 'frame.originalPointCount', count, 10001)
  const axis = array(item.frequencyHz, 'frame.frequencyHz', 2001)
    .map((entry) => number(entry, 'frame.frequencyHz', 1, 1e9))
  if (axis.length !== count || axis.some((entry, index) => index > 0 && entry <= axis[index - 1]!)) invalid('frame axis')
  const decimated = flag(item.decimated, 'frame.decimated')
  if (decimated !== (originalCount > count)) invalid('frame.decimated')
  const traces = array(item.traces, 'frame.traces', 4).map((entry) => trace(entry, count))
  if (new Set(traces.map((entry) => entry.name)).size !== traces.length) invalid('trace names')
  const result: AcquiredFrame = {
    available: true,
    current: flag(item.current, 'frame.current'),
    channelId: integer(item.channelId, 'frame.channelId', 1, 65535),
    runId: identity(item.runId, 'frame.runId'),
    revision: identity(item.revision, 'frame.revision'),
    frameSeq: identity(item.frameSeq, 'frame.frameSeq'),
    originalPointCount: originalCount,
    displayPointCount: count,
    decimated,
    frequencyHz: axis,
    traces,
  }
  return result
}

export function decodeClaim(value: unknown): ClaimResponse {
  const item = object(value, 'claim')
  const sessionId = text(item.sessionId, 'claim.sessionId')
  if (!sessionId || sessionId.length > 128 || !flag(item.hasControl, 'claim.hasControl')) invalid('claim control')
  return { sessionId, hasControl: true, leaseSeconds: number(item.leaseSeconds, 'claim.leaseSeconds', 1) }
}

export function decodeAccepted(value: unknown): AcceptedOperation {
  const item = object(value, 'operation')
  const operationId = text(item.operationId, 'operation.operationId')
  if (!operationId || operationId.length > 128) invalid('operation.operationId')
  return {
    operationId,
    state: choice(item.state, 'operation.state', ['pending', 'completed', 'failed']),
  }
}

export function decodeOperation(value: unknown): OperationSnapshot {
  const accepted = decodeAccepted(value)
  const item = object(value, 'operation')
  if (accepted.state === 'pending') {
    if (item.result !== null) invalid('pending result')
    return { ...accepted, result: null }
  }
  const result = object(item.result, 'operation.result')
  const ok = flag(result.ok, 'operation.result.ok')
  if ((accepted.state === 'completed') !== ok) invalid('operation outcome')
  return {
    ...accepted,
    result: {
      ok,
      errorCode: number(result.errorCode, 'operation.result.errorCode'),
      message: text(result.message, 'operation.result.message'),
    },
  }
}
