import type {
  AcceptedOperation,
  AcquiredFrame,
  ClaimResponse,
  ChannelStatus,
  CoreState,
  FrameResponse,
  InstrumentCapabilities,
  InstrumentStatus,
  Measurement,
  OperationSnapshot,
  ReceiverTrace,
} from './types'

import { MAX_TRACES } from '../domain/presentation'
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
import { decodeDisplay, decodeProjection } from './displayDecoders'
import { decodeCalibration } from './calibrationDecoders'
import { FRAME_POINT_LIMIT, FRAME_SAMPLE_BUDGET } from './frameLimits'
import { decodeSweepProgress, validateProgressChannels } from './sweepProgressDecoder'

function measurement(value: unknown): Measurement {
  const item = object(value, 'measurement')
  const name = text(item.name, 'measurement.name')
  if (!name || name.length > 128) invalid('measurement.name')
  const result: Measurement = {
    name,
    ...(item.channelId === undefined ? {} : { channelId: integer(item.channelId, 'measurement.channelId', 1, 65535) }),
    kind: choice(item.kind, 'measurement.kind', ['receiver', 'sParameter']),
    receiverKind: choice(item.receiverKind, 'measurement.receiverKind', [
      'a',
      'b',
    ]),
    receiverPort: integer(item.receiverPort, 'measurement.receiverPort', 1, 4),
    sourcePort: integer(item.sourcePort, 'measurement.sourcePort', 1, 4),
    parameter: text(item.parameter, 'measurement.parameter'),
    unit: choice(item.unit, 'measurement.unit', ['a.u.', '1']),
    calibration: choice(item.calibration, 'measurement.calibration', [
      'raw',
      'uncalibrated',
      'calibrated',
    ]),
  }
  if (result.kind === 'receiver') {
    const parameter = `${result.receiverKind}${result.receiverPort},${result.sourcePort}`
    if (
      result.parameter !== parameter ||
      result.unit !== 'a.u.' ||
      result.calibration !== 'raw'
    ) {
      invalid('receiver metadata')
    }
  } else {
    const parameter = `S${result.receiverPort}${result.sourcePort}`
    if (
      result.receiverKind !== 'b' ||
      result.parameter !== parameter ||
      result.unit !== '1' ||
      !['uncalibrated', 'calibrated'].includes(result.calibration)
    ) {
      invalid('S-parameter metadata')
    }
  }
  return result
}

function sweepTiming(config: Record<string, unknown>) {
  if (config.sweepTimeAuto === undefined && config.sweepTimeSeconds === undefined) return {}
  return {
    sweepTimeAuto: flag(config.sweepTimeAuto, 'config.sweepTimeAuto'),
    sweepTimeSeconds: number(config.sweepTimeSeconds, 'config.sweepTimeSeconds', Number.MIN_VALUE, 100000),
  }
}

function decodeChannel(value: unknown, capabilities: InstrumentCapabilities): ChannelStatus {
  const item = object(value, 'channel')
  const config = object(item.config, 'channel.config')
  const startHz = number(config.startHz, 'channel.startHz', capabilities.minFrequencyHz, capabilities.maxFrequencyHz)
  const stopHz = number(config.stopHz, 'channel.stopHz', capabilities.minFrequencyHz, capabilities.maxFrequencyHz)
  if (startHz > stopHz) invalid('channel frequency range')
  let stimulusZoom
  if (item.stimulusZoom !== undefined) {
    const zoom = object(item.stimulusZoom, 'channel.stimulusZoom')
    stimulusZoom = {
      available: flag(zoom.available, 'channel.stimulusZoom.available'),
      canZoomOut: flag(zoom.canZoomOut, 'channel.stimulusZoom.canZoomOut'),
      reason: text(zoom.reason, 'channel.stimulusZoom.reason'),
      depth: integer(zoom.depth, 'channel.stimulusZoom.depth', 0, 16),
    }
  }
  return {
    channelId: integer(item.channelId, 'channel.channelId', 1, 65535),
    name: text(item.name, 'channel.name'),
    state: choice<CoreState>(item.state, 'channel.state', ['Uninitialized', 'Ready', 'Running', 'Faulted', 'Closed']),
    error: text(item.error, 'channel.error'),
    acquisitionRevision: identity(item.acquisitionRevision, 'channel.acquisitionRevision'),
    runId: identity(item.runId, 'channel.runId'),
    activeConfigValid: flag(item.activeConfigValid, 'channel.activeConfigValid'),
    ...(item.lastSweepTimeSeconds === undefined ? {} : { lastSweepTimeSeconds: number(item.lastSweepTimeSeconds, 'channel.lastSweepTimeSeconds', 0) }),
    config: {
      ...sweepTiming(config),
      startHz, stopHz,
      points: integer(config.points, 'channel.points', capabilities.minSweepPoints, capabilities.maxSweepPoints),
      ifBandwidthHz: number(config.ifBandwidthHz, 'channel.ifBandwidthHz', 10),
      ...(config.powerDbm === undefined ? {} : { powerDbm: number(config.powerDbm, 'channel.powerDbm', -120, 100) }),
      triggerSource: choice(config.triggerSource, 'channel.triggerSource', ['IMM', 'MAN']),
      sweepMode: choice(config.sweepMode, 'channel.sweepMode', ['HOLD', 'SING', 'CONT']),
      ...(config.selectedSweepMode === undefined ? {} : { selectedSweepMode: choice<'SING' | 'CONT'>(config.selectedSweepMode, 'channel.selectedSweepMode', ['SING', 'CONT']) }),
    },
    ...(stimulusZoom === undefined ? {} : { stimulusZoom }),
    ...(item.calibration === undefined ? {} : { calibration: decodeCalibration(item.calibration, capabilities.portCount) }),
  }
}

