import assert from 'node:assert/strict'
import test from 'node:test'
import type { AcquiredFrame, ReceiverTrace } from '../src/api/types'
import { defaultTracePresentation } from '../src/domain/presentation'
import { exportVisibleFrameCsv } from '../src/plot/exportCsv'
import { markerPlacement, plotRect } from '../src/plot/canvasRenderer'
import { displaySeries, displayValue, markerResult, nearestIndex, normalizedY, referenceLevel, traceScale } from '../src/plot/traceMath'

function receiver(real: (number | null)[], imaginary = real.map(() => 0)): ReceiverTrace {
  return { name: 'Receiver A', kind: 'receiver', receiverKind: 'a', receiverPort: 1, sourcePort: 1,
    parameter: 'a1,1', unit: 'a.u.', calibration: 'raw',
    available: true, real, imag: imaginary, valid: real.map(() => true) }
}

function frame(trace = receiver([1, 1.001, 1.002])): AcquiredFrame {
  return { available: true, current: true, channelId: 1,
    runId: '18446744073709551613', revision: '18446744073709551614', frameSeq: '18446744073709551615',
    originalPointCount: 10001, displayPointCount: 3, decimated: true,
    frequencyHz: [1e6, 2e6, 9e6], traces: [trace] }
}

test('nearest marker uses actual nonuniform display samples and survives shorter frames', () => {
  assert.equal(nearestIndex([1e6, 2e6, 9e6], 2.1e6), 1)
  assert.equal(nearestIndex([1e6, 2e6, 9e6], -5), 0)
  assert.equal(nearestIndex([1e6, 2e6, 9e6], 100e6), 2)
  assert.equal(nearestIndex([], 2e6), -1)
  const shorter = frame(receiver([1, 1]))
  shorter.frequencyHz = [1e6, 2e6]
  shorter.displayPointCount = 2
  const marker = markerResult(shorter, shorter.traces[0], 9e6, 'db')
  assert.equal(marker?.index, 1)
  assert.equal(marker?.frequencyHz, 2e6)
})

test('invalid raw points remain gaps in both real and imaginary display series', () => {
  const trace = receiver([1, null, 2, 3], [0, 1, 2, Infinity])
  trace.valid[2] = false
  const series = displaySeries(trace, 'realimag')
  assert.equal(series.length, 2)
  assert.equal(series[0]!.values[0], 1)
  for (const component of series) {
    assert.ok(Number.isNaN(component.values[1]))
    assert.ok(Number.isNaN(component.values[2]))
    assert.ok(Number.isNaN(component.values[3]))
  }
  assert.equal(markerResult(frame(trace), trace, 2e6, 'db')?.valid, false)
})

test('raw display format values preserve their units and zero magnitude is negative infinity dB', () => {
  assert.equal(displayValue(3, 4, 'magnitude'), 5)
  assert.equal(displayValue(3, 4, 'real'), 3)
  assert.equal(displayValue(3, 4, 'imag'), 4)
  assert.equal(displayValue(0, 1, 'phase'), 90)
  assert.equal(displayValue(0, 0, 'db'), -Infinity)
  assert.ok(Number.isNaN(displayValue(NaN, 0, 'phase')))
})

test('normal startup scale stays wide, explicit Autoscale fits the real small variation', () => {
  const trace = receiver([1, 1.000001, 1.000002])
  const defaultPresentation = defaultTracePresentation()
  const initialScale = traceScale(trace, 'db', defaultPresentation.scale)
  assert.deepEqual(initialScale, { min: -90, max: 10, referencePosition: 9 })
  assert.equal(referenceLevel(initialScale), 0)
  assert.equal(normalizedY(0, initialScale), 0.1)
  const fitted = traceScale(trace, 'db', null)
  assert.ok(fitted.min <= 0)
  assert.ok(fitted.max >= displayValue(1.000002, 0, 'db'))
  assert.ok(fitted.max - fitted.min < 0.001)
  const constant = traceScale(receiver([1, 1, 1]), 'db', null)
  assert.ok(constant.max > constant.min)
  const zero = traceScale(receiver([0, 0, 0]), 'db', null)
  assert.deepEqual(zero, { min: -90, max: 10, referencePosition: 9 })
})

