import assert from 'node:assert/strict'
import test from 'node:test'
import {
  hitTestTrace,
  type SelectableCurve,
  type TraceHitTestInput,
} from '../src/plot/traceHitTest'

function cartesian(
  name = 'Trc1',
  values: number[] = [50, 50],
  active = false,
): Extract<SelectableCurve, { kind: 'cartesian' }> {
  return {
    name,
    kind: 'cartesian',
    format: 'db',
    color: '#60e600',
    active,
    dashed: false,
    frequencyHz: [0, 100],
    values,
    scale: { min: 0, max: 100 },
  }
}

function circular(
  format: 'smith' | 'polar' | 'invSmith' = 'smith',
): Extract<SelectableCurve, { kind: 'circular' }> {
  return {
    name: 'Circular',
    kind: 'circular',
    format,
    radius: 1,
    color: '#ffff26',
    active: false,
    points: [
      { real: -0.5, imaginary: 0 },
      { real: 0.5, imaginary: 0 },
    ],
  }
}

function diagram(curves: SelectableCurve[]): TraceHitTestInput {
  return {
    width: 100,
    height: 100,
    rect: { left: 10, top: 10, width: 80, height: 80 },
    xMin: 0,
    xMax: 100,
    bounds: { width: 100, height: 100, left: 0, top: 0 },
    curves,
  }
}

test('trace selection hits a segment midpoint rather than requiring a nearby sample vertex', () => {
  const input = diagram([cartesian()])
  assert.equal(hitTestTrace(input, { x: 50, y: 50 }), 'Trc1')
  assert.equal(hitTestTrace(input, { x: 50, y: 56 }), 'Trc1')
  assert.equal(hitTestTrace(input, { x: 50, y: 56.01 }), null)
  assert.equal(hitTestTrace(input, { x: 50, y: 45 }, 4), null)
  assert.equal(hitTestTrace(input, { x: 50, y: 45 }, 5), 'Trc1')
})

test('hit tolerance remains in client CSS pixels after unequal layout scaling and offsets', () => {
  const input = diagram([cartesian('Scaled', [80, 20])])
  input.bounds = { left: 300, top: 100, width: 200, height: 50 }
  assert.equal(hitTestTrace(input, { x: 400, y: 125 }, 0), 'Scaled')
  assert.equal(hitTestTrace(input, { x: 400, y: 129 }, 4), 'Scaled')
  assert.equal(hitTestTrace(input, { x: 400, y: 132 }, 4), null)
  assert.equal(hitTestTrace(input, { x: 50, y: 50 }), null)
})

test('invalid samples break Cartesian paths and isolated samples do not invent visible segments', () => {
  for (const invalid of [NaN, Infinity, -Infinity]) {
    const curve = cartesian('Broken', [50, invalid, 50])
    curve.frequencyHz = [0, 50, 100]
    assert.equal(hitTestTrace(diagram([curve]), { x: 50, y: 50 }), null)
  }
  const curve = cartesian('Broken frequency', [50, 50, 50])
  curve.frequencyHz = [0, NaN, 100]
  assert.equal(hitTestTrace(diagram([curve]), { x: 50, y: 50 }), null)
  const short = cartesian('Missing value', [50])
  assert.equal(hitTestTrace(diagram([short]), { x: 10, y: 50 }), null)
})

test('an invalid gap retains valid segments on either side without bridging the gap', () => {
  const curve = cartesian('Segments', [50, 50, NaN, 50, 50])
  curve.frequencyHz = [0, 25, 50, 75, 100]
  const input = diagram([curve])
  assert.equal(hitTestTrace(input, { x: 20, y: 50 }), 'Segments')
  assert.equal(hitTestTrace(input, { x: 50, y: 50 }), null)
  assert.equal(hitTestTrace(input, { x: 80, y: 50 }), 'Segments')
})

test('Cartesian hit testing rejects blank and clipped-out areas rather than selecting extended curves', () => {
  const curve = cartesian('Clipped', [50, 50])
  curve.frequencyHz = [-100, 200]
  const input = diagram([curve])
  assert.equal(hitTestTrace(input, { x: 50, y: 15 }), null)
  assert.equal(hitTestTrace(input, { x: 9, y: 50 }), null)
  assert.equal(hitTestTrace(input, { x: 91, y: 50 }), null)
  assert.equal(hitTestTrace(input, { x: 10, y: 50 }), 'Clipped')
  assert.equal(hitTestTrace(input, { x: 90, y: 50 }), 'Clipped')
  curve.values = [105, 105]
  assert.equal(hitTestTrace(input, { x: 50, y: 10 }), null)
})

test('clipped diagonal segments can cross the viewport even when both endpoints are outside', () => {
  const curve = cartesian('Crossing', [-100, 200])
  curve.frequencyHz = [-100, 200]
  const input = diagram([curve])
  assert.equal(hitTestTrace(input, { x: 50, y: 50 }), 'Crossing')
  assert.equal(hitTestTrace(input, { x: 10, y: 90 }), 'Crossing')
  assert.equal(hitTestTrace(input, { x: 50, y: 20 }), null)
})

test('the nearest curve wins even when a more distant active curve is drawn above it', () => {
  const input = diagram([
    cartesian('Near', [50, 50]),
    cartesian('Active', [55, 55], true),
  ])
  assert.equal(hitTestTrace(input, { x: 50, y: 50 }), 'Near')
  assert.equal(hitTestTrace(input, { x: 50, y: 46 }), 'Active')
})

