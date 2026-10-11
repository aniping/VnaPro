import assert from 'node:assert/strict'
import test from 'node:test'
import { decodeFrame, decodeStatus } from '../src/api/decoders'
import { InstrumentApi } from '../src/api/instrumentApi'
import type { AcquiredFrame, Measurement, MeasurementCandidate } from '../src/api/types'
import { measurementCandidate, measurementState } from '../src/domain/measurements'
import { defaultTracePresentation } from '../src/domain/presentation'
import { exportVisibleFrameCsv } from '../src/plot/exportCsv'
import { measurementFormatInfo, scaleValue } from '../src/plot/formatting'
import { getMarkerReadout } from '../src/plot'
import { displaySeries, displayValue } from '../src/plot/traceMath'

function sParameter(receiverPort = 1): Measurement {
  return {
    name: `S${receiverPort}1`, kind: 'sParameter', receiverKind: 'b', receiverPort, sourcePort: 1,
    parameter: `S${receiverPort}1`, unit: '1', calibration: 'uncalibrated',
  }
}

function snapshot(measurement: Measurement) {
  return {
    identity: 'VnaPro,NativeSimulator,0,0.2', backend: 'NativeSimulator', state: 'Ready', error: '', pending: false,
    revision: '2', runId: '1', activeConfigValid: true,
    config: { startHz: 1e6, stopHz: 100e6, points: 201, ifBandwidthHz: 1000, triggerSource: 'IMM', sweepMode: 'HOLD' },
    measurements: [measurement],
    control: { owner: 'web', hasControl: true, releasing: false, leaseRemainingMs: 30000, readOnlyReason: '' },
  }
}

function frame(): AcquiredFrame {
  return {
    available: true, current: true, channelId: 1, runId: '1', revision: '2', frameSeq: '3',
    originalPointCount: 3, displayPointCount: 3, decimated: false, frequencyHz: [1e6, 2e6, 3e6],
    traces: [{ ...sParameter(2), available: true, real: [0.5, 0, null], imag: [0, 0.25, null], valid: [true, true, false] }],
  }
}

test('S11 and S21 metadata decode only as uncalibrated dimensionless port-1 ratios', () => {
  for (const port of [1, 2]) {
    const measurement = sParameter(port)
    assert.deepEqual(decodeStatus(snapshot(measurement)).measurements[0], measurement)
  }
  assert.deepEqual(decodeFrame(frame()), frame())
  const malformed: Partial<Measurement>[] = [
    { receiverKind: 'a' }, { sourcePort: 2 }, { receiverPort: 3 },
    { parameter: 'b1,1' }, { unit: 'a.u.' }, { calibration: 'raw' },
    { kind: 'receiver' },
  ]
  for (const override of malformed) {
    assert.throws(() => decodeStatus(snapshot({ ...sParameter(), ...override })))
    const invalidFrame = frame()
    invalidFrame.traces[0] = { ...invalidFrame.traces[0]!, ...override }
    assert.throws(() => decodeFrame(invalidFrame))
  }
  const missingKind = { ...sParameter() } as Record<string, unknown>
  delete missingKind.kind
  assert.throws(() => decodeStatus({ ...snapshot(sParameter()), measurements: [missingKind] }))
})

test('RAW metadata requires a matching receiver parameter and raw amplitude units', () => {
  const raw: Measurement = {
    name: 'A42', kind: 'receiver', receiverKind: 'a', receiverPort: 4, sourcePort: 2,
    parameter: 'a4,2', unit: 'a.u.', calibration: 'raw',
  }
  assert.deepEqual(decodeStatus(snapshot(raw)).measurements[0], raw)
  assert.throws(() => decodeStatus(snapshot({ ...raw, parameter: 'a2,4' })))
  assert.throws(() => decodeStatus(snapshot({ ...raw, unit: '1' })))
  assert.throws(() => decodeStatus(snapshot({ ...raw, calibration: 'uncalibrated' })))
})

test('measurement choices normalize S-parameter ports while retaining an independent RAW draft', () => {
  const draft: MeasurementCandidate = {
    name: 'DUT', kind: 'receiver', receiverKind: 'a', receiverPort: 4, sourcePort: 3,
  }
  assert.deepEqual(measurementCandidate('S11', draft), {
    name: 'DUT', kind: 'sParameter', receiverKind: 'b', receiverPort: 1, sourcePort: 1,
  })
  assert.deepEqual(measurementCandidate('S21', draft), {
    name: 'DUT', kind: 'sParameter', receiverKind: 'b', receiverPort: 2, sourcePort: 1,
  })
  assert.deepEqual(measurementCandidate('RAW', draft), draft)
  assert.equal(draft.receiverPort, 4)
  assert.equal(measurementState(sParameter()), '未校准')
})

