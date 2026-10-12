import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive, type Ref } from 'vue'
import {
  isCircularFormat,
  type DisplayFormat,
} from '../src/domain/presentation'
import { circularReadout } from '../src/plot/smithMath'
import type { MarkerMove, MarkerPreviewPoint } from '../src/plot/markerDrag'
import type { MarkerResult } from '../src/plot/traceMath'
import {
  acquiredFrame,
  markerProjection,
  presentation,
  projection,
  receiverTrace,
  markerDefinitions,
  markerProjections,
} from './displayFixtures'
import { findComponent, findElements, sfcHarness } from './sfcHarness'

interface PaneBindings {
  plotArea: Ref<unknown>
  canvas: Ref<unknown>
  schedulePaint: () => void
  plottedMarkers: Ref<
    {
      name: string
      markerId: number
      placement: { left: number; top: number; tipX: number; tipY: number }
    }[]
  >
  dragPreview: Ref<unknown>
  beginDrag: (event: unknown, name: string, markerId?: number) => void
  dragMarker: (event: unknown) => void
  endDrag: (event: unknown) => void
  cancelDrag: () => void
  markerPreviewPoints: (name: string) => MarkerPreviewPoint[]
  markerClick: (name: string, markerId: number) => void
  markerKey: (event: unknown, name: string, markerId: number) => void
}

interface DiagramOptions {
  format?: DisplayFormat
  values?: number[]
  secondaryValues?: number[]
  commit?: (command: MarkerMove) => Promise<boolean>
  multipleMarkers?: boolean
  markerValues?: (number | null)[]
  markerSecondaryValues?: (number | null)[]
}

