import assert from 'node:assert/strict'
import test, { type TestContext } from 'node:test'
import { reactive, type Ref, type VNode } from 'vue'
import { plotRect } from '../src/plot/canvasRenderer'
import {
  paintMixedDiagram,
  type MixedBackground,
  type MixedCurve,
} from '../src/plot/mixedRenderer'
import {
  instrumentStatus,
  presentation,
  reflection,
  transmission,
} from './displayFixtures'
import { findElements, sfcHarness } from './sfcHarness'

function canvasProbe(context: TestContext) {
  const previousWindow = Object.getOwnPropertyDescriptor(globalThis, 'window')
  Object.defineProperty(globalThis, 'window', {
    configurable: true,
    value: { devicePixelRatio: 1 },
  })
  context.after(() => {
    if (previousWindow)
      Object.defineProperty(globalThis, 'window', previousWindow)
    else Reflect.deleteProperty(globalThis, 'window')
  })
  const strokes: { color: string; lines: number; arcs: number }[] = []
  const events: ('stroke' | 'label')[] = []
  let clears = 0
  let lines = 0
  let arcs = 0
  const drawing = {
    strokeStyle: '',
    setTransform() {},
    clearRect() {
      clears++
    },
    setLineDash() {},
    fillRect() {},
    beginPath() {
      lines = 0
      arcs = 0
    },
    lineTo() {
      lines++
    },
    closePath() {},
    fill() {},
    save() {},
    rect() {},
    clip() {},
    restore() {},
    arc() {
      arcs++
    },
    fillText() {
      events.push('label')
    },
    measureText(text: string) {
      return { width: text.length * 6 }
    },
    moveTo() {},
    stroke() {
      events.push('stroke')
      strokes.push({ color: this.strokeStyle, lines, arcs })
    },
  }
  const canvas = {
    width: 0,
    height: 0,
    style: {},
    getContext: () => drawing,
  } as unknown as HTMLCanvasElement
  return { canvas, strokes, events, clears: () => clears }
}

function paint(
  probe: ReturnType<typeof canvasProbe>,
  backgrounds: MixedBackground[],
  curves: MixedCurve[] = [],
) {
  paintMixedDiagram(
    probe.canvas,
    {
      width: 700,
      height: 500,
      rect: plotRect(700, 500),
      xMin: 10e6,
      xMax: 20e6,
      references: [],
      axisFormat: backgrounds[0]?.format ?? 'db',
      radius: backgrounds[0]?.radius ?? 1,
      backgrounds,
      curves,
    },
    { left: 0, top: 0, width: 700, height: 500 },
  )
}

for (const axisFormat of ['smith', 'db'] as const) {
  test(`a ${axisFormat} primary trace preserves both Smith and Cartesian backgrounds in one diagram`, (context) => {
    const probe = canvasProbe(context)
    const curves: MixedCurve[] = [
      {
        kind: 'cartesian',
        format: 'db',
        color: '#60e600',
        active: axisFormat === 'db',
        frequencyHz: [10e6, 20e6],
        values: [-20, -10],
        scale: { min: -90, max: 10 },
        dashed: false,
      },
      {
        kind: 'circular',
        format: 'smith',
        color: '#36c5d8',
        active: axisFormat === 'smith',
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
          { format: 'smith', radius: 1 },
        ],
        curves,
      },
      { left: 0, top: 0, width: 700, height: 500 },
    )
    assert.equal(probe.clears(), 1)
    const firstLabel = probe.events.indexOf('label')
    assert.ok(firstLabel > 0)
    assert.ok(
      probe.events.slice(firstLabel).every((event) => event === 'label'),
      'mixed backgrounds and traces must not paint over coordinate labels',
    )
    assert.ok(
      probe.strokes.some(
        (stroke) => stroke.color === '#4b5f6c' && stroke.lines === 11,
      ),
      'the Cartesian background must remain visible beside Smith traces',
    )
    assert.ok(
      probe.strokes.some(
        (stroke) => stroke.color === '#4b5f6c' && stroke.arcs > 0,
      ),
      'the Smith background must remain visible beside Cartesian traces',
    )
    const curveStrokes = probe.strokes.filter((stroke) =>
      ['#60e600', '#36c5d8'].includes(stroke.color),
    )
    assert.deepEqual(
      curveStrokes.map((stroke) => stroke.color),
      axisFormat === 'db' ? ['#36c5d8', '#60e600'] : ['#60e600', '#36c5d8'],
    )
    const firstCurve = probe.strokes.indexOf(curveStrokes[0]!)
    assert.ok(
      probe.strokes
        .slice(firstCurve)
        .every((stroke) => ['#60e600', '#36c5d8'].includes(stroke.color)),
      'all backgrounds must finish before any trace is painted',
    )
  })
}

