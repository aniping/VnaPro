import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive, type Ref } from 'vue'
import type { DisplayFormat } from '../src/domain/presentation'
import type { MarkerMove, MarkerPreviewPoint } from '../src/plot/markerDrag'
import type { MarkerResult } from '../src/plot/traceMath'
import {
  acquiredFrame,
  markerProjection,
  presentation,
  projection,
  receiverTrace,
} from './displayFixtures'
import { findComponent, sfcHarness } from './sfcHarness'

interface PaneBindings {
  plotArea: Ref<unknown>
  plottedMarkers: Ref<
    {
      name: string
      placement: { left: number; top: number; tipX: number; tipY: number }
    }[]
  >
  dragPreview: Ref<unknown>
  beginDrag: (event: unknown, name: string) => void
  dragMarker: (event: unknown) => void
  endDrag: (event: unknown) => void
  cancelDrag: () => void
  markerPreviewPoints: (name: string) => MarkerPreviewPoint[]
}

interface DiagramOptions {
  format?: DisplayFormat
  values?: number[]
  secondaryValues?: number[]
  commit?: (command: MarkerMove) => Promise<boolean>
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
    real: format === 'smith' ? values[0] : 0.25,
    imag: format === 'smith' ? secondaryValues[0] : 0,
    value: values[0],
    secondaryValue: secondaryValues[0],
    valueState: 'finite',
    secondaryState: 'finite',
    impedanceReal: 1.5,
    impedanceImag: 0.25,
    impedanceState: 'finite',
    reason: '',
  })
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
        .markers as { name: string; result: MarkerResult; preview?: boolean }[]
    },
    begin() {
      const placement = bindings.plottedMarkers.value[0]!.placement
      bindings.beginDrag(
        pointer(
          placement.left + placement.tipX,
          placement.top + placement.tipY,
          'pointerdown',
        ),
        'Reflect',
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
