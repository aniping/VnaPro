import assert from 'node:assert/strict'
import test from 'node:test'
import type { DisplayFormat, DisplayScale } from '../src/domain/presentation'
import {
  composeGraphicalZoom,
  FULL_GRAPHICAL_ZOOM,
  graphicalFrequencyRange,
  graphicalTraceScale,
  isGraphicalZoomed,
  panGraphicalZoom,
  type GraphicalZoomViewport,
} from '../src/plot/graphicalZoom'

function close(actual: number, expected: number): void {
  const tolerance = Math.max(Math.abs(expected), 1e-15) * 1e-12
  assert.ok(Math.abs(actual - expected) <= tolerance, `Expected ${actual} to be close to ${expected}`)
}

test('nested graphical selections retain the corresponding original frequency and response interval', () => {
  assert.equal(isGraphicalZoomed(null), false)
  assert.equal(isGraphicalZoomed(FULL_GRAPHICAL_ZOOM), false)
  assert.equal(Object.isFrozen(FULL_GRAPHICAL_ZOOM), true)
  const first = composeGraphicalZoom(FULL_GRAPHICAL_ZOOM, {
    left: 0.25, top: 0.125, width: 0.5, height: 0.5,
  })
  assert.ok(first)
  const nested = composeGraphicalZoom(first, {
    left: 0.5, top: 0.25, width: 0.5, height: 0.5,
  })
  assert.ok(nested)
  assert.deepEqual(nested, { left: 0.5, top: 0.25, width: 0.25, height: 0.25 })
  assert.equal(isGraphicalZoomed(nested), true)
  assert.deepEqual(graphicalFrequencyRange({ min: 1e9, max: 9e9 }, nested), { min: 5e9, max: 7e9 })
  assert.deepEqual(graphicalTraceScale({ min: -90, max: 10 }, 'db', nested), { min: -40, max: -15 })
  const direct = composeGraphicalZoom(FULL_GRAPHICAL_ZOOM, {
    left: 0.5, top: 0.25, width: 0.25, height: 0.25,
  })
  assert.deepEqual(nested, direct, 'Repeated selection must not reinterpret the second box against the full plot')
  assert.deepEqual(first, { left: 0.25, top: 0.125, width: 0.5, height: 0.5 })
})

test('one rectangle scales overlaid traces using each trace own response units and logarithmic decades', () => {
  const viewport = { left: 0.25, top: 0.25, width: 0.5, height: 0.5 }
  const cases: { scale: DisplayScale; format: DisplayFormat; expected: DisplayScale }[] = [
    { scale: { min: -90, max: 10 }, format: 'db', expected: { min: -65, max: -15 } },
    { scale: { min: -180, max: 180 }, format: 'phase', expected: { min: -90, max: 90 } },
    { scale: { min: -1, max: 1 }, format: 'realimag', expected: { min: -0.5, max: 0.5 } },
    { scale: { min: 0, max: 1e-6 }, format: 'delay', expected: { min: 0.25e-6, max: 0.75e-6 } },
  ]
  for (const { scale, format, expected } of cases) {
    const result = graphicalTraceScale(Object.freeze(scale), format, viewport)
    assert.ok(result)
    close(result.min, expected.min)
    close(result.max, expected.max)
  }
  const logViewport = { left: 0, top: 1 / 6, width: 1, height: 1 / 3 }
  const small = graphicalTraceScale({ min: 0.001, max: 1000 }, 'logMagnitude', logViewport)
  const large = graphicalTraceScale({ min: 1, max: 1e6 }, 'logMagnitude', logViewport)
  assert.ok(small && large)
  close(small.min, 1)
  close(small.max, 100)
  close(large.min, 1000)
  close(large.max, 100000)
  close(small.max / small.min, large.max / large.min)
})

test('vertical pan uses the current visible height and can expose responses outside the original Y range', () => {
  const original = Object.freeze({ left: 0.25, top: 0.25, width: 0.5, height: 0.5 })
  const moved = panGraphicalZoom(original, 2)
  assert.ok(moved)
  assert.deepEqual(moved, { left: 0.25, top: 1.25, width: 0.5, height: 0.5 })
  assert.deepEqual(graphicalTraceScale({ min: -90, max: 10 }, 'db', moved), { min: -165, max: -115 })
  const upward = panGraphicalZoom(moved, -3)
  assert.ok(upward)
  assert.deepEqual(upward, { left: 0.25, top: -0.25, width: 0.5, height: 0.5 })
  assert.deepEqual(graphicalTraceScale({ min: -90, max: 10 }, 'db', upward), { min: -15, max: 35 })
  assert.deepEqual(graphicalFrequencyRange({ min: 1e9, max: 9e9 }, upward), { min: 3e9, max: 7e9 })
  const nested = composeGraphicalZoom(moved, { left: 0.25, top: 0.25, width: 0.5, height: 0.5 })
  assert.deepEqual(nested, { left: 0.375, top: 1.375, width: 0.25, height: 0.25 })
  assert.equal(isGraphicalZoomed(upward), true)
  assert.deepEqual(original, { left: 0.25, top: 0.25, width: 0.5, height: 0.5 })
})

