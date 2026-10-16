import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive, type Ref } from 'vue'
import type { StimulusZoomSelection } from '../src/api/types'
import type { DisplayScale } from '../src/domain/presentation'
import type { GraphicalZoomViewport } from '../src/plot/graphicalZoom'
import type { SelectableCurve } from '../src/plot/traceHitTest'
import { acquiredFrame, instrumentStatus, markerProjection, presentation, projection, receiverTrace, reflection, transmission } from './displayFixtures'
import { findComponent, findElements, sfcHarness } from './sfcHarness'

const copy = (value: unknown) => JSON.parse(JSON.stringify(value))

async function diagram() {
  const reflect = receiverTrace(reflection, [0.1, 0.2, 0.3, 0.4, 0.5])
  reflect.projection = projection([-80, -40, -20, 0, 20])
  reflect.projection.marker = markerProjection({ available: true, valid: true, frequencyHz: 30e6,
    sampleIndex: 2, value: -40, valueState: 'finite' })
  const transmit = receiverTrace(transmission, [0.1, 0.2, 0.3, 0.4, 0.5])
  transmit.projection = projection([-150, -60, 0, 60, 150], 'phase')
  const frame = acquiredFrame(reflect)
  frame.frequencyHz = [10e6, 20e6, 30e6, 40e6, 50e6]
  frame.traces = [reflect, transmit]
  const views = { Reflect: presentation(), Transmit: presentation('phase') }
  views.Reflect.marker.enabled = true
  views.Reflect.marker.frequencyHz = 30e6
  views.Transmit.scale = { min: -180, max: 180, referencePosition: 5 }
  const writes: unknown[] = []
  const props = reactive({
    number: 1, layout: 'single', frame,
    measurements: [reflection, transmission], allMeasurements: [reflection, transmission],
    presentations: views, activeTraceName: 'Reflect', primaryTraceName: 'Reflect', selectedDiagram: 1,
    displayRevision: '7', revision: '1', graphicalZoomSelecting: true, graphicalZoomReset: 0,
    zoomSelecting: false, canZoom: true,
    config: instrumentStatus().config, frameCondition: { kind: 'current', label: '', detail: '' },
    canEditDisplay: false, canContinueReferenceDrag: false,
    commitStimulusZoom: async (value: unknown) => { writes.push(value); return true },
    commitTracePan: async (value: unknown) => { writes.push(value); return true },
    commitMarker: async () => true, commitReferencePosition: async () => true,
  })
  const view = await sfcHarness('src/components/diagrams/DiagramPane.vue', props, {
    window: { devicePixelRatio: 1 }, requestAnimationFrame: () => 1, cancelAnimationFrame() {},
  })
  const bindings = view.bindings as unknown as {
    graphicalViewport: Ref<GraphicalZoomViewport | null>
    xRange: Ref<{ min: number; max: number }>
    axisScale: Ref<DisplayScale>
    curves: Ref<SelectableCurve[]>
    rect: Ref<{ left: number; top: number; width: number; height: number }>
    plottedMarkers: Ref<{ placement: { left: number; top: number; tipX: number; tipY: number } }[]>
    markerPreviewPoints: (name: string) => { x: number; y: number; sampleIndex: number }[]
  }
  const layer = () => findComponent(view.render(), 'PlotInteractionLayer')!.props!
  const selection = (overrides: Partial<StimulusZoomSelection> = {}): StimulusZoomSelection => ({
    traceName: 'Reflect', startHz: 20e6, stopHz: 40e6, min: -65, max: -15,
    revision: props.revision, displayRevision: props.displayRevision, ...overrides,
  })
  async function zoom(overrides: Partial<StimulusZoomSelection> = {}) {
    props.graphicalZoomSelecting = true
    assert.equal(await layer()['commit-zoom'](selection(overrides)), true)
    props.graphicalZoomSelecting = false
    await nextTick()
  }
  return { ...view, props, bindings, layer, selection, zoom, writes }
}

test('read-only graphical zoom changes every Cartesian display scale without changing titles, samples or server state', async () => {
  const view = await diagram()
  try {
    assert.equal(view.layer()['can-zoom'], true)
    assert.equal(view.layer()['can-continue-gesture'], true)
    const before = JSON.stringify([view.props.presentations, view.props.config, view.props.frame])
    const source = view.bindings.curves.value.find((curve) => curve.name === 'Reflect')!
    await view.zoom()
    assert.deepEqual(copy(view.bindings.xRange.value), { min: 20e6, max: 40e6 })
    assert.deepEqual(copy(view.bindings.axisScale.value), { min: -65, max: -15, referencePosition: 9 })
    const curve = view.bindings.curves.value.find((item) => item.name === 'Reflect')!
    assert.equal(curve.kind, 'cartesian')
    assert.equal(source.kind, 'cartesian')
    if (curve.kind === 'cartesian' && source.kind === 'cartesian') {
      assert.equal(curve.values, source.values)
      assert.equal(curve.frequencyHz, source.frequencyHz)
    }
    const phase = view.bindings.curves.value.find((item) => item.name === 'Transmit')!
    assert.equal(phase.kind, 'cartesian')
    if (phase.kind === 'cartesian') assert.deepEqual(copy(phase.scale), { min: -90, max: 90, referencePosition: 5 })
    const strips = findElements(view.render(), (node) => typeof node.type === 'object' && Reflect.get(node.type, 'name') === 'TraceStrip')
    assert.deepEqual(copy(strips[0]!.props!.scale), copy(view.props.presentations.Reflect.scale))
    assert.equal(findElements(view.render(), (node) => typeof node.type === 'object' && Reflect.get(node.type, 'name') === 'ReferencePositionHandle').length, 0)
    assert.ok(findElements(view.render(), (node) => node.props?.['data-testid'] === 'graphical-zoom-line').length)
    assert.equal(JSON.stringify([view.props.presentations, view.props.config, view.props.frame]), before)
    assert.deepEqual(view.writes, [])
    assert.ok(view.emitted.some((event) => event[0] === 'graphicalZoomChanged' && copy(event[1]).zoomed === true))
    assert.ok(view.emitted.some((event) => event[0] === 'cancelGraphicalZoom'))
  } finally { view.dispose() }
})

