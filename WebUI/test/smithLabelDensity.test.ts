import assert from 'node:assert/strict'
import test, { type TestContext } from 'node:test'
import { paintSmithDiagram } from '../src/plot/smithRenderer'
import { smithPoint, smithViewport } from '../src/plot/smithMath'

function canvasProbe(context: TestContext) {
  const previousWindow = Object.getOwnPropertyDescriptor(globalThis, 'window')
  Object.defineProperty(globalThis, 'window', {
    configurable: true,
    value: { devicePixelRatio: 1.5 },
  })
  context.after(() => {
    if (previousWindow)
      Object.defineProperty(globalThis, 'window', previousWindow)
    else Reflect.deleteProperty(globalThis, 'window')
  })
  const labels: { text: string; x: number; y: number }[] = []
  const arcs: number[][] = []
  const curveMoves: number[][] = []
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
    lineTo() {},
    fillText(text: string, x: number, y: number) {
      labels.push({ text, x, y })
    },
    arc(x: number, y: number, radius: number) {
      arcs.push([x, y, radius])
    },
    moveTo(x: number, y: number) {
      if (this.strokeStyle === '#60e600') curveMoves.push([x, y])
    },
  }
  const canvas = {
    style: {},
    width: 0,
    height: 0,
    getContext: () => drawing,
  } as unknown as HTMLCanvasElement
  return { canvas, labels, arcs, curveMoves }
}

for (const format of ['smith', 'invSmith'] as const) {
  test(`a small ${format} circle reduces text density while retaining grid and trace geometry`, (context) => {
    const probe = canvasProbe(context)
    const width = 180
    const height = 130
    const sample = { real: 0.2, imaginary: 0.1 }
    paintSmithDiagram(
      probe.canvas,
      {
        width,
        height,
        radius: 1,
        format,
        curves: [
          {
            color: '#60e600',
            active: true,
            points: [sample, { real: 0.3, imaginary: 0.2 }],
          },
        ],
      },
      { width, height, left: 0.3, top: 0.2 },
    )
    assert.ok(
      probe.labels.length < 15,
      'the compact circle must omit crowded intermediate coordinate labels',
    )
    assert.deepEqual(
      probe.labels.map((label) => label.text),
      ['0', '1', '∞', '-1', '1'],
    )
    assert.equal(
      probe.arcs.length,
      17,
      'coordinate text thinning must preserve every grid circle',
    )
    const projected = smithPoint(sample, smithViewport(width, height), 1)
    assert.deepEqual(
      probe.curveMoves,
      [[projected.x, projected.y]],
      'trace and marker projections must remain unchanged',
    )
  })

  test(`a large ${format} circle preserves the original complete coordinate labels`, (context) => {
    const probe = canvasProbe(context)
    paintSmithDiagram(
      probe.canvas,
      {
        width: 700,
        height: 500,
        radius: 1,
        format,
        curves: [],
      },
      { width: 700, height: 500, left: 0, top: 0 },
    )
    assert.deepEqual(
      probe.labels.map((label) => label.text),
      [
        '0',
        '0.2',
        '0.5',
        '1',
        '2',
        '5',
        '∞',
        '-0.5',
        '0.5',
        '-1',
        '1',
        '-2',
        '2',
        '-5',
        '5',
      ],
    )
  })

  test(`${format} coordinate density follows the unit circle size at the saved backend radius`, (context) => {
    const probe = canvasProbe(context)
    const radius = 5
    const sample = { real: 0.2, imaginary: 0.1 }
    const diagram = {
      width: 700,
      height: 500,
      radius,
      format,
      curves: [
        {
          color: '#60e600',
          active: true,
          points: [sample, { real: 0.3, imaginary: 0.2 }],
        },
      ],
    }
    const before = JSON.stringify(diagram)
    paintSmithDiagram(probe.canvas, diagram, {
      width: 700,
      height: 500,
      left: 0,
      top: 0,
    })
    assert.equal(probe.labels.length, 5)
    assert.equal(
      probe.arcs.length,
      18,
      'the saved radius outline and all Smith grid circles remain present',
    )
    const projected = smithPoint(sample, smithViewport(700, 500), radius)
    assert.deepEqual(probe.curveMoves, [[projected.x, projected.y]])
    assert.equal(JSON.stringify(diagram), before)
  })
}
