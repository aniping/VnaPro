import assert from 'node:assert/strict'
import test from 'node:test'
import {
  createStimulusZoomGeometry,
  createTracePanGeometry,
  type StimulusZoomGeometryInput,
  type TracePanGeometryInput,
} from '../src/plot/plotGestures'
import { normalizedY, referenceLevel } from '../src/plot/traceMath'

function panInput(overrides: Partial<TracePanGeometryInput> = {}): TracePanGeometryInput {
  return {
    name: 'Trc2', format: 'db', scale: { min: -90, max: 10, referencePosition: 9 },
    displayRevision: '18446744073709551615', startClientY: 200, plotHeightCSS: 400,
    ...overrides,
  }
}

function zoomInput(overrides: Partial<StimulusZoomGeometryInput> = {}): StimulusZoomGeometryInput {
  return {
    traceName: 'Trc2', format: 'db', scale: { min: -90, max: 10, referencePosition: 9 },
    revision: '18446744073709551614', displayRevision: '18446744073709551615',
    range: { min: 100e6, max: 900e6 }, rect: { left: 40, top: 20, width: 400, height: 240 },
    bounds: { left: 100, top: 50, width: 1000, height: 900 }, size: { width: 500, height: 300 },
    ...overrides,
  }
}

function near(actual: number, expected: number) {
  assert.ok(Math.abs(actual - expected) <= 1e-12 * Math.max(Math.abs(expected), Number.MIN_VALUE),
    `expected ${expected}, got ${actual}`)
}

test('trace pan changes Ref Value while keeping per-division span and the reference triangle fixed', () => {
  const input = panInput()
  const geometry = createTracePanGeometry(input)!
  const moved = geometry.scaleAt(240)!
  assert.deepEqual(moved, { min: -80, max: 20, referencePosition: 9 })
  assert.equal(moved.max - moved.min, input.scale.max - input.scale.min)
  assert.equal(moved.referencePosition, input.scale.referencePosition)
  near(normalizedY(0, moved) - normalizedY(0, input.scale), 0.1)
  assert.deepEqual(geometry.commandAt(240), {
    name: 'Trc2', reference: 10, displayRevision: '18446744073709551615',
  })
  assert.deepEqual(input.scale, { min: -90, max: 10, referencePosition: 9 })
})

test('zero pan preserves the original scale and actual movement snaps absolute Ref Value without decimal tails', () => {
  const input = panInput({ scale: { min: -31.4965358, max: -26.4965358, referencePosition: 9 } })
  const geometry = createTracePanGeometry(input)!
  assert.deepEqual(geometry.scaleAt(200), input.scale)
  assert.equal(geometry.commandAt(200)!.reference, referenceLevel(input.scale))
  assert.equal(geometry.commandAt(203)!.reference, -26.96)
  const scale = geometry.scaleAt(203)!
  near(scale.max - scale.min, 5)
  near(referenceLevel(scale), -26.96)
  assert.equal(scale.referencePosition, 9)
})

test('small delay units retain their magnitude and stable significant digits during pan', () => {
  for (const [format, span, expected] of [
    ['delay', 1e-8, 6e-9], ['delayDerivation', 1e-18, 6e-19],
  ] as const) {
    const geometry = createTracePanGeometry(panInput({ format, scale: { min: 0, max: span, referencePosition: 5 } }))!
    assert.equal(geometry.commandAt(240)!.reference, expected)
    const scale = geometry.scaleAt(240)!
    near(scale.max - scale.min, span)
    near(referenceLevel(scale), expected)
  }
})

test('pan freezes the original format, revision, CSS height and scale against polling updates', () => {
  const input = panInput()
  const geometry = createTracePanGeometry(input)!
  input.name = 'Reused'
  input.displayRevision = '99'
  input.startClientY = -100
  input.plotHeightCSS = 1
  input.scale.min = -1000
  input.scale.referencePosition = 0
  input.format = 'logMagnitude'
  assert.deepEqual(geometry.commandAt(240), {
    name: 'Trc2', reference: 10, displayRevision: '18446744073709551615',
  })
  assert.deepEqual(geometry.scaleAt(240), { min: -80, max: 20, referencePosition: 9 })
})

test('Log Mag pan works in decades and submits a positive reference amplitude', () => {
  const input = panInput({ format: 'logMagnitude', scale: { min: 0.01, max: 100, referencePosition: 5 } })
  const geometry = createTracePanGeometry(input)!
  const scale = geometry.scaleAt(300)!
  near(scale.min, 0.1)
  near(scale.max, 1000)
  near(Math.log10(scale.max / scale.min), 4)
  assert.equal(geometry.commandAt(300)!.reference, 10)
  assert.deepEqual(geometry.scaleAt(200), input.scale)
  assert.equal(geometry.commandAt(200)!.reference, 1)
})