async function diagram(options: DiagramOptions = {}) {
  const format = options.format ?? 'db'
  const values = options.values ?? [-12, -6, -2]
  const secondaryValues = options.secondaryValues ?? [0, 0, 0]
  const trace = receiverTrace(undefined, [0.25, 0.5, 0.75])
  trace.projection = projection(values, format, secondaryValues)
  if (format === 'smith') {
    // 固定服务器返回值，刻意不与 Γ 的本地换算结果相同。
    trace.projection.impedanceRealValues = [1.5, 2.5, 3.5]
    trace.projection.impedanceImagValues = [0.25, 0.5, 0.75]
    trace.projection.impedanceStates = ['finite', 'finite', 'finite']
  }
  trace.projection.marker = markerProjection({
    available: true,
    valid: true,
    frequencyHz: 10e6,
    sampleIndex: 0,
    real: isCircularFormat(format) ? values[0] : 0.25,
    imag: isCircularFormat(format) ? secondaryValues[0] : 0,
    value: values[0],
    secondaryValue: secondaryValues[0],
    valueState: 'finite',
    secondaryState: 'finite',
    impedanceReal: 1.5,
    impedanceImag: 0.25,
    impedanceState: 'finite',
    reason: '',
  })
  if (format === 'polar') {
    trace.projection.markerValues = options.markerValues ?? [3, 7, 11]
    trace.projection.markerSecondaryValues = options.markerSecondaryValues ?? [
      17, 29, 43,
    ]
    trace.projection.markerValueStates = trace.projection.markerValues.map(
      (value) => (value === null ? 'undefined' : 'finite'),
    )
    trace.projection.markerSecondaryStates =
      trace.projection.markerSecondaryValues.map((value) =>
        value === null ? 'undefined' : 'finite',
      )
    trace.projection.marker.value = trace.projection.markerValues[0]!
    trace.projection.marker.secondaryValue =
      trace.projection.markerSecondaryValues[0]!
    trace.projection.marker.valueState = trace.projection.markerValueStates[0]!
    trace.projection.marker.secondaryState =
      trace.projection.markerSecondaryStates[0]!
  }
  const frame = acquiredFrame(trace)
  const view = presentation(format)
  if (format !== 'db')
    view.scale = { min: -180, max: 180, referencePosition: 5 }
  view.marker = {
    enabled: true,
    frequencyHz: 10e6,
    sampleIndex: 0,
    discrete: true,
  }
  if (options.multipleMarkers) {
    view.activeMarkerId = 1
    view.markers = markerDefinitions()
    view.markers[0] = { id: 1, ...view.marker }
    view.markers[1] = {
      id: 2,
      enabled: true,
      frequencyHz: 20e6,
      sampleIndex: 1,
      discrete: true,
    }
    trace.projection.activeMarkerId = 1
    trace.projection.markers = markerProjections()
    trace.projection.markers[0] = { id: 1, ...trace.projection.marker }
    trace.projection.markers[1] = {
      id: 2,
      ...trace.projection.marker,
      frequencyHz: 20e6,
      sampleIndex: 1,
      value: values[1]!,
      real: 0.5,
    }
  }
  const commits: MarkerMove[] = []
  const props = reactive({
    number: 1,
    layout: 'single',
    frame,
    measurements: [trace],
    allMeasurements: [trace],
    presentations: { Reflect: view },
    activeTraceName: 'Reflect',
    primaryTraceName: 'Reflect',
    config: { startHz: 10e6, stopHz: 30e6, points: 3, ifBandwidthHz: 1000 },
    frameCondition: { kind: 'current', label: '', detail: '' },
    canEditDisplay: true,
    commitMarker: async (value: MarkerMove) => {
      commits.push(value)
      return options.commit ? options.commit(value) : true
    },
  })
  let request = 0
  const callbacks = new Map<number, () => void>()
  const harness = await sfcHarness(
    'src/components/diagrams/DiagramPane.vue',
    props,
    {
      window: { devicePixelRatio: 1 },
      requestAnimationFrame: (callback: () => void) => {
        callbacks.set(++request, callback)
        return request
      },
      cancelAnimationFrame: (id: number) => callbacks.delete(id),
    },
  )
  const bindings = harness.bindings as unknown as PaneBindings
  bindings.plotArea.value = {
    getBoundingClientRect: () => ({ left: 0, top: 0, width: 700, height: 500 }),
  }
  const target = {
    setPointerCapture() {},
    hasPointerCapture: () => false,
    releasePointerCapture() {},
  }
  const pointer = (x: number, y: number, type = 'pointermove') => ({
    pointerId: 1,
    button: 0,
    clientX: x,
    clientY: y,
    type,
    currentTarget: target,
    preventDefault() {},
    stopPropagation() {},
  })
  return {
    ...harness,
    props,
    bindings,
    commits,
    pointer,
    readout() {
      return findComponent(harness.render(), 'MarkerReadout')!.props!
        .markers as {
        name: string
        markerId: number
        result: MarkerResult
        active: boolean
        preview?: boolean
      }[]
    },
    begin(markerId = 1) {
      const placement = bindings.plottedMarkers.value.find(
        (marker) => marker.markerId === markerId,
      )!.placement
      bindings.beginDrag(
        pointer(
          placement.left + placement.tipX,
          placement.top + placement.tipY,
          'pointerdown',
        ),
        'Reflect',
        markerId,
      )
    },
    moveTo(index: number) {
      const point = bindings.markerPreviewPoints('Reflect')[index]!
      bindings.dragMarker(pointer(point.x, point.y))
      return point
    },
    paint() {
      const pending = [...callbacks.values()]
      callbacks.clear()
      pending.forEach((callback) => callback())
    },
  }
}

test('the active marker glyph is layered above overlapping inactive markers while title order remains unchanged', async () => {
  const pane = await diagram({ multipleMarkers: true })
  try {
    const handles = findElements(
      pane.render(),
      (node) =>
        node.type === 'button' &&
        String(node.props?.class).includes('marker-handle'),
    )
    const active = handles.find((node) =>
      String(node.props!.class).includes('active'),
    )!
    const inactive = handles.find(
      (node) => !String(node.props!.class).includes('active'),
    )!
    assert.ok(active.props!.style.zIndex > inactive.props!.style.zIndex)
    const titles = findElements(
      pane.render(),
      (node) =>
        typeof node.type === 'object' &&
        Reflect.get(node.type, 'name') === 'TraceStrip',
    )
    assert.deepEqual(
      titles.map((node) => node.props!.measurement.name),
      ['Reflect'],
    )
  } finally {
    pane.dispose()
  }
})

test('actual Polar dragging uses backend magnitude and phase arrays while the glyph follows the original complex projection', async () => {
  const pane = await diagram({
    format: 'polar',
    values: [0.1, 0.2, 0.3],
    secondaryValues: [0, 0.1, 0.2],
    markerValues: [3, 7, 11],
    markerSecondaryValues: [17, 29, 43],
  })
  try {
    pane.begin()
    const point = pane.moveTo(2)
    pane.paint()
    const result = pane.readout()[0]!.result
    assert.equal(
      result.value,
      11,
      'Magnitude must be copied from the independent backend marker projection',
    )
    assert.equal(result.secondaryValue, 43)
    assert.equal(result.real, 0.3)
    assert.equal(result.imaginary, 0.2)
    assert.equal(result.index, point.sampleIndex)
    assert.match(circularReadout('polar', result), /\|z\| 11 1 \/ ∠ 43°/)
    assert.equal(pane.commits.length, 0)
  } finally {
    pane.bindings.cancelDrag()
    pane.dispose()
  }
})

