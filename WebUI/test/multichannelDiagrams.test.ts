import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive, type Ref } from 'vue'
import type { AcquiredFrame, ChannelStatus, FrameResponse, StimulusZoomSelection } from '../src/api/types'
import type { MarkerMove } from '../src/plot/markerDrag'
import { paintDiagram } from '../src/plot/canvasRenderer'
import { hitTestTrace, type SelectableCurve } from '../src/plot/traceHitTest'
import { acquiredFrame, instrumentStatus, markerProjection, presentation, projection, receiverTrace, reflection, transmission } from './displayFixtures'
import { findComponent, findElements, sfcHarness } from './sfcHarness'

const copy = (value: unknown) => JSON.parse(JSON.stringify(value))

function channelsFixture() {
  const measurements = [{ ...reflection, channelId: 1 }, { ...transmission, channelId: 2 }]
  const frames = measurements.map((measurement, index) => {
    const axis = index ? [1e9, 2e9, 3e9, 4e9, 5e9] : [10e6, 20e6, 30e6]
    const trace = receiverTrace(measurement, axis.map(() => 0.5))
    trace.projection = projection(axis.map(() => index ? -20 : -60))
    trace.projection.marker = markerProjection({ available: true, valid: true, frequencyHz: axis[1],
      sampleIndex: 1, real: 0.5, imag: 0, value: index ? -20 : -60, valueState: 'finite' })
    return { ...acquiredFrame(trace), channelId: index + 1, frequencyHz: axis,
      revision: `${index + 11}`, runId: `${index + 21}`, displayRevision: '7' }
  })
  const views = { Reflect: presentation(), Transmit: presentation() }
  for (const [index, view] of Object.values(views).entries())
    view.marker = { enabled: true, frequencyHz: frames[index]!.frequencyHz[1]!, sampleIndex: 1, discrete: true }
  const channels: ChannelStatus[] = frames.map((frame) => ({ channelId: frame.channelId,
    name: `Ch${frame.channelId}`, state: 'Ready', error: '', acquisitionRevision: frame.revision,
    runId: frame.runId, activeConfigValid: true, config: { ...instrumentStatus().config,
      startHz: frame.frequencyHz[0]!, stopHz: frame.frequencyHz.at(-1)!,
      points: frame.originalPointCount, ifBandwidthHz: frame.channelId * 1000, powerDbm: -5 * frame.channelId } }))
  return { measurements, frames, views, channels }
}

async function multichannelPane() {
  const fixture = channelsFixture()
  const moves: MarkerMove[] = []
  const props = reactive({ number: 1, layout: 'single', frame: fixture.frames[0]!,
    channelFrames: { 1: fixture.frames[0]!, 2: fixture.frames[1]! } as Record<number, FrameResponse>,
    channels: fixture.channels, activeChannelId: 1, connected: true,
    measurements: fixture.measurements, allMeasurements: fixture.measurements,
    presentations: fixture.views, activeTraceName: 'Reflect', primaryTraceName: 'Reflect', selectedDiagram: 1,
    displayRevision: '7', revision: '1', graphicalZoomSelecting: false, graphicalZoomReset: 0,
    zoomSelecting: false, canZoom: true, canEditDisplay: true, canContinueReferenceDrag: true,
    config: fixture.channels[0]!.config, frameCondition: { kind: 'current', label: '', detail: '' },
    commitMarker: async (value: MarkerMove) => { moves.push(value); return true },
    commitReferencePosition: async () => true,
    commitStimulusZoom: async () => true,
  })
  const pending = new Map<number, () => void>()
  let sequence = 0
  const view = await sfcHarness('src/components/diagrams/DiagramPane.vue', props, {
    window: { devicePixelRatio: 1 },
    requestAnimationFrame: (callback: () => void) => { pending.set(++sequence, callback); return sequence },
    cancelAnimationFrame: (id: number) => pending.delete(id),
  })
  const bindings = view.bindings as unknown as {
    plotArea: Ref<unknown>
    curves: Ref<SelectableCurve[]>
    xRange: Ref<{ min: number; max: number }>
    graphicalViewport: Ref<unknown>
    channelRows: Ref<{ channelId: number; range: unknown; zoomRange: unknown; active: boolean }[]>
    markerModels: Ref<{ name: string; result: { frequencyHz: number; value: number } }[]>
    plottedMarkers: Ref<{ name: string; placement: { left: number; top: number; tipX: number; tipY: number } }[]>
    markerPreviewPoints: (name: string) => { x: number; y: number; sampleIndex: number; result: { frequencyHz: number } }[]
    markerGestureActive: Ref<boolean>
    cancelDrag: () => void
  }
  bindings.plotArea.value = { getBoundingClientRect: () => ({ left: 0, top: 0, width: 700, height: 500 }) }
  const handles = () => findElements(view.render(), (node) => node.type === 'button' &&
    String(node.props?.['aria-label']).startsWith('M1 '))
  const handle = (name: string) => handles().find((node) => String(node.props!['aria-label']).startsWith(`M1 ${name},`))!.props!
  const target = { setPointerCapture() {}, hasPointerCapture: () => false, releasePointerCapture() {} }
  const pointer = (x: number, y: number, type = 'pointermove') => ({ type, pointerId: 1, button: 0,
    clientX: x, clientY: y, currentTarget: target, preventDefault() {}, stopPropagation() {} })
  const flush = () => { for (const [id, callback] of pending) { pending.delete(id); callback() } }
  const settle = async () => { await new Promise((resolve) => setImmediate(resolve)); await nextTick() }
  return { ...view, props, bindings, moves, handle, pointer, flush, settle,
    cleanup() { bindings.cancelDrag(); view.dispose() } }
}

