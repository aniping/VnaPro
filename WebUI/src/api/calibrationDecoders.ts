import type {
  CalibrationPreviewResponse,
  CalibrationSetSummary,
  CalibrationSnapshot,
  CalibrationStep,
} from './calibrationTypes'
import {
  array,
  choice,
  flag,
  identity,
  integer,
  invalid,
  number,
  object,
  text,
} from './decodeValues'
import { FRAME_POINT_LIMIT } from './frameLimits'

function identifier(value: unknown, field: string, empty = false): string {
  const result = text(value, field)
  if ((!empty && !result) || result.length > 128) invalid(field)
  return result
}

function idleIdentity(value: unknown, field: string): string {
  const result = identity(value, field)
  return result === '0' ? '' : result
}

function ports(
  method: 'onePort' | 'twoPort',
  port1: number,
  port2: number,
): void {
  if (method === 'twoPort' && (port2 === 0 || port1 === port2))
    invalid('calibration ports')
}

function step(value: unknown, portCount: number): CalibrationStep {
  const item = object(value, 'calibration.step')
  const result: CalibrationStep = {
    id: identifier(item.id, 'calibration.step.id'),
    kind: choice(item.kind, 'calibration.step.kind', [
      'open',
      'short',
      'load',
      'thru',
      'isolation',
    ]),
    port1: integer(item.port1, 'calibration.step.port1', 1, portCount),
    port2: integer(item.port2, 'calibration.step.port2', 0, portCount),
    captured: flag(item.captured, 'calibration.step.captured'),
    runId: identity(item.runId, 'calibration.step.runId'),
    frameSeq: identity(item.frameSeq, 'calibration.step.frameSeq'),
  }
  if (
    (result.kind === 'thru' || result.kind === 'isolation') &&
    (result.port2 === 0 || result.port1 === result.port2)
  )
    invalid('calibration.step pair')
  return result
}

function summary(value: unknown, portCount: number): CalibrationSetSummary {
  const item = object(value, 'calibration.set')
  const result: CalibrationSetSummary = {
    id: identity(item.id, 'calibration.set.id'),
    name: identifier(item.name, 'calibration.set.name'),
    method: choice(item.method, 'calibration.set.method', [
      'onePort',
      'twoPort',
    ]),
    port1: integer(item.port1, 'calibration.set.port1', 1, portCount),
    port2: integer(item.port2, 'calibration.set.port2', 0, portCount),
    compatible: flag(item.compatible, 'calibration.set.compatible'),
    ...(item.includeIsolation === undefined
      ? {}
      : {
          includeIsolation: flag(
            item.includeIsolation,
            'calibration.set.includeIsolation',
          ),
        }),
  }
  ports(result.method, result.port1, result.port2)
  for (const field of ['startHz', 'stopHz', 'ifBandwidthHz'] as const)
    if (item[field] !== undefined)
      result[field] = number(
        item[field],
        `calibration.set.${field}`,
        Number.MIN_VALUE,
      )
  if (item.points !== undefined)
    result.points = integer(item.points, 'calibration.set.points', 2, 10001)
  if (
    result.startHz !== undefined &&
    result.stopHz !== undefined &&
    result.startHz > result.stopHz
  )
    invalid('calibration.set frequency range')
  return result
}

/** 校准状态来自服务端；缺少能力时不构造成功的默认状态。 */
export function decodeCalibration(
  value: unknown,
  portCount = 4,
): CalibrationSnapshot {
  const item = object(value, 'calibration')
  const result: CalibrationSnapshot = {
    revision: identity(item.revision, 'calibration.revision'),
    collecting: flag(item.collecting, 'calibration.collecting'),
    capturePending: flag(item.capturePending, 'calibration.capturePending'),
    sessionId: idleIdentity(item.sessionId, 'calibration.sessionId'),
    method: choice(item.method, 'calibration.method', ['onePort', 'twoPort']),
    port1: integer(item.port1, 'calibration.port1', 1, portCount),
    port2: integer(item.port2, 'calibration.port2', 0, portCount),
    steps: array(item.steps, 'calibration.steps', 8).map((value) =>
      step(value, portCount),
    ),
    selectedSetId: idleIdentity(
      item.selectedSetId,
      'calibration.selectedSetId',
    ),
    enabled: flag(item.enabled, 'calibration.enabled'),
    compatible: flag(item.compatible, 'calibration.compatible'),
    reason: text(item.reason, 'calibration.reason'),
    ...(item.includeIsolation === undefined
      ? {}
      : {
          includeIsolation: flag(
            item.includeIsolation,
            'calibration.includeIsolation',
          ),
        }),
    ...(item.error === undefined
      ? {}
      : { error: text(item.error, 'calibration.error') }),
    sets: array(item.sets, 'calibration.sets', 16).map((value) =>
      summary(value, portCount),
    ),
  }
  if (
    new Set(result.steps.map((entry) => entry.id)).size !==
      result.steps.length ||
    new Set(result.sets.map((entry) => entry.id)).size !== result.sets.length
  )
    invalid('calibration duplicate identifiers')
  if (result.collecting) {
    if (!result.sessionId || !result.steps.length)
      invalid('calibration session')
    ports(result.method, result.port1, result.port2)
    const selectedPorts =
      result.method === 'onePort'
        ? [result.port1]
        : [result.port1, result.port2]
    if (
      result.steps.some(
        (entry) =>
          !selectedPorts.includes(entry.port1) ||
          (entry.port2 !== 0 && !selectedPorts.includes(entry.port2)),
      )
    )
      invalid('calibration.step coverage')
  }
  if (result.capturePending && !result.collecting)
    invalid('calibration.capturePending')
  if (
    result.selectedSetId &&
    !result.sets.some((entry) => entry.id === result.selectedSetId)
  )
    invalid('calibration.selectedSetId')
  return result
}