test('Polar zero-amplitude dragging keeps the circle center and a zero magnitude with undefined phase', async () => {
  const pane = await diagram({
    format: 'polar',
    values: [0.1, 0, 0.3],
    secondaryValues: [0, 0, 0.2],
    markerValues: [3, 0, 11],
    markerSecondaryValues: [17, null, 43],
  })
  try {
    const zero = pane.bindings
      .markerPreviewPoints('Reflect')
      .find((point) => point.sampleIndex === 1)!
    assert.ok(
      zero,
      'Undefined phase must not discard the valid zero-amplitude circle-center point',
    )
    pane.begin()
    pane.moveTo(1)
    pane.paint()
    const result = pane.readout()[0]!.result
    assert.equal(result.value, 0)
    assert.ok(Number.isNaN(result.secondaryValue))
    assert.equal(result.real, 0)
    assert.equal(result.imaginary, 0)
    assert.match(circularReadout('polar', result), /\|z\| 0 1 \/ ∠ —°/)
    assert.equal(pane.commits.length, 0)
  } finally {
    pane.bindings.cancelDrag()
    pane.dispose()
  }
})

test('the actual pane paints mixed Cartesian and Smith traces in one diagram and keeps per-trace marker coordinates', async () => {
  const pane = await diagram({
    format: 'smith',
    values: [0.1, 0.2, 0.3],
    secondaryValues: [0, 0.1, 0.2],
  })
  try {
    const other = receiverTrace(
      { ...pane.props.frame.traces[0]!, name: 'Other' },
      [0.25, 0.5, 0.75],
    )
    other.projection.marker = markerProjection({
      available: true,
      valid: true,
      reason: '',
      sampleIndex: 0,
      frequencyHz: 10e6,
      real: 0.25,
      imag: 0,
      value: -13.5,
      valueState: 'finite',
    })
    const view = presentation()
    view.marker = {
      enabled: true,
      frequencyHz: 10e6,
      sampleIndex: 0,
      discrete: true,
    }
    pane.props.frame.traces.push(other)
    pane.props.measurements.push(other)
    pane.props.allMeasurements.push(other)
    Reflect.set(pane.props.presentations, 'Other', view)
    const strokes: string[] = []
    const context = {
      strokeStyle: '',
      setTransform() {},
      clearRect() {},
      setLineDash() {},
      fillRect() {},
      beginPath() {},
      moveTo() {},
      lineTo() {},
      closePath() {},
      fill() {},
      save() {},
      rect() {},
      clip() {},
      restore() {},
      arc() {},
      fillText() {},
      stroke() {
        if (['#60e600', '#ffff26'].includes(this.strokeStyle))
          strokes.push(this.strokeStyle)
      },
    }
    pane.bindings.canvas.value = {
      width: 0,
      height: 0,
      style: {},
      getContext: () => context,
    }
    pane.bindings.schedulePaint()
    pane.paint()
    assert.deepEqual(
      strokes,
      ['#ffff26', '#60e600'],
      'Mixed traces must both draw, with the active Smith trace last',
    )
    const marker = pane.bindings.plottedMarkers.value.find(
      (marker) => marker.name === 'Other',
    )!
    assert.equal(
      marker.placement.left + marker.placement.tipX,
      1,
      'Cartesian marker retains the frequency x axis even under Smith grid',
    )
    assert.equal(pane.readout().length, 2)
    const titles = findElements(
      pane.render(),
      (node) =>
        typeof node.type === 'object' &&
        Reflect.get(node.type, 'name') === 'TraceStrip',
    )
    assert.deepEqual(
      titles.map((node) => node.props!.measurement.name),
      ['Reflect', 'Other'],
    )
  } finally {
    pane.dispose()
  }
})

