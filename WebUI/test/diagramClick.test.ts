import assert from 'node:assert/strict'
import test from 'node:test'
import { reactive, type Ref, type VNode } from 'vue'
import type { MarkerMove, MarkerPreviewPoint } from '../src/plot/markerDrag'
import type { TracePresentation } from '../src/domain/presentation'
import {
  acquiredFrame,
  markerDefinitions,
  markerProjection,
  markerProjections,
  presentation,
  projection,
  receiverTrace,
  reflection,
  transmission,
} from './displayFixtures'
import { findElements, sfcHarness } from './sfcHarness'

interface InteractionBindings {
  plotArea: Ref<unknown>
  markerPreviewPoints: (name: string) => MarkerPreviewPoint[]
  cancelDrag: () => void
}

type EventHandler = (event: unknown) => void

/** 通过真实模板绑定的事件重现点击症状，避免把回归绑到待重构的函数名。 */
async function diagram(canEditDisplay = true) {
  const traces = [reflection, transmission].map((measurement, traceIndex) => {
    const trace = receiverTrace(measurement, [0.1, 0.2, 0.3])
    const values = traceIndex ? [-64, -60, -62] : [-24, -20, -22]
    trace.projection = projection(values)
    trace.projection.markers = markerProjections()
    trace.projection.markers[0] = {
      id: 1,
      ...markerProjection({
        available: true,
        valid: true,
        frequencyHz: 20e6,
        sampleIndex: 1,
        real: 0.2,
        imag: 0,
        value: values[1],
        valueState: 'finite',
        reason: '',
      }),
    }
    trace.projection.markers[1] = {
      ...trace.projection.markers[0],
      id: 2,
      frequencyHz: 30e6,
      sampleIndex: 2,
      real: 0.3,
      value: values[2],
    }
    trace.projection.activeMarkerId = 1
    trace.projection.marker = { ...trace.projection.markers[0] }
    return trace
  })
  const frame = acquiredFrame(traces[0])
  frame.traces = traces
  const views: Record<string, TracePresentation> = {}
  for (const trace of traces) {
    const view = presentation()
    view.markers = markerDefinitions()
    view.markers[0] = {
      id: 1,
      enabled: true,
      frequencyHz: 20e6,
      sampleIndex: 1,
      discrete: true,
    }
    view.markers[1] = {
      ...view.markers[0],
      id: 2,
      frequencyHz: 30e6,
      sampleIndex: 2,
    }
    view.marker = { ...view.markers[0] }
    view.activeMarkerId = 1
    views[trace.name] = view
  }
  const commits: MarkerMove[] = []
  const props = reactive({
    number: 1,
    layout: 'single',
    frame,
    measurements: traces,
    allMeasurements: traces,
    presentations: views,
    activeTraceName: 'Reflect',
    primaryTraceName: 'Reflect',
    selectedDiagram: 1,
    config: { startHz: 10e6, stopHz: 30e6, points: 3, ifBandwidthHz: 1000 },
    frameCondition: { kind: 'current', label: '', detail: '' },
    canEditDisplay,
    commitMarker: async (command: MarkerMove) => {
      commits.push(command)
      return true
    },
  })
  let nextRequest = 0
  const callbacks = new Map<number, () => void>()
  const harness = await sfcHarness(
    'src/components/diagrams/DiagramPane.vue',
    props,
    {
      window: { devicePixelRatio: 1 },
      requestAnimationFrame: (callback: () => void) => {
        callbacks.set(++nextRequest, callback)
        return nextRequest
      },
      cancelAnimationFrame: (id: number) => callbacks.delete(id),
    },
  )
  const bindings = harness.bindings as unknown as InteractionBindings
  const plotTarget = {
    getBoundingClientRect: () => ({ left: 0, top: 0, width: 700, height: 500 }),
    setPointerCapture() {},
    hasPointerCapture: () => false,
    releasePointerCapture() {},
  }
  bindings.plotArea.value = plotTarget
  function event(x: number, y: number, type: string) {
    return {
      type,
      pointerId: 1,
      button: 0,
      clientX: x,
      clientY: y,
      currentTarget: plotTarget,
      target: { tagName: 'CANVAS', closest: () => null },
      preventDefault() {},
      stopPropagation() {},
    }
  }
  function handler(node: VNode, name: string): EventHandler {
    const callback = node.props?.[name]
    assert.equal(
      typeof callback,
      'function',
      `${name} must be bound by the SFC template`,
    )
    return callback as EventHandler
  }
  function marker(name: string, markerId: number): VNode {
    return findElements(
      harness.render(),
      (node) =>
        node.type === 'button' &&
        String(node.props?.class).includes('marker-handle') &&
        String(node.props?.['aria-label']).startsWith(`M${markerId} ${name},`),
    )[0]!
  }
  return {
    ...harness,
    props,
    bindings,
    commits,
    event,
    handler,
    marker,
    click(x: number, y: number) {
      const plot = findElements(
        harness.render(),
        (node) => node.type === 'div' && node.props?.class === 'plot-area',
      )[0]!
      handler(plot, 'onClick')(event(x, y, 'click'))
    },
    paint() {
      const pending = [...callbacks.values()]
      callbacks.clear()
      pending.forEach((callback) => callback())
    },
    saved() {
      return JSON.parse(JSON.stringify(props.presentations))
    },
  }
}

test('clicking empty plot space leaves Marker frequency and selected trace unchanged', async () => {
  const pane = await diagram()
  try {
    const saved = pane.saved()
    pane.click(175, 35)
    assert.deepEqual(
      pane.emitted,
      [],
      'Blank click must not issue an instrument edit',
    )
    assert.equal(pane.commits.length, 0)
    assert.deepEqual(pane.saved(), saved)
  } finally {
    pane.dispose()
  }
})