test('S-parameter formats, markers and CSV retain ratios, phase, invalid gaps and calibration metadata', () => {
  const acquired = frame()
  const trace = acquired.traces[0]!
  const db = displaySeries(trace, 'db')[0]!.values
  assert.ok(Math.abs(db[0]! - (-6.020599913279624)) < 1e-12)
  assert.ok(Math.abs(db[1]! - (-12.041199826559248)) < 1e-12)
  assert.ok(Number.isNaN(db[2]))
  assert.deepEqual(displaySeries(trace, 'phase')[0]!.values.slice(0, 2), [0, 90])
  for (const format of ['magnitude', 'real', 'imag', 'realimag'] as const) {
    assert.equal(measurementFormatInfo(format, trace).unit, '1')
  }
  assert.equal(measurementFormatInfo('db', trace).unit, 'dB')
  assert.equal(measurementFormatInfo('phase', trace).unit, 'deg')
  assert.equal(scaleValue(0.1, 'real', 5, trace), '0.1 1')
  const presentation = { ...defaultTracePresentation(), format: 'realimag' as const,
    marker: { enabled: true, frequencyHz: 2e6 } }
  assert.equal(getMarkerReadout(acquired, trace, presentation)?.valueText, '0 1 / 0.25 1')
  const csv = exportVisibleFrameCsv(acquired, { [trace.name]: presentation })
  assert.match(csv, /CSV schema 2/)
  assert.match(csv, /kind,parameter,raw_unit,calibration/)
  assert.match(csv, /true,realimag,"1",0,0.25,sParameter,"S21","1",uncalibrated/)
  assert.match(csv, /false,realimag,"1",,,sParameter,"S21","1",uncalibrated/)
})

test('measurement POST sends the selected kind and revision without client-generated result metadata', async () => {
  let sent: Record<string, unknown> = {}
  const fetcher: typeof fetch = async (_input, init) => {
    sent = JSON.parse(String(init?.body))
    return new Response(JSON.stringify({ operationId: 'op-s21', state: 'pending' }), { status: 202 })
  }
  const api = new InstrumentApi({ fetch: fetcher, storage: { getItem: () => 'token', setItem() {}, removeItem() {} } })
  await api.addMeasurement(measurementCandidate('S21', {
    name: 'Transmission', kind: 'receiver', receiverKind: 'a', receiverPort: 4, sourcePort: 3,
  }), '18446744073709551615')
  assert.equal(sent.kind, 'sParameter')
  assert.equal(sent.receiverKind, 'b')
  assert.equal(sent.receiverPort, 2)
  assert.equal(sent.sourcePort, 1)
  assert.equal(sent.expectedRevision, '18446744073709551615')
  assert.equal(Object.hasOwn(sent, 'unit'), false)
  assert.equal(Object.hasOwn(sent, 'calibration'), false)
})

test('zero complex amplitude remains valid while its phase display and marker are undefined', () => {
  const acquired = frame()
  const trace = acquired.traces[0]!
  trace.real[0] = 0
  trace.imag[0] = -0
  assert.equal(displayValue(0, -0, 'magnitude'), 0)
  assert.equal(displayValue(0, 0, 'db'), -Infinity)
  assert.ok(Number.isNaN(displaySeries(trace, 'phase')[0]!.values[0]))
  const presentation = { ...defaultTracePresentation(), format: 'phase' as const,
    marker: { enabled: true, frequencyHz: 1e6 } }
  const marker = getMarkerReadout(acquired, trace, presentation)
  assert.equal(marker?.valid, true)
  assert.equal(marker?.valueText, '— °')
  const csv = exportVisibleFrameCsv(acquired, { [trace.name]: presentation })
  assert.match(csv, /1000000,0,0,true,phase,"deg",,,sParameter/)
})

test('logarithmic magnitude stays finite when finite complex components exceed the linear magnitude range', () => {
  const maximum = Number.MAX_VALUE
  const expected = 20 * Math.log10(maximum) + 10 * Math.log10(2)
  const db = displayValue(maximum, maximum, 'db')
  assert.ok(Number.isFinite(db))
  assert.ok(Math.abs(db - expected) < 1e-10)
  assert.equal(displayValue(maximum, 0, 'db'), 20 * Math.log10(maximum))
  assert.equal(displayValue(Number.MIN_VALUE, 0, 'db'), 20 * Math.log10(Number.MIN_VALUE))
})

test('CW status and repeated acquired frequencies decode without allowing a descending axis', () => {
  const status = snapshot(sParameter())
  status.config.startHz = 1e9
  status.config.stopHz = 1e9
  assert.deepEqual(decodeStatus(status).config, status.config)
  assert.throws(() => decodeStatus({ ...status, config: { ...status.config, startHz: 1e9, stopHz: 1e9 - 1 } }))
  const acquired = frame()
  acquired.frequencyHz = [1e9, 1e9, 1e9]
  assert.deepEqual(decodeFrame(acquired), acquired)
  acquired.frequencyHz = [1e6, 1e6, 2e6]
  assert.deepEqual(decodeFrame(acquired), acquired)
  acquired.frequencyHz = [1e6, 1e6 - 1, 2e6]
  assert.throws(() => decodeFrame(acquired))
})