test('multiple enabled markers retain independent readouts and selecting M2 requests its backend identity', async () => {
  const pane = await diagram({ multipleMarkers: true })
  try {
    assert.deepEqual(
      pane
        .readout()
        .map((marker) => [
          marker.markerId,
          marker.result.frequencyHz,
          marker.active,
        ]),
      [
        [1, 10e6, true],
        [2, 20e6, false],
      ],
    )
    assert.equal(pane.bindings.plottedMarkers.value.length, 2)
    pane.bindings.markerClick('Reflect', 2)
    assert.deepEqual(JSON.parse(JSON.stringify(pane.emitted)), [
      ['selectMarker', { name: 'Reflect', markerId: 2 }],
    ])
    assert.equal(
      pane.props.presentations.Reflect.activeMarkerId,
      1,
      'Click cannot mutate the saved selection before readback',
    )
    pane.bindings.markerKey(
      {
        key: 'ArrowRight',
        shiftKey: false,
        preventDefault() {},
        stopPropagation() {},
      },
      'Reflect',
      2,
    )
    assert.deepEqual(JSON.parse(JSON.stringify(pane.emitted[1])), [
      'moveMarker',
      { name: 'Reflect', markerId: 2, step: 1, discrete: true },
    ])
  } finally {
    pane.dispose()
  }
})

test('dragging inactive M2 previews only M2 and submits its identity once without touching M1', async () => {
  const pane = await diagram({ multipleMarkers: true })
  try {
    pane.begin(2)
    pane.moveTo(2)
    pane.paint()
    assert.equal(pane.readout()[0]!.result.frequencyHz, 10e6)
    assert.equal(pane.readout()[0]!.preview, false)
    assert.equal(pane.readout()[1]!.result.frequencyHz, 30e6)
    assert.equal(pane.readout()[1]!.preview, true)
    assert.equal(pane.commits.length, 0)
    pane.bindings.endDrag(pane.pointer(699, 150, 'pointerup'))
    await new Promise((resolve) => setImmediate(resolve))
    assert.equal(pane.commits.length, 1)
    assert.equal(pane.commits[0]!.markerId, 2)
    assert.equal(
      pane.props.presentations.Reflect.markers![0]!.frequencyHz,
      10e6,
    )
    assert.equal(
      pane.props.frame.traces[0]!.projection.markers![0]!.frequencyHz,
      10e6,
    )
    assert.equal(pane.readout()[1]!.result.frequencyHz, 20e6)
  } finally {
    pane.bindings.cancelDrag()
    pane.dispose()
  }
})

test('all ten saved marker projections render together and readout clicks target M10', async () => {
  const pane = await diagram({ multipleMarkers: true })
  try {
    const presentation = pane.props.presentations.Reflect
    const projection = pane.props.frame.traces[0]!.projection
    presentation.activeMarkerId = 10
    presentation.markers!.forEach((marker) => {
      marker.enabled = true
      marker.frequencyHz = 10e6
      marker.sampleIndex = 0
    })
    projection.markers = projection.markers!.map((marker) => ({
      ...projection.marker,
      id: marker.id,
    }))
    const markers = pane.readout()
    assert.equal(markers.length, 10)
    assert.equal(pane.bindings.plottedMarkers.value.length, 10)
    assert.equal(markers.filter((marker) => marker.active).length, 1)
    assert.equal(markers[9]!.active, true)
    const readout = findComponent(pane.render(), 'MarkerReadout')!
    readout.props!.onSelect({ name: 'Reflect', markerId: 10 })
    assert.deepEqual(JSON.parse(JSON.stringify(pane.emitted[0])), [
      'selectMarker',
      { name: 'Reflect', markerId: 10 },
    ])
  } finally {
    pane.dispose()
  }
})

test('actual pointermove updates MarkerReadout XY and ghost in the same display frame without backend writes', async () => {
  const pane = await diagram()
  try {
    assert.equal(pane.readout()[0]!.result.frequencyHz, 10e6)
    pane.begin()
    pane.bindings.dragMarker(pane.pointer(699, 150))
    pane.paint()
    assert.ok(
      pane.bindings.dragPreview.value,
      'Ghost must be visible before readback',
    )
    assert.equal(
      pane.readout()[0]!.result.frequencyHz,
      30e6,
      'Readout still shows the saved marker X while its ghost moved',
    )
    assert.equal(
      pane.readout()[0]!.result.value,
      -2,
      'Readout Y must come from the selected server projection',
    )
    assert.equal(
      pane.props.frame.traces[0]!.projection.marker.frequencyHz,
      10e6,
    )
    assert.deepEqual(pane.commits, [])
    pane.bindings.cancelDrag()
    assert.equal(pane.readout()[0]!.result.frequencyHz, 10e6)
  } finally {
    pane.dispose()
  }
})