test('overlay traces keep their own frequency axes, point counts, markers and channel footer when the active channel changes', async () => {
  const view = await multichannelPane()
  try {
    const curves = view.bindings.curves.value
    for (const [index, curve] of curves.entries()) {
      assert.equal(curve.kind, 'cartesian')
      if (curve.kind === 'cartesian') {
        assert.deepEqual(copy(curve.frequencyHz), view.props.channelFrames[index + 1]!.available
          ? copy((view.props.channelFrames[index + 1] as AcquiredFrame).frequencyHz) : [])
        assert.deepEqual(copy(curve.range), index ? { min: 1e9, max: 5e9 } : { min: 10e6, max: 30e6 })
        assert.equal(curve.values.length, index ? 5 : 3)
      }
    }
    assert.deepEqual(copy(view.bindings.markerModels.value.map((marker) => [marker.name, marker.result.frequencyHz, marker.result.value])),
      [['Reflect', 20e6, -60], ['Transmit', 2e9, -20]])
    const transmit = view.bindings.plottedMarkers.value.find((marker) => marker.name === 'Transmit')!
    assert.equal(transmit.placement.left + transmit.placement.tipX, 1 + 698 / 4)
    const end = { key: 'End', preventDefault() {}, stopPropagation() {} }
    view.handle('Transmit').onKeydown(end)
    view.handle('Reflect').onKeydown(end)
    assert.deepEqual(copy(view.emitted.filter((event) => event[0] === 'moveMarker').map((event) => event[1])),
      [{ name: 'Transmit', markerId: 1, sampleIndex: 4, discrete: true },
        { name: 'Reflect', markerId: 1, sampleIndex: 2, discrete: true }])
    const rows = () => findElements(view.render(), (node) => node.props?.['data-channel-id'] !== undefined)
    assert.equal(rows().length, 2)
    assert.match(JSON.stringify(rows()[0]), /10 MHz/)
    assert.match(JSON.stringify(rows()[1]), /5 GHz/)
    assert.match(JSON.stringify(rows()[1]), /2 kHz/)
    assert.match(JSON.stringify(rows()[1]), /-10 dBm/)
    rows()[1]!.props!.onClick()
    assert.deepEqual(view.emitted.at(-1), ['selectTrace', 'Transmit'])
    view.props.activeChannelId = 2
    view.props.frame = view.props.channelFrames[2] as AcquiredFrame // top-level active alias may change independently
    view.props.config = view.props.channels[1]!.config
    await nextTick()
    assert.deepEqual(copy(view.bindings.xRange.value), { min: 10e6, max: 30e6 })
    assert.deepEqual(copy(view.bindings.channelRows.value.map((row) => [row.channelId, row.active])), [[1, false], [2, true]])
    assert.deepEqual(copy(view.bindings.markerModels.value.map((marker) => marker.result.frequencyHz)), [20e6, 2e9])
  } finally { view.cleanup() }
})

