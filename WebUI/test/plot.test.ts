import assert from 'node:assert/strict'
import test from 'node:test'
import { exportVisibleFrameCsv } from '../src/plot/exportCsv'
import { markerPlacement, plotRect } from '../src/plot/canvasRenderer'
import {
  displaySeries,
  markerResult,
  normalizedY,
  referenceLevel,
} from '../src/plot/traceMath'
import {
  acquiredFrame,
  markerProjection,
  presentation,
  projection,
  receiverTrace,
} from './displayFixtures'

test('reference position uses divisions from the bottom, including non-default triangle position', () => {
  assert.equal(referenceLevel({ min: -80, max: 20 }), 0)
  const scale = { min: -80, max: 20, referencePosition: 9 }
  assert.equal(referenceLevel(scale), 10)
  assert.equal(normalizedY(referenceLevel(scale), scale), 0.1)
  assert.equal(referenceLevel({ min: -80, max: 20, referencePosition: 0 }), -80)
  assert.equal(referenceLevel({ min: -80, max: 20, referencePosition: 10 }), 20)
})

test('instrument grid occupies the plot viewport with only its one-pixel border inset', () => {
  for (const [width, height] of [
    [846, 658],
    [421, 305],
  ]) {
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

test('Cartesian rendering uses backend values and preserves the supplied invalid gaps', () => {
  const trace = receiverTrace()
  trace.real = [900, 900, 900]
  trace.imag = [900, 900, 900]
  trace.valid = [true, true, true]
  trace.projection = projection([12, null, -4], 'realimag', [-8, null, 5])
  const series = displaySeries(trace, 'realimag')
  assert.deepEqual(series[0]!.values, [12, NaN, -4])
  assert.deepEqual(series[1]!.values, [-8, NaN, 5])
  assert.deepEqual(displaySeries(trace, 'phase'), [])
})

test('marker readout consumes a full-frame backend sample outside the display array', () => {
  const frame = acquiredFrame()
  frame.originalPointCount = 10001
  frame.decimated = true
  frame.sampleIndexes = [0, 10000]
  const trace = frame.traces[0]!
  trace.projection.marker = markerProjection({
    available: true,
    valid: true,
    reason: '',
    sampleIndex: 4567,
    frequencyHz: 3e9,
    real: 3,
    imag: 4,
    value: 13.9794,
    valueState: 'finite',
  })
  const marker = markerResult(frame, trace, 3e9, 'db')
  assert.equal(marker!.index, 4567)
  assert.equal(marker!.frequencyHz, 3e9)
  assert.equal(marker!.value, 13.9794)
})

test('CSV schema 4 preserves backend projections, uint64 versions and original sample indexes', () => {
  const trace = receiverTrace()
  trace.projection = projection([null, 37], 'db')
  trace.projection.valueStates[0] = 'negativeInfinity'
  const frame = acquiredFrame(trace)
  frame.frameSeq = '18446744073709551615'
  frame.measurementRevision = '9007199254740993'
  frame.displayRevision = '9007199254740994'
  frame.originalPointCount = 10001
  frame.sampleIndexes = [0, 10000]
  frame.decimated = true
  const csv = exportVisibleFrameCsv(frame, { [trace.name]: presentation() })
  assert.match(csv, /CSV schema 4/)
  assert.match(
    csv,
    /measurementRevision=9007199254740993,displayRevision=9007199254740994/,
  )
  assert.match(csv, /source_index,display_state,secondary_state/)
  assert.match(csv, /-Infinity,.*0,negativeInfinity,undefined/)
  assert.match(csv, /37,.*10000,finite,undefined/)
  assert.doesNotMatch(csv, /normalized_r|normalized_x|impedance_state/)
})