test('reference position uses divisions from the bottom, including non-default triangle position', () => {
  assert.equal(referenceLevel({ min: -80, max: 20 }), 0)
  const scale = traceScale(undefined, 'db', { min: -80, max: 20, referencePosition: 9 })
  assert.equal(referenceLevel(scale), 10)
  assert.equal(normalizedY(referenceLevel(scale), scale), 0.1)
  assert.equal(referenceLevel({ min: -80, max: 20, referencePosition: 0 }), -80)
  assert.equal(referenceLevel({ min: -80, max: 20, referencePosition: 10 }), 20)
})

test('instrument grid occupies the plot viewport with only its one-pixel border inset', () => {
  for (const [width, height] of [[846, 658], [421, 305]]) {
    const rect = plotRect(width!, height!)
    assert.equal(rect.left, 1)
    assert.equal(rect.top, 1)
    assert.equal(rect.left + rect.width, width! - 1)
    assert.equal(rect.top + rect.height, height! - 1)
  }
})

test('M1 keeps a full hit target at both frequency endpoints and scale edges without moving its sample tip', () => {
  const rect = plotRect(421, 305)
  const points = [
    { x: rect.left, y: rect.top },
    { x: rect.left + rect.width, y: rect.top },
    { x: rect.left, y: rect.top + rect.height },
    { x: rect.left + rect.width, y: rect.top + rect.height },
  ]
  for (const point of points) {
    const marker = markerPlacement(point, rect)
    assert.equal(marker.width, 32)
    assert.equal(marker.height, 32)
    assert.ok(marker.left >= rect.left && marker.top >= rect.top)
    assert.ok(marker.left + marker.width <= rect.left + rect.width)
    assert.ok(marker.top + marker.height <= rect.top + rect.height)
    assert.equal(marker.left + marker.tipX, point.x)
    assert.equal(marker.top + marker.tipY, point.y)
    const glyphTip = /L([\d.e+-]+) ([\d.e+-]+)Z/.exec(marker.glyphPath)
    assert.ok(glyphTip)
    assert.equal(marker.left + marker.glyphLeft + Number(glyphTip[1]), point.x)
    const glyphTipY = marker.orientation === 'up' ? 0 : 9
    assert.equal(marker.top + marker.glyphTop + glyphTipY, point.y)
    assert.ok(marker.labelTop >= 0 && marker.labelTop + 16 <= marker.height)
    assert.equal(marker.orientation, point.y === rect.top ? 'up' : 'down')
  }
})

test('CSV preserves uint64 identities, sample provenance, gaps and each visible trace format', () => {
  const trace = receiver([0, null, 2])
  const snapshot = frame(trace)
  const hidden = { ...receiver([10, 10, 10]), name: 'Hidden' }
  snapshot.traces.push(hidden)
  const csv = exportVisibleFrameCsv(snapshot, {
    [trace.name]: defaultTracePresentation(), Hidden: { ...defaultTracePresentation(), visible: false },
  })
  assert.match(csv, /frameSeq=18446744073709551615,runId=18446744073709551613,revision=18446744073709551614/)
  assert.match(csv, /originalPointCount=10001,displayPointCount=3,decimated=true/)
  assert.match(csv, /Source sample indices are not supplied/)
  assert.match(csv, /"Receiver A",a,1,1,0,1000000,0,0,true,db,"dB re 1 a.u.",-Infinity,/)
  assert.match(csv, /"Receiver A",a,1,1,1,2000000,,,false,db/)
  assert.doesNotMatch(csv, /Hidden/)
  assert.equal(csv.split('\r\n').filter(line => line.startsWith('"Receiver A"')).length, 3)
})