for (const [format, values, secondaryValues] of [
  ['magnitude', [0.2, 0.8, 1.5], [0, 0, 0]],
  ['phase', [10, 20, 37], [0, 0, 0]],
  ['real', [0.2, 0.4, 0.6], [0, 0, 0]],
  ['imag', [0.1, 0.3, 0.9], [0, 0, 0]],
  ['realimag', [0.2, 0.4, 0.6], [0.1, 0.3, 0.9]],
] as [DisplayFormat, number[], number[]][]) {
  test(`actual ${format} drag reads server-formatted primary and secondary values without reformatting measurement data`, async () => {
    const pane = await diagram({ format, values, secondaryValues })
    try {
      pane.begin()
      pane.moveTo(2)
      pane.paint()
      const marker = pane.readout()[0]!
      assert.equal(marker.result.frequencyHz, 30e6)
      assert.equal(marker.result.value, values[2])
      assert.equal(marker.result.secondaryValue, secondaryValues[2])
      assert.equal(marker.preview, true)
      assert.deepEqual(pane.commits, [])
    } finally {
      pane.bindings.cancelDrag()
      pane.dispose()
    }
  })
}

test('Smith ghost and readout select the same original sample, including the server-projected impedance', async () => {
  const pane = await diagram({
    format: 'smith',
    values: [0.1, 0.2, 0.3],
    secondaryValues: [0, 0.1, 0.2],
  })
  try {
    pane.props.frame.sampleIndexes = [0, 7, 14]
    pane.props.frame.originalPointCount = 15
    pane.begin()
    const point = pane.moveTo(2)
    pane.paint()
    const result = pane.readout()[0]!.result
    assert.equal(result.index, point.sampleIndex)
    assert.equal(result.index, 14)
    assert.equal(result.frequencyHz, 30e6)
    assert.equal(result.real, 0.3)
    assert.equal(result.imaginary, 0.2)
    assert.equal(result.impedanceReal, 3.5)
    assert.equal(result.impedanceImag, 0.75)
    assert.equal(result.impedanceState, 'finite')
    assert.equal(
      pane.props.frame.traces[0]!.projection.marker.frequencyHz,
      10e6,
    )
  } finally {
    pane.bindings.cancelDrag()
    pane.dispose()
  }
})

test('a legacy Smith projection previews new Gamma without borrowing the old marker impedance', async () => {
  const pane = await diagram({
    format: 'smith',
    values: [0.1, 0.2, 0.3],
    secondaryValues: [0, 0.1, 0.2],
  })
  try {
    const projection = pane.props.frame.traces[0]!.projection
    projection.impedanceRealValues = []
    projection.impedanceImagValues = []
    projection.impedanceStates = []
    pane.begin()
    pane.moveTo(2)
    pane.paint()
    const result = pane.readout()[0]!.result
    assert.equal(result.real, 0.3)
    assert.equal(result.impedanceState, 'invalid')
    assert.equal(result.impedanceReal, null)
    assert.equal(result.impedanceImag, null)
    assert.equal(projection.marker.impedanceReal, 1.5)
  } finally {
    pane.bindings.cancelDrag()
    pane.dispose()
  }
})

test('subsequent CONT frames do not replace the pixels or values of the current drag snapshot', async () => {
  const pane = await diagram()
  try {
    pane.begin()
    pane.moveTo(2)
    pane.paint()
    pane.props.frame = {
      ...pane.props.frame,
      frameSeq: '2',
      traces: [
        {
          ...pane.props.frame.traces[0]!,
          projection: {
            ...pane.props.frame.traces[0]!.projection,
            values: [-17, -11, -7],
          },
        },
      ],
    }
    await nextTick()
    pane.bindings.dragMarker(pane.pointer(699, 150))
    pane.paint()
    assert.equal(pane.readout()[0]!.result.value, -2)
    assert.equal(pane.readout()[0]!.result.frequencyHz, 30e6)
    assert.deepEqual(pane.commits, [])
  } finally {
    pane.bindings.cancelDrag()
    pane.dispose()
  }
})