test('equal-distance hits follow active-last and stable later-paint order without mutating input', () => {
  const active = cartesian('Active', [50, 50], true)
  const first = cartesian('First')
  const last = cartesian('Last')
  const input = diagram([active, first, last])
  const original = input.curves.slice()
  assert.equal(hitTestTrace(input, { x: 50, y: 50 }), 'Active')
  assert.deepEqual(input.curves, original)
  active.active = false
  assert.equal(hitTestTrace(input, { x: 50, y: 50 }), 'Last')
  first.active = true
  last.active = true
  assert.equal(hitTestTrace(input, { x: 50, y: 50 }), 'Last')
})

test('multi-series Real/Imag paths return one owner name and remain independent', () => {
  const real = cartesian('Complex', [75, 75])
  const imaginary = cartesian('Complex', [25, 25])
  real.format = imaginary.format = 'realimag'
  const input = diagram([real, imaginary])
  assert.equal(hitTestTrace(input, { x: 50, y: 30 }), 'Complex')
  assert.equal(hitTestTrace(input, { x: 50, y: 70 }), 'Complex')
  assert.equal(hitTestTrace(input, { x: 50, y: 50 }), null)
})

test('Log Mag applies the renderer logarithmic axis and breaks at nonpositive projected values', () => {
  const curve = cartesian('Log', [0.001, 10])
  curve.format = 'logMagnitude'
  curve.scale = { min: 0.001, max: 10 }
  const input = diagram([curve])
  assert.equal(hitTestTrace(input, { x: 50, y: 50 }), 'Log')
  curve.values = [0.001, 0, 10]
  curve.frequencyHz = [0, 50, 100]
  assert.equal(hitTestTrace(input, { x: 50, y: 50 }), null)
  curve.values = [0.001, -1, 10]
  assert.equal(hitTestTrace(input, { x: 50, y: 50 }), null)
})

for (const format of ['smith', 'polar', 'invSmith'] as const) {
  test(`${format} selection follows the displayed complex segment without negating its projection`, () => {
    const curve = circular(format)
    curve.points = [
      { real: 0.25, imaginary: 0.25 },
      { real: 0.75, imaginary: 0.25 },
    ]
    const input = diagram([curve])
    assert.equal(hitTestTrace(input, { x: 66, y: 42 }, 0), 'Circular')
    assert.equal(hitTestTrace(input, { x: 34, y: 58 }, 0), null)
  })
}

test('circular mapping uses physical CSS size to keep its viewport circular under unequal scales', () => {
  const input = diagram([circular()])
  input.width = 200
  input.height = 150
  input.bounds = { width: 400, height: 75, left: 120, top: 40 }
  assert.equal(hitTestTrace(input, { x: 320, y: 77.5 }, 0), 'Circular')
  assert.equal(hitTestTrace(input, { x: 329.75, y: 77.5 }, 0), 'Circular')
  assert.equal(hitTestTrace(input, { x: 340, y: 77.5 }, 6), null)
})

test('circular curves outside the unit circle remain selectable inside the renderer rectangular clip', () => {
  const curve = circular()
  curve.points = [
    { real: 1.2, imaginary: 0 },
    { real: 2, imaginary: 0 },
  ]
  const input = diagram([curve])
  assert.equal(hitTestTrace(input, { x: 95, y: 50 }), 'Circular')
  assert.equal(hitTestTrace(input, { x: 99, y: 50 }), 'Circular')
  assert.equal(hitTestTrace(input, { x: 99.01, y: 50 }), null)
  assert.equal(hitTestTrace(input, { x: 104, y: 50 }), null)
})

test('circular invalid gaps retain isolated visible dots and do not connect across missing samples', () => {
  const curve = circular()
  curve.points = [
    { real: -0.5, imaginary: 0 },
    null,
    { real: 0.5, imaginary: 0 },
  ]
  const input = diagram([curve])
  assert.equal(hitTestTrace(input, { x: 34, y: 50 }), 'Circular')
  assert.equal(hitTestTrace(input, { x: 66, y: 50 }), 'Circular')
  assert.equal(hitTestTrace(input, { x: 50, y: 50 }), null)
  curve.points = curve.points.map((point, index) =>
    index === 1 ? { real: NaN, imaginary: 0 } : point,
  )
  assert.equal(hitTestTrace(input, { x: 50, y: 50 }), null)
})

test('mixed-format overlaps respect the same active priority as their paint passes', () => {
  const rectangular = cartesian('Cartesian', [50, 50], true)
  const circle = circular()
  const input = diagram([rectangular, circle])
  assert.equal(hitTestTrace(input, { x: 50, y: 50 }), 'Cartesian')
  rectangular.active = false
  assert.equal(hitTestTrace(input, { x: 50, y: 50 }), 'Circular')
  circle.radius = 2
  assert.equal(hitTestTrace(input, { x: 65, y: 50 }, 0), 'Cartesian')
})

test('invalid input coordinates, geometry, tolerance and circular radii produce no selection', () => {
  const input = diagram([cartesian(), circular()])
  for (const point of [
    { x: NaN, y: 50 },
    { x: 50, y: Infinity },
  ])
    assert.equal(hitTestTrace(input, point), null)
  for (const tolerance of [-1, NaN, Infinity])
    assert.equal(hitTestTrace(input, { x: 50, y: 50 }, tolerance), null)
  for (const bounds of [
    { ...input.bounds, width: 0 },
    { ...input.bounds, height: NaN },
    { ...input.bounds, left: Infinity },
  ])
    assert.equal(hitTestTrace({ ...input, bounds }, { x: 50, y: 50 }), null)
  assert.equal(hitTestTrace({ ...input, width: 0 }, { x: 50, y: 50 }), null)
  assert.equal(hitTestTrace(diagram([]), { x: 50, y: 50 }), null)
  const curve = circular()
  for (const radius of [0, NaN, Infinity, 0.005]) {
    curve.radius = radius
    assert.equal(hitTestTrace(diagram([curve]), { x: 50, y: 50 }), null)
  }
})