test('Smith, Inv Smith and Polar backgrounds coexist even before measurement samples arrive', (context) => {
  const backgrounds: MixedBackground[] = [
    { format: 'db', radius: 1 },
    { format: 'smith', radius: 1 },
    { format: 'invSmith', radius: 1 },
    { format: 'polar', radius: 1 },
  ]
  const expected = backgrounds
    .map((background) => {
      const individual = canvasProbe(context)
      paint(individual, [background])
      return individual.strokes.filter((stroke) => stroke.color === '#4b5f6c')
        .length
    })
    .reduce((sum, count) => sum + count, 0)
  const combined = canvasProbe(context)
  paint(combined, backgrounds)
  assert.equal(combined.clears(), 1)
  assert.equal(
    combined.strokes.filter((stroke) => stroke.color === '#4b5f6c').length,
    expected,
  )
})

test('multiple traces sharing one grid do not darken or repeatedly paint the same background', (context) => {
  const single = canvasProbe(context)
  const repeated = canvasProbe(context)
  const backgrounds: MixedBackground[] = [
    { format: 'db', radius: 1 },
    { format: 'smith', radius: 1 },
  ]
  paint(single, backgrounds)
  paint(repeated, [
    ...backgrounds,
    ...backgrounds,
    { format: 'phase', radius: 1 },
    { format: 'swr', radius: 1 },
  ])
  assert.deepEqual(repeated.strokes, single.strokes)
  assert.equal(repeated.clears(), 1)
})

function hasClass(node: VNode, name: string) {
  return String(node.props?.class).split(/\s+/).includes(name)
}

test('visible backend formats supply mixed backgrounds without a frame, while axis labels follow the primary trace', async () => {
  const measurements = [reflection, transmission]
  const props = reactive({
    number: 1,
    layout: 'grid',
    frame: null,
    measurements,
    allMeasurements: measurements,
    presentations: {
      Reflect: presentation('smith'),
      Transmit: presentation('db'),
    },
    activeTraceName: 'Reflect',
    primaryTraceName: 'Reflect',
    selectedDiagram: 1,
    config: instrumentStatus(measurements).config,
    frameCondition: { kind: 'empty', label: '', detail: '' },
    canEditDisplay: true,
    commitMarker: async () => true,
  })
  props.presentations.Reflect.smithRadius = 2
  props.presentations.Transmit.smithRadius = 0.5
  const view = await sfcHarness(
    'src/components/diagrams/DiagramPane.vue',
    props,
    {
      window: { devicePixelRatio: 1 },
      requestAnimationFrame: () => 1,
      cancelAnimationFrame() {},
    },
  )
  try {
    const before = JSON.stringify(props)
    const backgrounds = view.bindings.backgrounds as Ref<MixedBackground[]>
    const labels = () =>
      findElements(view.render(), (node) => hasClass(node, 'y-tick'))
    assert.deepEqual(
      Array.from(backgrounds.value, (background) => ({ ...background })),
      [
        { format: 'smith', radius: 2 },
        { format: 'db', radius: 2 },
      ],
    )
    assert.equal(
      labels().length,
      0,
      'Smith primary keeps its own circular labels instead of borrowing dB labels',
    )
    assert.equal(JSON.stringify(props), before)
    props.primaryTraceName = 'Transmit'
    props.activeTraceName = 'Transmit'
    assert.equal(labels().length, 11)
    assert.deepEqual(
      Array.from(backgrounds.value, (background) => ({ ...background })),
      [
        { format: 'smith', radius: 2 },
        { format: 'db', radius: 0.5 },
      ],
    )
    props.presentations.Reflect.visible = false
    assert.deepEqual(
      Array.from(backgrounds.value, (background) => ({ ...background })),
      [{ format: 'db', radius: 0.5 }],
      'a hidden Smith trace does not leave its background in the diagram',
    )
    props.presentations.Reflect.visible = true
    props.presentations.Transmit.format = 'polar'
    assert.deepEqual(
      Array.from(backgrounds.value, (background) => ({ ...background })),
      [
        { format: 'smith', radius: 0.5 },
        { format: 'polar', radius: 0.5 },
      ],
      'circular primary shares its backend radius between both backgrounds and curves',
    )
    assert.deepEqual(view.emitted, [])
  } finally {
    view.dispose()
  }
})