test('Cartesian drawing and hit testing use per-curve ranges for disjoint acquisition bands', async () => {
  const view = await multichannelPane()
  const windowDescriptor = Object.getOwnPropertyDescriptor(globalThis, 'window')
  try {
    const curves = view.bindings.curves.value.filter((curve) => curve.kind === 'cartesian')
    assert.equal(hitTestTrace({ width: 700, height: 500, rect: { left: 1, top: 1, width: 698, height: 498 },
      xMin: 10e6, xMax: 30e6, curves, bounds: { left: 0, top: 0, width: 700, height: 500 } },
    { x: 350, y: 1 + 498 * 0.3 }), 'Transmit')
    const points: { color: unknown; x: number; y: number }[] = []
    const state: Record<string, unknown> = {}
    const context = new Proxy(state, { get(target, key: string) {
      if (key === 'moveTo' || key === 'lineTo') return (x: number, y: number) => points.push({ color: target.strokeStyle, x, y })
      return key in target ? target[key] : () => {}
    } })
    const canvas = { style: {}, width: 0, height: 0, getContext: () => context } as unknown as HTMLCanvasElement
    Object.defineProperty(globalThis, 'window', { configurable: true, value: { devicePixelRatio: 1 } })
    paintDiagram(canvas, { width: 700, height: 500, rect: { left: 1, top: 1, width: 698, height: 498 },
      xMin: 10e6, xMax: 30e6, references: [], curves }, { left: 0, top: 0, width: 700, height: 500 }, { grid: false })
    for (const curve of curves) {
      const rendered = points.filter((point) => point.color === curve.color)
      assert.equal(rendered[0]!.x, 1)
      assert.equal(rendered.at(-1)!.x, 699)
      assert.equal(rendered.length, curve.frequencyHz.length)
    }
  } finally {
    if (windowDescriptor) Object.defineProperty(globalThis, 'window', windowDescriptor)
    else Reflect.deleteProperty(globalThis, 'window')
    view.cleanup()
  }
})

test('marker drag on a secondary channel uses that channel points and frequency range', async () => {
  const view = await multichannelPane()
  try {
    const points = view.bindings.markerPreviewPoints('Transmit')
    assert.deepEqual(points.map((point) => point.result.frequencyHz), [1e9, 2e9, 3e9, 4e9, 5e9])
    view.handle('Transmit').onPointerdown(view.pointer(points[1]!.x, points[1]!.y, 'pointerdown'))
    view.handle('Transmit').onPointermove(view.pointer(points[4]!.x, points[4]!.y))
    view.handle('Transmit').onPointerup(view.pointer(points[4]!.x, points[4]!.y, 'pointerup'))
    await view.settle()
    assert.equal(view.moves.length, 1)
    assert.deepEqual(copy(view.moves[0]), { name: 'Transmit', markerId: 1, frequencyHz: 5e9, discrete: true })
  } finally { view.cleanup() }
})

test('an unavailable channel never borrows the primary channel samples or marker readings', async () => {
  const view = await multichannelPane()
  try {
    view.props.channelFrames[2] = { available: false, current: false, channelId: 2, reason: 'Waiting' }
    await nextTick()
    assert.deepEqual(copy(view.bindings.markerModels.value.map((marker) => marker.name)), ['Reflect'])
    assert.equal(view.bindings.markerPreviewPoints('Transmit').length, 0)
    assert.equal(view.bindings.curves.value.some((curve) => curve.name === 'Transmit'), false)
    assert.equal(view.bindings.channelRows.value.length, 2)
  } finally { view.cleanup() }
})