test('pointer release submits once and keeps its XY preview until completed readback', async () => {
  let complete!: (value: boolean) => void
  const pane = await diagram({
    commit: () =>
      new Promise((resolve) => {
        complete = resolve
      }),
  })
  try {
    pane.begin()
    pane.moveTo(2)
    pane.paint()
    pane.bindings.endDrag(pane.pointer(699, 150, 'pointerup'))
    assert.equal(pane.commits.length, 1)
    assert.equal(pane.readout()[0]!.result.frequencyHz, 30e6)
    assert.equal(pane.readout()[0]!.preview, true)
    pane.props.canEditDisplay = false
    await nextTick()
    assert.equal(pane.readout()[0]!.result.frequencyHz, 30e6)
    pane.props.frame.traces[0]!.projection.marker = markerProjection({
      available: true,
      valid: true,
      frequencyHz: 30e6,
      sampleIndex: 2,
      real: 0.75,
      imag: 0,
      value: -2,
      valueState: 'finite',
      reason: '',
    })
    complete(true)
    await new Promise((resolve) => setImmediate(resolve))
    assert.equal(pane.bindings.dragPreview.value, null)
    assert.equal(pane.readout()[0]!.result.frequencyHz, 30e6)
    assert.equal(pane.readout()[0]!.preview, false)
    assert.equal(pane.commits.length, 1)
  } finally {
    pane.bindings.cancelDrag()
    pane.dispose()
  }
})

test('failed release restores the authoritative XY without saving the preview', async () => {
  const pane = await diagram({ commit: async () => false })
  try {
    pane.begin()
    pane.moveTo(2)
    pane.paint()
    pane.bindings.endDrag(pane.pointer(699, 150, 'pointerup'))
    await new Promise((resolve) => setImmediate(resolve))
    assert.equal(pane.bindings.dragPreview.value, null)
    assert.equal(pane.readout()[0]!.result.frequencyHz, 10e6)
    assert.equal(pane.readout()[0]!.result.value, -12)
    assert.equal(pane.commits.length, 1)
  } finally {
    pane.dispose()
  }
})

for (const change of [
  'readonly',
  'format',
  'axis',
  'catalog',
  'hidden',
] as const) {
  test(`${change} invalidates the real drag preview and restores server XY`, async () => {
    const pane = await diagram()
    try {
      pane.begin()
      pane.moveTo(2)
      pane.paint()
      switch (change) {
        case 'readonly':
          pane.props.canEditDisplay = false
          break
        case 'format':
          pane.props.presentations.Reflect.format = 'phase'
          break
        case 'axis':
          pane.props.frame.revision = '2'
          break
        case 'catalog':
          pane.props.frame.measurementRevision = '2'
          break
        case 'hidden':
          pane.props.presentations.Reflect.visible = false
          break
      }
      await nextTick()
      assert.equal(pane.bindings.dragPreview.value, null)
      assert.deepEqual(pane.commits, [])
      if (change === 'hidden') assert.deepEqual(pane.readout(), [])
      else if (change !== 'format')
        assert.equal(pane.readout()[0]!.result.frequencyHz, 10e6)
    } finally {
      pane.dispose()
    }
  })
}

test('pointer cancellation restores the saved XY and does not submit a backend command', async () => {
  const pane = await diagram()
  try {
    pane.begin()
    pane.moveTo(2)
    pane.paint()
    assert.equal(pane.readout()[0]!.result.frequencyHz, 30e6)
    pane.bindings.endDrag(pane.pointer(699, 150, 'pointercancel'))
    assert.equal(pane.bindings.dragPreview.value, null)
    assert.equal(pane.readout()[0]!.result.frequencyHz, 10e6)
    assert.equal(pane.readout()[0]!.result.value, -12)
    assert.deepEqual(pane.commits, [])
  } finally {
    pane.dispose()
  }
})

for (const state of ['open', 'overflow', 'invalid'] as const) {
  test(`Smith dragging displays the server ${state} impedance state without keeping stale finite values`, async () => {
    const pane = await diagram({
      format: 'smith',
      values: [0.1, 0.2, 0.3],
      secondaryValues: [0, 0.1, 0.2],
    })
    try {
      const projection = pane.props.frame.traces[0]!.projection
      projection.impedanceRealValues[2] = null
      projection.impedanceImagValues[2] = null
      projection.impedanceStates[2] = state
      pane.begin()
      pane.moveTo(2)
      pane.paint()
      const result = pane.readout()[0]!.result
      assert.equal(result.impedanceState, state)
      assert.equal(result.impedanceReal, null)
      assert.equal(result.impedanceImag, null)
    } finally {
      pane.bindings.cancelDrag()
      pane.dispose()
    }
  })
}