test('pan rejects circular formats, invalid geometry and non-finite or overflowing pointer results', () => {
  for (const invalid of [
    { name: '' }, { displayRevision: '1.5' }, { displayRevision: '' },
    { startClientY: NaN }, { plotHeightCSS: 0 }, { plotHeightCSS: Infinity },
    { scale: { min: 1, max: 1 } }, { scale: { min: -Infinity, max: 1 } },
    { scale: { min: -1, max: 1, referencePosition: 11 } },
    { format: 'logMagnitude', scale: { min: 0, max: 1 } },
    ...(['smith', 'polar', 'invSmith'] as const).map((format) => ({ format })),
  ] as Partial<TracePanGeometryInput>[])
    assert.equal(createTracePanGeometry(panInput(invalid)), null)
  const geometry = createTracePanGeometry(panInput())!
  assert.equal(geometry.commandAt(Infinity), null)
  assert.equal(geometry.scaleAt(NaN), null)
  assert.equal(geometry.scaleAt(Number.MAX_VALUE), null)
  const logarithmic = createTracePanGeometry(panInput({ format: 'logMagnitude', scale: { min: 1e300, max: 1e304 } }))!
  assert.equal(logarithmic.commandAt(100000), null)
})

test('stimulus zoom maps nonuniform CSS scaling to real frequency and Y ranges with reverse selection support', () => {
  const geometry = createStimulusZoomGeometry(zoomInput())!
  const start = { x: 380, y: 290 }
  const end = { x: 780, y: 650 }
  assert.deepEqual(geometry.pointAt(start), { x: 140, y: 80 })
  assert.deepEqual(geometry.pointAt(end), { x: 340, y: 200 })
  const selection = geometry.selectionAt(start, end)
  assert.deepEqual(selection, {
    traceName: 'Trc2', startHz: 300e6, stopHz: 700e6, min: -65, max: -15,
    revision: '18446744073709551614', displayRevision: '18446744073709551615',
  })
  assert.deepEqual(geometry.selectionAt(end, start), selection)
})

test('zoom clamps selection to plot edges and enforces six CSS pixels independently on both axes', () => {
  const geometry = createStimulusZoomGeometry(zoomInput())!
  assert.deepEqual(geometry.pointAt({ x: -1000, y: -1000 }), { x: 40, y: 20 })
  assert.deepEqual(geometry.pointAt({ x: 10000, y: 10000 }), { x: 440, y: 260 })
  const start = { x: 180, y: 110 }
  assert.equal(geometry.selectionAt(start, { x: 185.999, y: 300 }), null)
  assert.equal(geometry.selectionAt(start, { x: 400, y: 115.999 }), null)
  assert.ok(geometry.selectionAt(start, { x: 186, y: 116 }))
  assert.equal(geometry.pointAt({ x: NaN, y: 0 }), null)
  assert.equal(geometry.selectionAt(start, { x: Infinity, y: 300 }), null)
})

test('zoom freezes metadata and every nested geometry value before later snapshots or layout changes', () => {
  const input = zoomInput()
  const geometry = createStimulusZoomGeometry(input)!
  const start = { x: 380, y: 290 }
  const end = { x: 780, y: 650 }
  const expected = geometry.selectionAt(start, end)
  input.traceName = 'Reused'
  input.revision = '99'
  input.displayRevision = '88'
  input.range.min = 0
  input.range.max = 1
  input.scale.min = -1000
  input.rect.width = 1
  input.bounds.height = 1
  input.size.width = 1
  assert.deepEqual(geometry.selectionAt(start, end), expected)
})

test('logarithmic zoom returns positive physical amplitudes rather than decade coordinates', () => {
  const geometry = createStimulusZoomGeometry(zoomInput({ format: 'logMagnitude', scale: { min: 0.01, max: 100 } }))!
  const selected = geometry.selectionAt({ x: 380, y: 290 }, { x: 780, y: 650 })!
  near(selected.min, 0.1)
  near(selected.max, 10)
  assert.equal(selected.startHz, 300e6)
  assert.equal(selected.stopHz, 700e6)
})

test('zoom rejects CW, circular or invalid ranges and zero or out-of-canvas plot geometry', () => {
  for (const invalid of [
    { traceName: '' }, { revision: '' }, { displayRevision: '1.1' },
    { range: { min: 1, max: 1 } }, { range: { min: 1, max: Infinity } },
    { size: { width: 0, height: 300 } },
    { rect: { left: -1, top: 20, width: 400, height: 240 } },
    { rect: { left: 40, top: 20, width: 500, height: 240 } },
    { bounds: { left: 0, top: 0, width: 1000, height: 0 } },
    { scale: { min: 10, max: -90 } },
    { format: 'logMagnitude', scale: { min: -1, max: 10 } },
    ...(['smith', 'polar', 'invSmith'] as const).map((format) => ({ format })),
  ] as Partial<StimulusZoomGeometryInput>[])
    assert.equal(createStimulusZoomGeometry(zoomInput(invalid)), null)
})