test('nested graphical selection composes in original coordinates and 1:1 reset restores the whole plot', async () => {
  const view = await diagram()
  try {
    await view.zoom()
    await view.zoom({ startHz: 25e6, stopHz: 35e6, min: -52.5, max: -27.5 })
    assert.deepEqual(copy(view.bindings.graphicalViewport.value), { left: 0.375, top: 0.375, width: 0.25, height: 0.25 })
    assert.deepEqual(copy(view.bindings.xRange.value), { min: 25e6, max: 35e6 })
    view.props.graphicalZoomReset++
    await nextTick()
    assert.equal(view.bindings.graphicalViewport.value, null)
    assert.deepEqual(copy(view.bindings.xRange.value), { min: 10e6, max: 50e6 })
    assert.deepEqual(copy(view.bindings.axisScale.value), copy(view.props.presentations.Reflect.scale))
    assert.equal(findElements(view.render(), (node) => node.props?.['data-testid'] === 'graphical-zoom-line').length, 0)
  } finally { view.dispose() }
})

test('new frame sequence retains a graphical viewport but authoritative geometry changes clear it', async () => {
  const view = await diagram()
  try {
    await view.zoom()
    const viewport = copy(view.bindings.graphicalViewport.value)
    view.props.frame = { ...view.props.frame, frameSeq: '200', traces: [...view.props.frame.traces] }
    await nextTick()
    assert.deepEqual(copy(view.bindings.graphicalViewport.value), viewport)
    for (const change of [
      () => { view.props.presentations.Reflect.scale = { min: -80, max: 20, referencePosition: 9 } },
      () => { view.props.primaryTraceName = 'Transmit' },
    ]) {
      change()
      await nextTick()
      assert.equal(view.bindings.graphicalViewport.value, null)
      view.props.primaryTraceName = 'Reflect'
      view.props.presentations.Reflect.scale = { min: -90, max: 10, referencePosition: 9 }
      await nextTick()
      await view.zoom()
    }
    view.props.frame = { ...view.props.frame, revision: '2' }
    await nextTick()
    assert.equal(view.bindings.graphicalViewport.value, null)
  } finally { view.dispose() }
})

test('vertical pan inside graphical zoom moves the local viewport for all traces and never calls persistent pan', async () => {
  const view = await diagram()
  try {
    await view.zoom()
    assert.equal(view.layer()['can-pan'], true)
    view.layer().onPreview({ min: -55, max: -5, referencePosition: 9 })
    assert.deepEqual(copy(view.bindings.axisScale.value), { min: -55, max: -5, referencePosition: 9 })
    assert.deepEqual(copy(view.layer().scale), { min: -65, max: -15, referencePosition: 9 }, 'Preview must not reset its own gesture input')
    assert.equal(await view.layer()['commit-pan']({ name: 'Reflect', reference: -10, displayRevision: '7' }), true)
    view.layer().onPreview(null)
    assert.deepEqual(copy(view.bindings.xRange.value), { min: 20e6, max: 40e6 })
    assert.deepEqual(copy(view.bindings.axisScale.value), { min: -55, max: -5, referencePosition: 9 })
    assert.deepEqual(view.writes, [])
  } finally { view.dispose() }
})

test('markers and drag previews use the zoomed frequency and response axes without clamping invisible markers to plot edges', async () => {
  const view = await diagram()
  try {
    await view.zoom()
    const marker = view.bindings.plottedMarkers.value[0]!
    const rect = view.bindings.rect.value
    assert.equal(marker.placement.left + marker.placement.tipX, rect.left + rect.width / 2)
    assert.equal(marker.placement.top + marker.placement.tipY, rect.top + rect.height / 2)
    const points = view.bindings.markerPreviewPoints('Reflect')
    assert.deepEqual(points.map((point) => point.sampleIndex), [1, 2])
    view.props.frame.traces[0]!.projection.marker.frequencyHz = 50e6
    assert.equal(view.bindings.plottedMarkers.value.length, 0)
  } finally { view.dispose() }
})

test('graphical viewport blocks Stimulus Zoom while reset preserves the existing persistent pan route', async () => {
  const view = await diagram()
  try {
    await view.zoom()
    view.props.canEditDisplay = true
    view.props.canContinueReferenceDrag = true
    view.props.zoomSelecting = true
    assert.equal(view.layer()['can-zoom'], false)
    assert.equal(await view.layer()['commit-zoom'](view.selection()), false)
    assert.deepEqual(view.writes, [])
    view.props.zoomSelecting = false
    view.props.graphicalZoomReset++
    await nextTick()
    assert.equal(await view.layer()['commit-pan']({ name: 'Reflect', reference: 5, displayRevision: '7' }), true)
    assert.equal(view.writes.length, 1)
  } finally { view.dispose() }
})

test('inactive reset signals do not reset another viewport and scope disposal clears the diagram zoom indicator', async () => {
  const view = await diagram()
  await view.zoom()
  view.props.selectedDiagram = 2
  view.props.graphicalZoomReset++
  await nextTick()
  assert.ok(view.bindings.graphicalViewport.value)
  view.dispose()
  assert.deepEqual(copy(view.emitted.at(-1)), ['graphicalZoomChanged', { diagram: 1, zoomed: false }])
})