test('all four diagrams highlight their active channel row, including inactive diagrams, and legacy power stays absent', async () => {
  const panes = await Promise.all(Array.from({ length: 4 }, () => multichannelPane()))
  try {
    for (const [index, pane] of panes.entries()) {
      pane.props.number = index + 1
      pane.props.measurements = [pane.props.measurements[0]!]
      pane.props.selectedDiagram = 1
      pane.props.activeChannelId = 1
      await nextTick()
      assert.equal(pane.bindings.channelRows.value.length, 1)
      assert.equal(pane.bindings.channelRows.value[0]!.active, true, `Diagram ${index + 1} must highlight Ch1 independently of the active diagram`)
      const label = findElements(pane.render(), (node) => node.type === 'span' && String(node.props?.class).includes('channel-label'))[0]
      assert.ok(label && String(label.props!.class).split(' ').includes('active'))
    }
    const pane = panes[3]!
    Reflect.deleteProperty(pane.props.channels[0]!.config, 'powerDbm')
    await nextTick()
    assert.equal(findElements(pane.render(), (node) => node.props?.class === 'channel-power').length, 0)
  } finally { for (const pane of panes) pane.cleanup() }
})

test('graphical zoom composes the same normalized viewport on every channel frequency axis', async () => {
  const view = await multichannelPane()
  try {
    view.props.graphicalZoomSelecting = true
    const layer = findComponent(view.render(), 'PlotInteractionLayer')!.props!
    assert.equal(await layer['commit-zoom']({ traceName: 'Reflect', startHz: 15e6, stopHz: 25e6,
      min: -65, max: -15, revision: '1', displayRevision: '7' } satisfies StimulusZoomSelection), true)
    view.props.graphicalZoomSelecting = false
    await nextTick()
    assert.deepEqual(copy(view.bindings.curves.value.filter((curve) => curve.kind === 'cartesian').map((curve) => curve.range)),
      [{ min: 15e6, max: 25e6 }, { min: 2e9, max: 4e9 }])
    assert.deepEqual(copy(view.bindings.channelRows.value.map((row) => row.zoomRange)),
      [{ min: 15e6, max: 25e6 }, { min: 2e9, max: 4e9 }])
    view.props.channelFrames[2] = { ...(view.props.channelFrames[2] as AcquiredFrame), frameSeq: '222' }
    await nextTick()
    assert.ok(view.bindings.graphicalViewport.value, 'New sample frames must retain the local viewport')
    view.props.channelFrames[2] = { ...(view.props.channelFrames[2] as AcquiredFrame), revision: '99' }
    await nextTick()
    assert.equal(view.bindings.graphicalViewport.value, null, 'An acquisition identity change in any overlaid channel invalidates frozen geometry')
  } finally { view.cleanup() }
})

test('marker geometry survives ordinary channel frames but cancels when the secondary channel acquisition identity changes', async () => {
  const view = await multichannelPane()
  try {
    const points = view.bindings.markerPreviewPoints('Transmit')
    view.handle('Transmit').onPointerdown(view.pointer(points[1]!.x, points[1]!.y, 'pointerdown'))
    view.props.channelFrames[2] = { ...(view.props.channelFrames[2] as AcquiredFrame), frameSeq: '333' }
    await nextTick()
    assert.equal(view.bindings.markerGestureActive.value, true)
    view.props.channelFrames[2] = { ...(view.props.channelFrames[2] as AcquiredFrame), runId: '444' }
    await nextTick()
    assert.equal(view.bindings.markerGestureActive.value, false)
    assert.deepEqual(view.moves, [])
  } finally { view.cleanup() }
})