test('clicking the middle of an inactive trace segment selects its owner without moving a Marker', async () => {
  const pane = await diagram()
  try {
    const saved = pane.saved()
    const [a, b] = pane.bindings.markerPreviewPoints('Transmit')
    pane.click((a!.x + b!.x) / 2, (a!.y + b!.y) / 2)
    assert.deepEqual(pane.emitted, [['selectTrace', 'Transmit']])
    assert.equal(pane.commits.length, 0)
    assert.deepEqual(
      pane.saved(),
      saved,
      'Selection remains a backend request until readback',
    )
  } finally {
    pane.dispose()
  }
})

test('clicking blank space in another diagram requests diagram activation without changing Marker data', async () => {
  const pane = await diagram()
  try {
    pane.props.selectedDiagram = 2
    const saved = pane.saved()
    pane.click(175, 35)
    assert.deepEqual(pane.emitted, [['selectDiagram', 1]])
    assert.equal(pane.commits.length, 0)
    assert.deepEqual(pane.saved(), saved)
  } finally {
    pane.dispose()
  }
})

test('clicking blank space restores the diagram primary trace after a hidden definition was selected', async () => {
  const pane = await diagram()
  try {
    pane.props.activeTraceName = 'Hidden'
    const saved = pane.saved()
    pane.click(175, 35)
    assert.deepEqual(pane.emitted, [['selectDiagram', 1]])
    assert.equal(pane.commits.length, 0)
    assert.deepEqual(pane.saved(), saved)
  } finally {
    pane.dispose()
  }
})

test('clicking a trace in another diagram selects the hit trace instead of only activating the diagram', async () => {
  const pane = await diagram()
  try {
    pane.props.selectedDiagram = 2
    const saved = pane.saved()
    const [a, b] = pane.bindings.markerPreviewPoints('Transmit')
    pane.click((a!.x + b!.x) / 2, (a!.y + b!.y) / 2)
    assert.deepEqual(pane.emitted, [['selectTrace', 'Transmit']])
    assert.equal(pane.commits.length, 0)
    assert.deepEqual(pane.saved(), saved)
  } finally {
    pane.dispose()
  }
})

test('trace selection does not require an enabled Marker', async () => {
  const pane = await diagram()
  try {
    for (const view of Object.values(pane.props.presentations)) {
      view.marker.enabled = false
      view.markers?.forEach((marker) => (marker.enabled = false))
    }
    const saved = pane.saved()
    const [a, b] = pane.bindings.markerPreviewPoints('Transmit')
    pane.click((a!.x + b!.x) / 2, (a!.y + b!.y) / 2)
    assert.deepEqual(pane.emitted, [['selectTrace', 'Transmit']])
    assert.equal(pane.commits.length, 0)
    assert.deepEqual(pane.saved(), saved)
  } finally {
    pane.dispose()
  }
})

test('clicking a Marker body selects its trace and Marker identity once without repositioning', async () => {
  const pane = await diagram()
  try {
    const saved = pane.saved()
    const marker = pane.marker('Transmit', 2)
    const point = pane.bindings.markerPreviewPoints('Transmit')[2]!
    pane.handler(
      marker,
      'onPointerdown',
    )(pane.event(point.x, point.y, 'pointerdown'))
    pane.handler(
      marker,
      'onPointerup',
    )(pane.event(point.x, point.y, 'pointerup'))
    pane.handler(marker, 'onClick')(pane.event(point.x, point.y, 'click'))
    await new Promise((resolve) => setImmediate(resolve))
    assert.deepEqual(JSON.parse(JSON.stringify(pane.emitted)), [
      ['selectMarker', { name: 'Transmit', markerId: 2 }],
    ])
    assert.equal(pane.commits.length, 0)
    assert.deepEqual(pane.saved(), saved)
  } finally {
    pane.dispose()
  }
})

test('dragging a Marker previews locally and commits only once on release', async () => {
  const pane = await diagram()
  try {
    const saved = pane.saved()
    const marker = pane.marker('Transmit', 1)
    const start = pane.bindings.markerPreviewPoints('Transmit')[1]!
    const end = pane.bindings.markerPreviewPoints('Transmit')[2]!
    pane.handler(
      marker,
      'onPointerdown',
    )(pane.event(start.x, start.y, 'pointerdown'))
    pane.handler(
      marker,
      'onPointermove',
    )(pane.event(end.x, end.y, 'pointermove'))
    pane.paint()
    assert.equal(pane.commits.length, 0)
    assert.deepEqual(pane.saved(), saved)
    pane.handler(marker, 'onPointerup')(pane.event(end.x, end.y, 'pointerup'))
    pane.handler(marker, 'onClick')(pane.event(end.x, end.y, 'click'))
    await new Promise((resolve) => setImmediate(resolve))
    assert.equal(pane.commits.length, 1)
    assert.equal(pane.commits[0]!.name, 'Transmit')
    assert.equal(pane.commits[0]!.markerId, 1)
    assert.equal(pane.commits[0]!.frequencyHz, 30e6)
    assert.equal(
      pane.emitted.filter((item) => item[0] === 'moveMarker').length,
      0,
    )
    assert.deepEqual(pane.saved(), saved)
  } finally {
    pane.bindings.cancelDrag()
    pane.dispose()
  }
})

test('a read-only observer cannot select a trace or move Marker through plot clicks', async () => {
  const pane = await diagram(false)
  try {
    const [a, b] = pane.bindings.markerPreviewPoints('Transmit')
    pane.click(350, 35)
    pane.click((a!.x + b!.x) / 2, (a!.y + b!.y) / 2)
    assert.deepEqual(pane.emitted, [])
    assert.equal(pane.commits.length, 0)
  } finally {
    pane.dispose()
  }
})
