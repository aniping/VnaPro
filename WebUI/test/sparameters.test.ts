import { legacyProtocolFields } from './displayFixtures'
import assert from 'node:assert/strict'
import test from 'node:test'
import { decodeFrame, decodeStatus } from '../src/api/decoders'
import type { AcquiredFrame, Measurement } from '../src/api/types'
import { measurementState } from '../src/domain/measurements'
import { measurementFormatInfo, scaleValue } from '../src/plot/formatting'
import { displaySeries } from '../src/plot/traceMath'
import { displaySnapshot, projection } from './displayFixtures'

function sParameter(receiverPort = 1): Measurement {
  return {
    name: `S${receiverPort}1`,
    kind: 'sParameter',
    receiverKind: 'b',
    receiverPort,
    sourcePort: 1,
    parameter: `S${receiverPort}1`,
    unit: '1',
    calibration: 'uncalibrated',
  }
}

function snapshot(measurement: Measurement) {
  return {
    identity: 'VnaPro,NativeSimulator,0,0.2',
    backend: 'NativeSimulator',
    state: 'Ready',
    error: '',
    pending: false,
    revision: '2',
    acquisitionRevision: '2',
    runId: '1',
    activeConfigValid: true,
    capabilities: {
      portCount: 4,
      minFrequencyHz: 1,
      maxFrequencyHz: 8e9,
      minSweepPoints: 2,
      maxSweepPoints: 10001,
    },
    config: {
      startHz: 1e6,
      stopHz: 100e6,
      points: 201,
      ifBandwidthHz: 1000,
      triggerSource: 'IMM',
      sweepMode: 'HOLD',
    },
    measurements: [measurement],
    display: displaySnapshot([measurement]),
    control: {
      owner: 'web',
      hasControl: true,
      releasing: false,
      leaseRemainingMs: 30000,
      readOnlyReason: '',
    },
  }
}

function frame(): AcquiredFrame {
  return {
    available: true,
    current: true,
    channelId: 1,
    runId: '1',
    revision: '2',
    measurementRevision: '2',
    displayRevision: '1',
    sampleIndexes: [0, 1, 2],
    frameSeq: '3',
    originalPointCount: 3,
    displayPointCount: 3,
    decimated: false,
    frequencyHz: [1e6, 2e6, 3e6],
    traces: [
      {
        ...sParameter(2),
        available: true,
        real: [0.5, 0, null],
        imag: [0, 0.25, null],
        valid: [true, true, false],
        projection: projection([-6.02, -12.04, null]),
      },
    ],
  }
}

test('S11 and S21 metadata decode only as uncalibrated dimensionless port-1 ratios', () => {
  for (const port of [1, 2]) {
    const measurement = sParameter(port)
    assert.deepEqual(
      decodeStatus(snapshot(measurement)).measurements[0],
      measurement,
    )
  }
  assert.deepEqual(legacyProtocolFields(decodeFrame(frame())), frame())
  const malformed: Partial<Measurement>[] = [
    { receiverKind: 'a' },
    { sourcePort: 2 },
    { receiverPort: 3 },
    { parameter: 'b1,1' },
    { unit: 'a.u.' },
    { calibration: 'raw' },
    { kind: 'receiver' },
  ]
  for (const override of malformed) {
    assert.throws(() =>
      decodeStatus(snapshot({ ...sParameter(), ...override })),
    )
    const invalidFrame = frame()
    invalidFrame.traces[0] = { ...invalidFrame.traces[0]!, ...override }
    assert.throws(() => decodeFrame(invalidFrame))
  }
  const missingKind = { ...sParameter() } as Record<string, unknown>
  delete missingKind.kind
  assert.throws(() =>
    decodeStatus({ ...snapshot(sParameter()), measurements: [missingKind] }),
  )
})

test('RAW metadata requires a matching receiver parameter and raw amplitude units', () => {
  const raw: Measurement = {
    name: 'A42',
    kind: 'receiver',
    receiverKind: 'a',
    receiverPort: 4,
    sourcePort: 2,
    parameter: 'a4,2',
    unit: 'a.u.',
    calibration: 'raw',
  }
  assert.deepEqual(decodeStatus(snapshot(raw)).measurements[0], raw)
  assert.throws(() => decodeStatus(snapshot({ ...raw, parameter: 'a2,4' })))
  assert.throws(() => decodeStatus(snapshot({ ...raw, unit: '1' })))
  assert.throws(() =>
    decodeStatus(snapshot({ ...raw, calibration: 'uncalibrated' })),
  )
})

test('S parameter units and the backend format projection preserve calibration metadata', () => {
  const acquired = frame()
  const trace = acquired.traces[0]!
  assert.deepEqual(displaySeries(trace, 'db')[0]!.values, [-6.02, -12.04, NaN])
  for (const format of ['magnitude', 'real', 'imag', 'realimag'] as const)
    assert.equal(measurementFormatInfo(format, trace).unit, '1')
  assert.equal(measurementFormatInfo('db', trace).unit, 'dB')
  assert.equal(measurementFormatInfo('phase', trace).unit, 'deg')
  assert.equal(scaleValue(0.1, 'real', 5, trace), '0.1 1')
  assert.equal(measurementState(trace), '未校准')
})

test('negative infinity and undefined backend states remain distinct without local measurement math', () => {
  const trace = frame().traces[0]!
  trace.projection = projection([null, null, 1], 'db')
  trace.projection.valueStates = ['negativeInfinity', 'undefined', 'finite']
  assert.deepEqual(displaySeries(trace, 'db')[0]!.values, [-Infinity, NaN, 1])
})

test('CW status and repeated acquired frequencies decode without allowing a descending axis', () => {
  const status = snapshot(sParameter())
  status.config.startHz = 1e9
  status.config.stopHz = 1e9
  assert.deepEqual(decodeStatus(status).config, status.config)
  assert.throws(() =>
    decodeStatus({
      ...status,
      config: { ...status.config, startHz: 1e9, stopHz: 1e9 - 1 },
    }),
  )
  const acquired = frame()
  acquired.frequencyHz = [1e9, 1e9, 1e9]
  assert.deepEqual(legacyProtocolFields(decodeFrame(acquired)), acquired)
  acquired.frequencyHz = [1e6, 1e6, 2e6]
  assert.deepEqual(legacyProtocolFields(decodeFrame(acquired)), acquired)
  acquired.frequencyHz = [1e6, 1e6 - 1, 2e6]
  assert.throws(() => decodeFrame(acquired))
})
