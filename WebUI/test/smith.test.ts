import assert from 'node:assert/strict'
import test from 'node:test'
import { getMarkerReadout } from '../src/plot'
import {
  nearestSmithIndex,
  smithPoint,
  smithReadout,
  smithSeries,
  smithViewport,
} from '../src/plot/smithMath'
import { paintSmithDiagram } from '../src/plot/smithRenderer'
import {
  acquiredFrame,
  markerProjection,
  presentation,
  projection,
  receiverTrace,
} from './displayFixtures'

function canvasProbe(context: { after: (callback: () => void) => void }) {
  const oldWindow = Object.getOwnPropertyDescriptor(globalThis, 'window')
  Object.defineProperty(globalThis, 'window', {
    configurable: true,
    value: { devicePixelRatio: 1.5 },
  })
  context.after(() => {
    if (oldWindow) Object.defineProperty(globalThis, 'window', oldWindow)
    else Reflect.deleteProperty(globalThis, 'window')
  })
  const lines: number[][] = []
  const isolated: number[][] = []
  const moves: { color: string; x: number; y: number }[] = []
  const drawing = {
    strokeStyle: '',
    setTransform() {},
    clearRect() {},
    setLineDash() {},
    fillRect() {},
    save() {},
    restore() {},
    beginPath() {},
    clip() {},
    stroke() {},
    rect() {},
    fill() {},
    fillText() {},
    arc(x: number, y: number) {
      if (this.strokeStyle === '#60e600') isolated.push([x, y])
    },
    moveTo(x: number, y: number) {
      if (['#60e600', '#ffff26'].includes(this.strokeStyle))
        moves.push({ color: this.strokeStyle, x, y })
    },
    lineTo(x: number, y: number) {
      if (this.strokeStyle === '#60e600') lines.push([x, y])
    },
  }
  const canvas = { style: {}, width: 0, height: 0, getContext: () => drawing }
  return { canvas, lines, isolated, moves }
}

test('Smith projection remains circular in wide and narrow viewports at unequal CSS scales and DPR', () => {
  for (const [width, height] of [
    [850, 650],
    [300, 650],
    [420, 300],
  ]) {
    for (const scaleX of [0.75, 1, 1.3]) {
      for (const scaleY of [0.8, 1.2]) {
        const viewport = smithViewport(width!, height!, scaleX, scaleY)
        const real = smithPoint({ real: 1, imaginary: 0 }, viewport, 1)
        const imaginary = smithPoint({ real: 0, imaginary: 1 }, viewport, 1)
        for (const dpr of [1, 1.5, 2]) {
          assert.ok(
            Math.abs(
              (real.x - viewport.centerX) * scaleX * dpr -
                (viewport.centerY - imaginary.y) * scaleY * dpr,
            ) < 1e-9,
          )
        }
        assert.equal(
          smithPoint({ real: 0, imaginary: 0 }, viewport, 1).x,
          width! / 2,
        )
      }
    }
  }
})

test('Smith renderer breaks the path at invalid points instead of connecting across the gap', (context) => {
  const { canvas, lines, isolated } = canvasProbe(context)
  paintSmithDiagram(
    canvas as unknown as HTMLCanvasElement,
    {
      width: 400,
      height: 300,
      radius: 1,
      curves: [
        {
          color: '#60e600',
          active: true,
          points: [
            { real: 0, imaginary: 0 },
            null,
            { real: 0.2, imaginary: 0.2 },
            { real: 0.3, imaginary: 0.3 },
          ],
        },
      ],
    },
    { width: 400, height: 300, left: 0.3, top: 0.2 },
  )
  assert.equal(lines.length, 1)
  assert.equal(isolated.length, 1)
  assert.ok(canvas.width >= 600 && canvas.height >= 450)
})

test('Smith overlay uses the active window radius for both traces and marker coordinates', (context) => {
  const { canvas, moves } = canvasProbe(context)
  const sample = { real: 0.3, imaginary: 0.4 }
  const curves = [
    { color: '#60e600', active: false, savedRadius: 0.2, points: [sample] },
    { color: '#ffff26', active: true, savedRadius: 2, points: [sample] },
  ]
  for (const radius of [1, 2]) {
    moves.length = 0
    paintSmithDiagram(
      canvas as unknown as HTMLCanvasElement,
      { width: 400, height: 300, radius, curves },
      { width: 400, height: 300, left: 0.3, top: 0.2 },
    )
    const marker = smithPoint(sample, smithViewport(400, 300), radius)
    assert.equal(moves.length, 2)
    for (const move of moves) {
      assert.equal(move.x, marker.x)
      assert.equal(move.y, marker.y)
    }
  }
})

test('Smith pixel hit testing uses the backend Gamma projection and retains points outside the unit circle', () => {
  const trace = receiverTrace()
  trace.real = [99, 99, 99, 99]
  trace.imag = [99, 99, 99, 99]
  trace.projection = projection([0.3, 0.3, null, 1.2], 'smith', [
    -0.3,
    0.3,
    null,
    0,
  ])
  const viewport = smithViewport(400, 400)
  const selected = smithPoint({ real: 0.3, imaginary: 0.3 }, viewport, 1)
  assert.equal(nearestSmithIndex(trace, viewport, 1, selected.x, selected.y), 1)
  assert.equal(smithSeries(trace)[2], null)
  assert.equal(smithSeries(trace)[3]!.real, 1.2)
})

test('Smith readouts format independent backend impedance without recomputing it from Gamma', () => {
  const frame = acquiredFrame()
  const trace = frame.traces[0]!
  trace.projection = projection([0.2, 0.3], 'smith', [0.1, 0.2])
  trace.projection.marker = markerProjection({
    available: true,
    valid: true,
    reason: '',
    sampleIndex: null,
    frequencyHz: 15e6,
    real: 0.25,
    imag: 0.125,
    value: 0.25,
    secondaryValue: 0.125,
    valueState: 'finite',
    secondaryState: 'finite',
    impedanceReal: 7.75,
    impedanceImag: -2.5,
    impedanceState: 'finite',
  })
  const setting = presentation('smith')
  setting.marker.enabled = true
  const readout = getMarkerReadout(frame, trace, setting)
  assert.match(readout!.valueText, /z 7.75 − j2.5/)
  assert.equal(readout!.index, null)
  assert.equal(
    smithReadout({
      real: 1,
      imaginary: 0,
      impedanceReal: null,
      impedanceImag: null,
      impedanceState: 'open',
    }),
    'Γ 1 + j0 · z ∞ (open)',
  )
})