export function decodeStatus(value: unknown): InstrumentStatus {
  const item = object(value, 'status')
  const config = object(item.config, 'config')
  const control = object(item.control, 'control')
  const limits = object(item.capabilities, 'capabilities')
  const portCount = integer(limits.portCount, 'capabilities.portCount', 1, 4)
  const capabilities: InstrumentCapabilities = {
    portCount,
    minFrequencyHz: number(
      limits.minFrequencyHz,
      'capabilities.minFrequencyHz',
      Number.MIN_VALUE,
    ),
    maxFrequencyHz: number(
      limits.maxFrequencyHz,
      'capabilities.maxFrequencyHz',
      Number.MIN_VALUE,
    ),
    minSweepPoints: integer(
      limits.minSweepPoints,
      'capabilities.minSweepPoints',
      2,
      10001,
    ),
    maxSweepPoints: integer(
      limits.maxSweepPoints,
      'capabilities.maxSweepPoints',
      2,
      10001,
    ),
    ...(limits.minPowerDbm === undefined ? {} : { minPowerDbm: number(limits.minPowerDbm, 'capabilities.minPowerDbm') }),
    ...(limits.maxPowerDbm === undefined ? {} : { maxPowerDbm: number(limits.maxPowerDbm, 'capabilities.maxPowerDbm') }),
    ...(limits.manualSweepTimeSupported === undefined ? {} : { manualSweepTimeSupported: flag(limits.manualSweepTimeSupported, 'capabilities.manualSweepTimeSupported') }),
  }
  if (
    capabilities.minFrequencyHz > capabilities.maxFrequencyHz ||
    capabilities.minSweepPoints > capabilities.maxSweepPoints ||
    ((capabilities.minPowerDbm !== undefined || capabilities.maxPowerDbm !== undefined) &&
      (capabilities.minPowerDbm === undefined || capabilities.maxPowerDbm === undefined || capabilities.minPowerDbm > capabilities.maxPowerDbm))
  )
    invalid('capabilities bounds')
  const startHz = number(
    config.startHz,
    'config.startHz',
    capabilities.minFrequencyHz,
    capabilities.maxFrequencyHz,
  )
  const stopHz = number(
    config.stopHz,
    'config.stopHz',
    capabilities.minFrequencyHz,
    capabilities.maxFrequencyHz,
  )
  if (startHz > stopHz) invalid('config.startHz / stopHz')
  const measurements = array(item.measurements, 'measurements', MAX_TRACES).map(
    measurement,
  )
  if (
    new Set(measurements.map((entry) => entry.name)).size !==
    measurements.length
  )
    invalid('measurement names')
  if (
    measurements.some(
      (entry) => entry.receiverPort > portCount || entry.sourcePort > portCount,
    )
  )
    invalid('measurement port capability')
  const result: InstrumentStatus = {
    identity: text(item.identity, 'identity'),
    backend: text(item.backend, 'backend'),
    state: choice<CoreState>(item.state, 'state', [
      'Uninitialized',
      'Ready',
      'Running',
      'Faulted',
      'Closed',
    ]),
    error: text(item.error, 'error'),
    pending: flag(item.pending, 'pending'),
    revision: identity(item.revision, 'revision'),
    acquisitionRevision: identity(
      item.acquisitionRevision,
      'acquisitionRevision',
    ),
    runId: identity(item.runId, 'runId'),
    activeConfigValid: flag(item.activeConfigValid, 'activeConfigValid'),
    ...(item.lastSweepTimeSeconds === undefined ? {} : { lastSweepTimeSeconds: number(item.lastSweepTimeSeconds, 'lastSweepTimeSeconds', 0) }),
    capabilities,
    ...(item.activeChannelId === undefined ? {} : { activeChannelId: integer(item.activeChannelId, 'activeChannelId', 1, 65535) }),
    ...(item.channels === undefined ? {} : { channels: array(item.channels, 'channels', 16).map((channel) => decodeChannel(channel, capabilities)) }),
    ...(item.stimulusZoom === undefined ? {} : (() => {
      const zoom = object(item.stimulusZoom, 'stimulusZoom')
      return { stimulusZoom: {
        available: flag(zoom.available, 'stimulusZoom.available'),
        canZoomOut: flag(zoom.canZoomOut, 'stimulusZoom.canZoomOut'),
        reason: text(zoom.reason, 'stimulusZoom.reason'),
        depth: integer(zoom.depth, 'stimulusZoom.depth', 0, 16),
      } }
    })()),
    config: {
      ...sweepTiming(config),
      startHz,
      stopHz,
      points: integer(
        config.points,
        'config.points',
        capabilities.minSweepPoints,
        capabilities.maxSweepPoints,
      ),
      ifBandwidthHz: number(config.ifBandwidthHz, 'config.ifBandwidthHz', 10),
      ...(config.powerDbm === undefined ? {} : { powerDbm: number(config.powerDbm, 'config.powerDbm', -120, 100) }),
      triggerSource: choice(config.triggerSource, 'config.triggerSource', [
        'IMM',
        'MAN',
      ]),
      sweepMode: choice(config.sweepMode, 'config.sweepMode', [
        'HOLD',
        'SING',
        'CONT',
      ]),
      ...(config.selectedSweepMode === undefined ? {} : {
        selectedSweepMode: choice<'SING' | 'CONT'>(config.selectedSweepMode, 'config.selectedSweepMode', ['SING', 'CONT']),
      }),
    },
    measurements,
    display: decodeDisplay(item.display, measurements),
    ...(item.calibration === undefined ? {} : { calibration: decodeCalibration(item.calibration, portCount) }),
    control: {
      owner: choice(control.owner, 'control.owner', ['none', 'scpi', 'web']),
      hasControl: flag(control.hasControl, 'control.hasControl'),
      releasing: flag(control.releasing, 'control.releasing'),
      leaseRemainingMs: number(
        control.leaseRemainingMs,
        'control.leaseRemainingMs',
        0,
      ),
      readOnlyReason: text(control.readOnlyReason, 'control.readOnlyReason'),
    },
  }
  if (result.channels) {
    const ids = new Set(result.channels.map((channel) => channel.channelId))
    if (!ids.size || ids.size !== result.channels.length ||
      !ids.has(result.activeChannelId ?? 1) ||
      measurements.some((entry) => !ids.has(entry.channelId ?? 1))) invalid('channel membership')
  }
  return result
}