export function decodeCalibrationExport(
  value: unknown,
): Record<string, unknown> {
  const result = object(value, 'calibration data')
  if (result.schema !== 1) invalid('calibration data.schema')
  // 系数合同由服务端校验；浏览器不解算、插值或重写系数。
  return result
}

export function decodeCalibrationPreview(
  value: unknown,
): CalibrationPreviewResponse {
  const item = object(value, 'calibration preview')
  const common = {
    sessionId: identity(item.sessionId, 'preview.sessionId'),
    stepId: identifier(item.stepId, 'preview.stepId'),
    calibrationRevision: identity(
      item.calibrationRevision,
      'preview.calibrationRevision',
    ),
  }
  if (!flag(item.available, 'preview.available'))
    return {
      ...common,
      available: false,
      reason: text(item.reason, 'preview.reason'),
    }
  const count = integer(
    item.displayPointCount,
    'preview.displayPointCount',
    2,
    FRAME_POINT_LIMIT,
  )
  const original = integer(
    item.originalPointCount,
    'preview.originalPointCount',
    count,
    FRAME_POINT_LIMIT,
  )
  const values = (field: string) =>
    array(item[field], `preview.${field}`, count).map((entry) =>
      entry === null ? null : number(entry, `preview.${field}`),
    )
  const states = (field: string) =>
    array(item[field], `preview.${field}`, count).map((entry) =>
      choice(entry, `preview.${field}`, [
        'finite',
        'negativeInfinity',
        'undefined',
      ] as const),
    )
  const result = {
    ...common,
    available: true as const,
    parameter: text(item.parameter, 'preview.parameter'),
    runId: identity(item.runId, 'preview.runId'),
    frameSeq: identity(item.frameSeq, 'preview.frameSeq'),
    originalPointCount: original,
    displayPointCount: count,
    sampleIndexes: array(
      item.sampleIndexes,
      'preview.sampleIndexes',
      count,
    ).map((entry) => integer(entry, 'preview.sampleIndex', 0, original - 1)),
    decimated: flag(item.decimated, 'preview.decimated'),
    ifBandwidthHz: number(
      item.ifBandwidthHz,
      'preview.ifBandwidthHz',
      Number.MIN_VALUE,
    ),
    frequencyHz: array(item.frequencyHz, 'preview.frequencyHz', count).map(
      (entry) => number(entry, 'preview.frequencyHz', Number.MIN_VALUE),
    ),
    valid: array(item.valid, 'preview.valid', count).map((entry) =>
      flag(entry, 'preview.valid'),
    ),
    real: values('real'),
    imag: values('imag'),
    dbValues: values('dbValues'),
    dbStates: states('dbStates'),
    phaseValues: values('phaseValues'),
    phaseStates: states('phaseStates'),
  }
  if (!/^S[1-4][1-4]$/.test(result.parameter)) invalid('preview.parameter')
  if (result.decimated !== original > count) invalid('preview.decimated')
  for (const field of [
    'sampleIndexes',
    'frequencyHz',
    'valid',
    'real',
    'imag',
    'dbValues',
    'dbStates',
    'phaseValues',
    'phaseStates',
  ] as const)
    if (result[field].length !== count) invalid(`preview.${field} count`)
  if (
    result.frequencyHz.some(
      (entry, index) => index > 0 && entry <= result.frequencyHz[index - 1]!,
    ) ||
    result.sampleIndexes.some(
      (entry, index) => index > 0 && entry <= result.sampleIndexes[index - 1]!,
    )
  )
    invalid('preview.axis ordering')
  if (
    result.valid.some(
      (valid, index) =>
        valid && (result.real[index] === null || result.imag[index] === null),
    )
  )
    invalid('preview.valid complex data')
  for (const [projected, projectedStates] of [
    [result.dbValues, result.dbStates],
    [result.phaseValues, result.phaseStates],
  ] as const)
    if (
      projected.some(
        (entry, index) =>
          (projectedStates[index] === 'finite') !== (entry !== null),
      )
    )
      invalid('preview.projection states')
  return result
}
