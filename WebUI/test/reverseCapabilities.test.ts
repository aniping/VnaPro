import assert from 'node:assert/strict'
import test from 'node:test'
import { decodeFrame, decodeStatus } from '../src/api/decoders'
import { measurementCandidate } from '../src/domain/measurements'
import { sweepWithinCapabilities } from '../src/domain/sweep'

const capabilities = {
  portCount: 4, minFrequencyHz: 10e6, maxFrequencyHz: 8e9,
  minSweepPoints: 2, maxSweepPoints: 10001,
}

function status(receiverPort = 1, sourcePort = 2) {
  return {
    identity: 'VnaPro,NativeSimulator,0,0.2', backend: 'NativeSimulator', state: 'Ready',
    error: '', pending: false, revision: '1', runId: '1', activeConfigValid: true,
    capabilities,
    config: { startHz: 10e6, stopHz: 8e9, points: 201, ifBandwidthHz: 1000, triggerSource: 'IMM', sweepMode: 'HOLD' },
    measurements: [{ name: 'Reverse', kind: 'sParameter', receiverKind: 'b', receiverPort, sourcePort,
      parameter: `S${receiverPort}${sourcePort}`, unit: '1', calibration: 'uncalibrated' }],
    control: { owner: 'none', hasControl: false, releasing: false, leaseRemainingMs: 0, readOnlyReason: '' },
  }
}

test('reverse S12 and S22 ratios decode with the source-2 reference', () => {
  for (const port of [1, 2]) {
    const value = status(port)
    assert.deepEqual(decodeStatus(value).measurements, value.measurements)
    const frame = {
      available: true, current: true, channelId: 1, runId: '1', revision: '1', frameSeq: '1',
      originalPointCount: 2, displayPointCount: 2, decimated: false, frequencyHz: [10e6, 8e9],
      traces: [{ ...value.measurements[0], available: true, real: [0.2, 0.3], imag: [0.1, 0.2], valid: [true, true] }],
    }
    assert.deepEqual(decodeFrame(frame), frame)
  }
})

test('reverse definitions derive receiver and source ports without changing the RAW draft', () => {
  const draft = { name: 'Reverse', kind: 'receiver' as const, receiverKind: 'a' as const, receiverPort: 4, sourcePort: 3 }
  assert.deepEqual(measurementCandidate('S12', draft), { name: 'Reverse', kind: 'sParameter', receiverKind: 'b', receiverPort: 1, sourcePort: 2 })
  assert.deepEqual(measurementCandidate('S22', draft), { name: 'Reverse', kind: 'sParameter', receiverKind: 'b', receiverPort: 2, sourcePort: 2 })
  assert.equal(draft.sourcePort, 3)
  assert.equal(sweepWithinCapabilities({ startHz: 10e6, stopHz: 8e9, points: 201, ifBandwidthHz: 1000 }, capabilities), true)
  assert.equal(sweepWithinCapabilities({ startHz: 10e6 - 1, stopHz: 8e9, points: 201, ifBandwidthHz: 1000 }, capabilities), false)
  assert.equal(sweepWithinCapabilities({ startHz: 8e9, stopHz: 8e9, points: 2, ifBandwidthHz: 1000 }, capabilities), true)
})

test('status capabilities govern frequency, point and port bounds for two and four ports', () => {
  assert.deepEqual(decodeStatus(status()), status())
  const twoPort = { ...status(), capabilities: { ...capabilities, portCount: 2 } }
  assert.deepEqual(decodeStatus(twoPort), twoPort)
  const missing = { ...status() } as Record<string, unknown>
  delete missing.capabilities
  assert.throws(() => decodeStatus(missing))
  assert.throws(() => decodeStatus({ ...status(), capabilities: { ...capabilities, portCount: 0 } }))
  assert.throws(() => decodeStatus({ ...status(), config: { ...status().config, startHz: 9e6 } }))
  assert.throws(() => decodeStatus({ ...status(), config: { ...status().config, points: 10002 } }))
  const raw = { ...twoPort, measurements: [{ name: 'Raw', kind: 'receiver', receiverKind: 'a',
    receiverPort: 3, sourcePort: 1, parameter: 'a3,1', unit: 'a.u.', calibration: 'raw' }] }
  assert.throws(() => decodeStatus(raw))
})