test('zoom and pan derive frozen render values without changing the physical frequency range or saved trace scale', () => {
  const range = Object.freeze({ min: 1e7, max: 8e9 })
  const scale = Object.freeze({ min: -90, max: 10, referencePosition: 9 })
  const selection = Object.freeze({ left: 0.25, top: 0.25, width: 0.5, height: 0.5 })
  const selected = composeGraphicalZoom(FULL_GRAPHICAL_ZOOM, selection)
  assert.ok(selected)
  const moved = panGraphicalZoom(selected, 0.5)
  assert.ok(moved)
  const frequency = graphicalFrequencyRange(range, moved)
  const response = graphicalTraceScale(scale, 'db', moved)
  assert.ok(frequency && response)
  for (const output of [selected, moved, frequency, response]) assert.equal(Object.isFrozen(output), true)
  assert.notEqual(frequency, range)
  assert.notEqual(response, scale)
  assert.deepEqual(range, { min: 1e7, max: 8e9 })
  assert.deepEqual(scale, { min: -90, max: 10, referencePosition: 9 })
  assert.deepEqual(selection, { left: 0.25, top: 0.25, width: 0.5, height: 0.5 })
  assert.deepEqual(FULL_GRAPHICAL_ZOOM, { left: 0, top: 0, width: 1, height: 1 })
  assert.throws(() => { moved.top = 0 }, TypeError)
  assert.throws(() => { frequency.min = 0 }, TypeError)
  assert.throws(() => { response.min = 0 }, TypeError)
})

test('invalid or numerically unusable selections, axes and pans return null without repairing caller values', () => {
  const invalid: GraphicalZoomViewport[] = [
    { left: -0.1, top: 0, width: 0.5, height: 0.5 },
    { left: 0.75, top: 0, width: 0.5, height: 0.5 },
    { left: 0, top: NaN, width: 1, height: 1 },
    { left: 0, top: 0, width: 0, height: 1 },
    { left: 0, top: 0, width: 1, height: -1 },
    { left: 0, top: 0, width: 1, height: Infinity },
  ]
  for (const value of invalid) {
    const before = { ...value }
    Object.freeze(value)
    assert.equal(isGraphicalZoomed(value), false)
    assert.equal(composeGraphicalZoom(value, FULL_GRAPHICAL_ZOOM), null)
    assert.equal(composeGraphicalZoom(FULL_GRAPHICAL_ZOOM, value), null)
    assert.equal(graphicalFrequencyRange({ min: 1, max: 9 }, value), null)
    assert.equal(graphicalTraceScale({ min: -90, max: 10 }, 'db', value), null)
    assert.equal(panGraphicalZoom(value, 1), null)
    assert.deepEqual(value, before)
  }
  for (const selection of [{ left: 0, top: -0.25, width: 1, height: 0.5 },
    { left: 0, top: 0.75, width: 1, height: 0.5 }])
    assert.equal(composeGraphicalZoom(FULL_GRAPHICAL_ZOOM, selection), null)
  for (const range of [{ min: 3, max: 3 }, { min: 4, max: 3 }, { min: NaN, max: 9 }, { min: 1, max: Infinity }])
    assert.equal(graphicalFrequencyRange(range, FULL_GRAPHICAL_ZOOM), null)
  for (const scale of [{ min: 3, max: 3 }, { min: 4, max: 3 }, { min: -Infinity, max: 9 }])
    assert.equal(graphicalTraceScale(scale, 'db', FULL_GRAPHICAL_ZOOM), null)
  for (const format of ['smith', 'polar', 'invSmith'] as const)
    assert.equal(graphicalTraceScale({ min: -1, max: 1 }, format, FULL_GRAPHICAL_ZOOM), null)
  for (const min of [0, -1])
    assert.equal(graphicalTraceScale({ min, max: 10 }, 'logMagnitude', FULL_GRAPHICAL_ZOOM), null)
  assert.equal(graphicalTraceScale({ min: 1, max: 10 }, 'logMagnitude', { left: 0, top: -1000, width: 1, height: 1 }), null)
  assert.equal(graphicalTraceScale({ min: 1, max: 10 }, 'logMagnitude', { left: 0, top: 1000, width: 1, height: 1 }), null)
  assert.equal(composeGraphicalZoom({ left: 0, top: 0, width: Number.MIN_VALUE, height: 1 },
    { left: 0, top: 0, width: 0.5, height: 1 }), null)
  for (const delta of [NaN, Infinity, -Infinity]) assert.equal(panGraphicalZoom(FULL_GRAPHICAL_ZOOM, delta), null)
  assert.equal(panGraphicalZoom({ left: 0, top: 0, width: 1, height: 2 }, Number.MAX_VALUE), null)
})