function trace(value: unknown, count: number): ReceiverTrace {
  const item = object(value, 'trace')
  const real = array(item.real, 'trace.real', FRAME_POINT_LIMIT).map((entry) =>
    entry === null ? null : number(entry, 'trace.real'),
  )
  const imag = array(item.imag, 'trace.imag', FRAME_POINT_LIMIT).map((entry) =>
    entry === null ? null : number(entry, 'trace.imag'),
  )
  const valid = array(item.valid, 'trace.valid', FRAME_POINT_LIMIT).map(
    (entry) => flag(entry, 'trace.valid'),
  )
  if (
    [real.length, imag.length, valid.length].some((length) => length !== count)
  )
    invalid('trace point count')
  if (
    valid.some(
      (entry, index) => entry && (real[index] === null || imag[index] === null),
    )
  )
    invalid('trace valid data')
  return {
    ...measurement(item),
    available: flag(item.available, 'trace.available'),
    real,
    imag,
    valid,
    projection: decodeProjection(item.projection, count),
  }
}

function decodeCompleteFrameEntry(value: unknown): FrameResponse {
  const item = object(value, 'frame')
  if (!flag(item.available, 'frame.available')) {
    if (flag(item.current, 'frame.current'))
      invalid('unavailable frame.current')
    return {
      available: false,
      current: false,
      reason: text(item.reason, 'frame.reason'),
      ...(item.channelId === undefined ? {} : { channelId: integer(item.channelId, 'frame.channelId', 1, 65535) }),
    }
  }
  const count = integer(
    item.displayPointCount,
    'frame.displayPointCount',
    1,
    FRAME_POINT_LIMIT,
  )
  const originalCount = integer(
    item.originalPointCount,
    'frame.originalPointCount',
    count,
    FRAME_POINT_LIMIT,
  )
  const axis = array(
    item.frequencyHz,
    'frame.frequencyHz',
    FRAME_POINT_LIMIT,
  ).map((entry) => number(entry, 'frame.frequencyHz', Number.MIN_VALUE))
  if (
    axis.length !== count ||
    axis.some((entry, index) => index > 0 && entry < axis[index - 1]!)
  )
    invalid('frame axis')
  const decimated = flag(item.decimated, 'frame.decimated')
  if (decimated !== originalCount > count) invalid('frame.decimated')
  const entries = array(item.traces, 'frame.traces', MAX_TRACES)
  if (entries.length * count > FRAME_SAMPLE_BUDGET)
    invalid('frame sample budget')
  const traces = entries.map((entry) => trace(entry, count))
  if (new Set(traces.map((entry) => entry.name)).size !== traces.length)
    invalid('trace names')
  const result: AcquiredFrame = {
    available: true,
    current: flag(item.current, 'frame.current'),
    channelId: integer(item.channelId, 'frame.channelId', 1, 65535),
    runId: identity(item.runId, 'frame.runId'),
    revision: identity(item.revision, 'frame.revision'),
    measurementRevision: identity(
      item.measurementRevision,
      'frame.measurementRevision',
    ),
    displayRevision: identity(item.displayRevision, 'frame.displayRevision'),
    ...(item.calibrationRevision === undefined ? {} : { calibrationRevision: identity(item.calibrationRevision, 'frame.calibrationRevision') }),
    frameSeq: identity(item.frameSeq, 'frame.frameSeq'),
    originalPointCount: originalCount,
    displayPointCount: count,
    decimated,
    frequencyHz: axis,
    sampleIndexes: array(
      item.sampleIndexes,
      'frame.sampleIndexes',
      FRAME_POINT_LIMIT,
    ).map((entry) => integer(entry, 'frame.sampleIndex', 0, originalCount - 1)),
    traces,
  }
  if (
    result.sampleIndexes.length !== count ||
    result.sampleIndexes.some(
      (index, position) =>
        position > 0 && index <= result.sampleIndexes[position - 1]!,
    )
  )
    invalid('frame.sampleIndexes')
  return result
}

