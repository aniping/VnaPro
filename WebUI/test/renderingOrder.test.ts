import assert from 'node:assert/strict'
import test, { type TestContext } from 'node:test'
import {
  paintDiagram,
  plotRect,
  type CanvasCurve,
} from '../src/plot/canvasRenderer'
import { paintSmithDiagram } from '../src/plot/smithRenderer'
import { paintMixedDiagram, type MixedCurve } from '../src/plot/mixedRenderer'
import { smithPoint, smithViewport } from '../src/plot/smithMath'
import { normalizedY, referenceLevel } from '../src/plot/traceMath'

function canvasProbe(test: TestContext) {
  const oldWindow = Object.getOwnPropertyDescriptor(globalThis, 'window')
  Object.defineProperty(globalThis, 'window', {
    configurable: true,
    value: { devicePixelRatio: 1 },
  })
  test.after(() => {
    if (oldWindow) Object.defineProperty(globalThis, 'window', oldWindow)
    else Reflect.deleteProperty(globalThis, 'window')
  })
  const strokes: string[] = []
  const moves: { color: string; x: number; y: number }[] = []
  let clears = 0
  const colors = ['#60e600', '#ffff26', '#36c5d8']
  const context = {
    strokeStyle: '',
    setTransform() {},
    clearRect() {
      clears++
    },
    setLineDash() {},
    fillRect() {},
    beginPath() {},
    lineTo() {},
    closePath() {},
    fill() {},
    save() {},
    rect() {},
    clip() {},
    restore() {},
    arc() {},
    fillText() {},
    moveTo(x: number, y: number) {
      if (colors.includes(this.strokeStyle))
        moves.push({ color: this.strokeStyle, x, y })
    },
    stroke() {
      if (colors.includes(this.strokeStyle)) strokes.push(this.strokeStyle)
    },
  }
  const canvas = {
    width: 0,
    height: 0,
    style: {},
    getContext: () => context,
  } as unknown as HTMLCanvasElement
  return { canvas, colors, strokes, moves, clears: () => clears }
}

test('Cartesian and Smith paint active traces last without changing their catalog/title order', (context) => {
  const probe = canvasProbe(context)
  const curves: CanvasCurve[] = probe.colors.map((color, index) => ({
    color,
    active: index === 0,
    frequencyHz: [10e6, 20e6],
    values: [-20, -20],
    scale: { min: -90, max: 10 },
    dashed: false,
  }))
  paintDiagram(
    probe.canvas,
    {
      width: 700,
      height: 500,
      rect: plotRect(700, 500),
      xMin: 10e6,
      xMax: 20e6,
      references: [],
      curves,
    },
    { left: 0, top: 0, width: 700, height: 500 },
  )
  assert.deepEqual(probe.strokes, [
    probe.colors[1],
    probe.colors[2],
    probe.colors[0],
  ])
  assert.deepEqual(
    curves.map((curve) => curve.color),
    probe.colors,
  )
  probe.strokes.length = 0
  const circular = curves.map((curve) => ({
    color: curve.color,
    active: curve.active,
    points: [
      { real: 0.2, imaginary: 0.1 },
      { real: 0.3, imaginary: 0.2 },
    ],
  }))
  paintSmithDiagram(
    probe.canvas,
    { width: 700, height: 500, radius: 1, curves: circular },
    { left: 0, top: 0, width: 700, height: 500 },
  )
  assert.deepEqual(probe.strokes, [
    probe.colors[1],
    probe.colors[2],
    probe.colors[0],
  ])
  assert.deepEqual(
    circular.map((curve) => curve.color),
    probe.colors,
  )
})

for (const axisFormat of ['db', 'smith', 'polar', 'invSmith'] as const) {
  test(`mixed ${axisFormat} axes paint Cartesian and circular projections together with a single clear`, (context) => {
    const probe = canvasProbe(context)
    const curves: MixedCurve[] = [
      {
        kind: 'cartesian',
        format: 'db',
        color: '#60e600',
        active: true,
        frequencyHz: [10e6, 20e6],
        values: [-20, -10],
        scale: { min: -90, max: 10 },
        dashed: false,
      },
      {
        kind: 'circular',
        format: 'invSmith',
        color: '#ffff26',
        active: false,
        radius: 1,
        points: [
          { real: 0.2, imaginary: 0.1 },
          { real: 0.3, imaginary: 0.2 },
        ],
      },
    ]
    paintMixedDiagram(
      probe.canvas,
      {
        width: 700,
        height: 500,
        rect: plotRect(700, 500),
        xMin: 10e6,
        xMax: 20e6,
        references: [],
        axisFormat,
        radius: 1,
        backgrounds: [
          { format: 'db', radius: 1 },
          { format: 'invSmith', radius: 1 },
        ],
        curves,
      },
      { left: 0, top: 0, width: 700, height: 500 },
    )
    assert.equal(probe.clears(), 1)
    assert.deepEqual(probe.strokes, ['#ffff26', '#60e600'])
    const firstCircular = probe.moves.find((move) => move.color === '#ffff26')!
    const original = smithPoint(
      { real: 0.2, imaginary: 0.1 },
      smithViewport(700, 500),
      1,
    )
    assert.equal(
      firstCircular.x,
      original.x,
      'Inv Smith changes the grid, never negates the complex trace',
    )
    assert.equal(firstCircular.y, original.y)
    assert.deepEqual(
      curves.map((curve) => curve.color),
      ['#60e600', '#ffff26'],
    )
  })
}

test('Log Mag maps only positive backend amplitudes into a logarithmic axis and keeps the reference value physical', () => {
  const scale = { min: 0.001, max: 10, referencePosition: 5 }
  assert.equal(normalizedY(0.1, scale, 'logMagnitude'), 0.5)
  assert.equal(referenceLevel(scale, 'logMagnitude'), 0.1)
  assert.ok(Number.isNaN(normalizedY(0, scale, 'logMagnitude')))
})
