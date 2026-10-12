import { legacyProtocolFields } from './displayFixtures'
import assert from 'node:assert/strict'
import test from 'node:test'
import { decodeFrame, decodeStatus } from '../src/api/decoders'
import { displaySnapshot, projection } from './displayFixtures'
import { sweepWithinCapabilities } from '../src/domain/sweep'

const capabilities = {
  portCount: 4,
  minFrequencyHz: 10e6,
  maxFrequencyHz: 8e9,
  minSweepPoints: 2,
  maxSweepPoints: 10001,
}

function status(receiverPort = 1, sourcePort = 2) {
  const measurement = {
    name: 'Reverse',
    kind: 'sParameter',
    receiverKind: 'b',
    receiverPort,
    sourcePort,
    parameter: `S${receiverPort}${sourcePort}`,
    unit: '1',
    calibration: 'uncalibrated',
  }
  return {
    display: displaySnapshot([measurement]),
    identity: 'VnaPro,NativeSimulator,0,0.2',
    backend: 'NativeSimulator',
    state: 'Ready',
    error: '',
    pending: false,
    revision: '1',
    acquisitionRevision: '1',
    runId: '1',
    activeConfigValid: true,
    capabilities,
    config: {
      startHz: 10e6,
      stopHz: 8e9,
      points: 201,
      ifBandwidthHz: 1000,
      triggerSource: 'IMM',
      sweepMode: 'HOLD',
    },
    measurements: [
      {
        name: 'Reverse',
        kind: 'sParameter',
        receiverKind: 'b',
        receiverPort,
        sourcePort,
        parameter: `S${receiverPort}${sourcePort}`,
        unit: '1',
        calibration: 'uncalibrated',
      },
    ],
    control: {
      owner: 'none',
      hasControl: false,
      releasing: false,
      leaseRemainingMs: 0,
      readOnlyReason: '',
    },
  }
}

test('reverse S12 and S22 ratios decode with the source-2 reference', () => {
  for (const port of [1, 2]) {
    const value = status(port)
    assert.deepEqual(decodeStatus(value).measurements, value.measurements)
    const frame = {
      available: true,
      current: true,
      channelId: 1,
      runId: '1',
      revision: '1',
      measurementRevision: '1',
      displayRevision: '1',
      sampleIndexes: [0, 1],
      frameSeq: '1',
      originalPointCount: 2,
      displayPointCount: 2,
      decimated: false,
      frequencyHz: [10e6, 8e9],
      traces: [
        {
          ...value.measurements[0],
          available: true,
          real: [0.2, 0.3],
          imag: [0.1, 0.2],
          valid: [true, true],
          projection: projection([-13, -10]),
        },
      ],
    }
    assert.deepEqual(legacyProtocolFields(decodeFrame(frame)), frame)
  }
})

test('input drafts use server capability bounds without writing derived measurement definitions', () => {
  assert.equal(
    sweepWithinCapabilities(
      { startHz: 10e6, stopHz: 8e9, points: 201, ifBandwidthHz: 1000 },
      capabilities,
    ),
    true,
  )
  assert.equal(
    sweepWithinCapabilities(
      { startHz: 10e6 - 1, stopHz: 8e9, points: 201, ifBandwidthHz: 1000 },
      capabilities,
    ),
    false,
  )
  assert.equal(
    sweepWithinCapabilities(
      { startHz: 8e9, stopHz: 8e9, points: 2, ifBandwidthHz: 1000 },
      capabilities,
    ),
    true,
  )
})

test('status capabilities govern frequency, point and port bounds for two and four ports', () => {
  assert.deepEqual(legacyProtocolFields(decodeStatus(status())), status())
  const twoPort = {
    ...status(),
    capabilities: { ...capabilities, portCount: 2 },
  }
  assert.deepEqual(legacyProtocolFields(decodeStatus(twoPort)), twoPort)
  const missing = { ...status() } as Record<string, unknown>
  delete missing.capabilities
  assert.throws(() => decodeStatus(missing))
  assert.throws(() =>
    decodeStatus({
      ...status(),
      capabilities: { ...capabilities, portCount: 0 },
    }),
  )
  assert.throws(() =>
    decodeStatus({ ...status(), config: { ...status().config, startHz: 9e6 } }),
  )
  assert.throws(() =>
    decodeStatus({
      ...status(),
      config: { ...status().config, points: 10002 },
    }),
  )
  const raw = {
    ...twoPort,
    measurements: [
      {
        name: 'Raw',
        kind: 'receiver',
        receiverKind: 'a',
        receiverPort: 3,
        sourcePort: 1,
        parameter: 'a3,1',
        unit: 'a.u.',
        calibration: 'raw',
      },
    ],
  }
  assert.throws(() => decodeStatus(raw))
})