test('an independent Ch1 graphical viewport survives Ch2 CAS and frame changes, but resets for its own frequency or scale', async () => {
  const view = await multichannelPane()
  try {
    view.props.measurements = [view.props.measurements[0]!]
    const zoom = async () => {
      view.props.graphicalZoomSelecting = true
      const layer = findComponent(view.render(), 'PlotInteractionLayer')!.props!
      assert.equal(await layer['commit-zoom']({ traceName: 'Reflect', startHz: 15e6, stopHz: 25e6,
        min: -65, max: -15, revision: view.props.revision,
        displayRevision: view.props.displayRevision } satisfies StimulusZoomSelection), true)
      view.props.graphicalZoomSelecting = false
      await nextTick()
    }
    await zoom()
    const viewport = copy(view.bindings.graphicalViewport.value)
    view.props.revision = '2'
    view.props.displayRevision = '8'
    view.props.channels[1]!.acquisitionRevision = '99'
    view.props.channels[1]!.config.startHz = 2e9
    view.props.channelFrames[2] = { ...(view.props.channelFrames[2] as AcquiredFrame),
      revision: '99', measurementRevision: '2', displayRevision: '8', frequencyHz: [2e9, 3e9, 4e9] }
    // Ch1 接收共享显示/目录的新投影，但采集计划和频轴没有变化。
    view.props.channelFrames[1] = { ...(view.props.channelFrames[1] as AcquiredFrame),
      measurementRevision: '2', displayRevision: '8', frameSeq: '500' }
    await nextTick()
    assert.deepEqual(copy(view.bindings.graphicalViewport.value), viewport)
    assert.deepEqual(copy(view.bindings.xRange.value), { min: 15e6, max: 25e6 })
    view.props.selectedDiagram = 2
    view.props.activeChannelId = 2
    view.props.activeTraceName = 'Transmit'
    await nextTick()
    assert.deepEqual(copy(view.bindings.graphicalViewport.value), viewport, 'Selecting another diagram must retain this diagram viewport')
    view.props.selectedDiagram = 1
    view.props.channelFrames[1] = { ...(view.props.channelFrames[1] as AcquiredFrame), frequencyHz: [10e6, 30e6, 50e6] }
    await nextTick()
    assert.equal(view.bindings.graphicalViewport.value, null)
    await zoom()
    view.props.presentations.Reflect.scale.min = -80
    await nextTick()
    assert.equal(view.bindings.graphicalViewport.value, null)
  } finally { view.cleanup() }
})

test('workspace split panes and the shared marker table route through each primary trace channel', async () => {
  const fixture = channelsFixture()
  fixture.views.Reflect.markerInfoLocation = 'table'
  fixture.views.Transmit.markerInfoLocation = 'table'
  const props = reactive({ diagrams: fixture.measurements.map((measurement, index) => ({ number: index + 1,
    measurementNames: [measurement.name], primaryMeasurement: measurement.name })),
    frame: fixture.frames[1]!, channelFrames: { 1: fixture.frames[0]!, 2: fixture.frames[1]! },
    channels: fixture.channels, activeChannelId: 2, connected: true, measurements: fixture.measurements,
    presentations: fixture.views, activeTraceName: 'Transmit', selectedDiagram: 2, displayRevision: '7', revision: '1',
    layout: 'grid', gridColumns: 2, gridRows: 1, config: fixture.channels[1]!.config,
    frameCondition: { kind: 'current', label: '', detail: '' }, canEditDisplay: true,
    commitMarker: async () => true, infoTable: { visible: true, position: 'bottom' } })
  const view = await sfcHarness('src/components/diagrams/DiagramWorkspace.vue', props, {})
  try {
    const panes = findElements(view.render(), (node) => typeof node.type === 'object' && Reflect.get(node.type, 'name') === 'DiagramPane')
    assert.deepEqual(panes.map((pane) => pane.props!.frame.channelId), [1, 2])
    assert.deepEqual(panes.map((pane) => pane.props!.config.points), [3, 5])
    assert.deepEqual(panes.map((pane) => pane.props!['frame-condition'].kind), ['current', 'current'])
    const table = findComponent(view.render(), 'InfoTable')!.props!
    assert.deepEqual(copy(table.fields.map((field: { markers: { result: { frequencyHz: number } }[] }) => field.markers[0]!.result.frequencyHz)), [20e6, 2e9])
    props.channels[1]!.runId = '555'
    await nextTick()
    const updated = findElements(view.render(), (node) => typeof node.type === 'object' && Reflect.get(node.type, 'name') === 'DiagramPane')
    assert.equal(updated[0]!.props!['frame-condition'].kind, 'current')
    assert.equal(updated[1]!.props!['frame-condition'].kind, 'acquiring')
  } finally { view.dispose() }
})