function decodeFrameEntry(value: unknown): FrameResponse {
  const item = object(value, 'frame')
  const result = decodeCompleteFrameEntry(item)
  return item.sweepProgress === undefined ? result : {
    ...result,
    sweepProgress: decodeSweepProgress(item.sweepProgress, result.channelId, decodeCompleteFrameEntry),
  }
}

export function decodeFrame(value: unknown): FrameResponse {
  const item = object(value, 'frame')
  const result = decodeFrameEntry(item)
  if (item.channelFrames === undefined) return result
  const channelFrames = array(item.channelFrames, 'frame.channelFrames', 16).map(decodeFrameEntry)
  validateProgressChannels(channelFrames)
  const ids = new Set(channelFrames.map((frame) => frame.channelId))
  const names = channelFrames.flatMap((frame) => frame.available ? frame.traces.map((trace) => trace.name) : [])
  if (!ids.size || ids.has(undefined) || ids.size !== channelFrames.length || names.length > MAX_TRACES || new Set(names).size !== names.length ||
    channelFrames.reduce((sum, frame) => sum + (frame.available ? frame.displayPointCount * frame.traces.length : 0), 0) > FRAME_SAMPLE_BUDGET ||
    channelFrames.some((frame) => frame.available && frame.traces.some((trace) => (trace.channelId ?? frame.channelId) !== frame.channelId)))
    invalid('frame channel membership')
  return { ...result, channelFrames }
}

export function decodeClaim(value: unknown): ClaimResponse {
  const item = object(value, 'claim')
  const sessionId = text(item.sessionId, 'claim.sessionId')
  if (
    !sessionId ||
    sessionId.length > 128 ||
    !flag(item.hasControl, 'claim.hasControl')
  )
    invalid('claim control')
  return {
    sessionId,
    hasControl: true,
    leaseSeconds: number(item.leaseSeconds, 'claim.leaseSeconds', 1),
  }
}

export function decodeAccepted(value: unknown): AcceptedOperation {
  const item = object(value, 'operation')
  const operationId = text(item.operationId, 'operation.operationId')
  if (!operationId || operationId.length > 128) invalid('operation.operationId')
  return {
    operationId,
    state: choice(item.state, 'operation.state', [
      'pending',
      'completed',
      'failed',
    ]),
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
