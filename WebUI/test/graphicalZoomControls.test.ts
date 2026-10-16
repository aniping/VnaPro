import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive, type VNode } from 'vue'
import type { useInstrument } from '../src/composables/useInstrument'
import { acquiredFrame, instrumentStatus, presentation } from './displayFixtures'
import { findComponent, findElements, sfcHarness } from './sfcHarness'

function control(render: () => VNode, id: string) {
  const node = findElements(render(), item => item.props?.['data-testid'] === `toolbar-${id}`)[0]
  assert.ok(node, `Missing ${id}`)
  return node.props!
}

test('toolbar Zoom Select and 1:1 reflect parent state and emit only their own enabled actions', async () => {
  const props = reactive({
    layout: 'single', hasTrace: true, canAutoscale: false, canEditDisplay: false,
    markerEnabled: false, canExport: true, canWrite: false, busy: false,
    canDelete: false, canAddTrace: false, canGraphicalZoom: false,
    graphicalZoomSelecting: false, canResetGraphicalZoom: false,
    graphicalZoomReason: 'Choose a current Cartesian frame',
  })
  const view = await sfcHarness('src/components/instrument/InstrumentToolbar.vue', props, {})
  try {
    assert.equal(control(view.render, 'zoom-select').disabled, true)
    assert.equal(control(view.render, 'zoom-reset').disabled, true)
    assert.equal(control(view.render, 'zoom-select').title, props.graphicalZoomReason)
    control(view.render, 'zoom-select').onClick()
    control(view.render, 'zoom-reset').onClick()
    assert.deepEqual(view.emitted, [])
    props.canGraphicalZoom = true
    assert.equal(control(view.render, 'zoom-select').disabled, false, 'Display-only zoom remains usable without the write lease')
    assert.equal(control(view.render, 'zoom-select')['aria-pressed'], false)
    control(view.render, 'zoom-select').onClick()
    props.graphicalZoomSelecting = true
    props.canResetGraphicalZoom = true
    assert.equal(control(view.render, 'zoom-select')['aria-pressed'], true)
    assert.equal(control(view.render, 'zoom-reset').disabled, false)
    control(view.render, 'zoom-select').onClick()
    control(view.render, 'zoom-reset').onClick()
    assert.deepEqual(view.emitted, [['chooseGraphicalZoom'], ['chooseGraphicalZoom'], ['resetGraphicalZoom']])
    props.canGraphicalZoom = false
    props.canResetGraphicalZoom = false
    control(view.render, 'zoom-select').onClick()
    control(view.render, 'zoom-reset').onClick()
    assert.equal(view.emitted.length, 3)
  } finally { view.dispose() }
})

async function graphicalApp() {
  const requests: string[] = []
  const view = await sfcHarness('src/App.vue', {}, {
    setTimeout, clearTimeout, AbortController,
    fetch: async (url: string) => {
      requests.push(url)
      assert.ok(url.endsWith('/control/claim'), `Graphical zoom cannot write to the service: ${url}`)
      return new Response(JSON.stringify({ sessionId: 'zoom-owner', hasControl: true, leaseSeconds: 30 }))
    },
  })
  const instrument = view.bindings.instrument as ReturnType<typeof useInstrument>
  instrument.status.value = instrumentStatus()
  instrument.frame.value = acquiredFrame()
  instrument.frameCurrent.value = true
  instrument.connected.value = true
  return {
    ...view, instrument, requests,
    toolbar: () => findComponent(view.render(), 'InstrumentToolbar')!.props!,
    workspace: () => findComponent(view.render(), 'DiagramWorkspace')!.props!,
    stimulus() {
      const host = findComponent(view.render(), 'SofttoolHost')!
      const slot = (host.children as { default: () => VNode[] }).default()
      return slot.map(node => findComponent(node, 'ZoomStimulusSofttool')).find(Boolean)!.props!
    },
    cleanup() { instrument.stop(); view.dispose() },
  }
}

test('App permits read-only graphical zoom only for a connected current visible Cartesian sweep', async () => {
  const app = await graphicalApp()
  try {
    assert.equal(app.instrument.canEditDisplay.value, false)
    assert.equal(app.toolbar()['can-graphical-zoom'], true)
    const savedStatus = app.instrument.status.value!
    const savedFrame = app.instrument.frame.value!
    app.instrument.frame.value = null
    assert.equal(app.toolbar()['can-graphical-zoom'], false)
    app.instrument.frame.value = savedFrame
    app.instrument.frameCurrent.value = false
    assert.equal(app.toolbar()['can-graphical-zoom'], false)
    app.instrument.frameCurrent.value = true
    app.instrument.connected.value = false
    assert.equal(app.toolbar()['can-graphical-zoom'], false)
    app.instrument.connected.value = true
    app.instrument.status.value = { ...savedStatus, pending: true }
    assert.equal(app.toolbar()['can-graphical-zoom'], false)
    app.instrument.status.value = {
      ...savedStatus, display: { ...savedStatus.display, presentations: { ...savedStatus.display.presentations, Reflect: { ...presentation(), visible: false } } },
    }
    assert.equal(app.toolbar()['can-graphical-zoom'], false)
    for (const format of ['smith', 'polar', 'invSmith'] as const) {
      app.instrument.status.value = {
        ...savedStatus, display: { ...savedStatus.display, presentations: { ...savedStatus.display.presentations, Reflect: presentation(format) } },
      }
      assert.equal(app.toolbar()['can-graphical-zoom'], false)
    }
    app.instrument.status.value = { ...savedStatus, config: { ...savedStatus.config, stopHz: savedStatus.config.startHz } }
    assert.equal(app.toolbar()['can-graphical-zoom'], false)
    app.instrument.status.value = savedStatus
    app.toolbar().onChooseGraphicalZoom()
    assert.equal(app.toolbar()['graphical-zoom-selecting'], true)
    assert.equal(app.workspace()['graphical-zoom-selecting'], true)
    app.toolbar().onChooseGraphicalZoom()
    assert.equal(app.toolbar()['graphical-zoom-selecting'], false)
    assert.deepEqual(app.requests, [])
  } finally { app.cleanup() }
})

test('App makes graphical and stimulus rectangle selection mutually exclusive without submitting settings', async () => {
  const app = await graphicalApp()
  try {
    app.instrument.status.value = { ...app.instrument.status.value!, stimulusZoom: { available: true, canZoomOut: false, reason: '', depth: 0 } }
    await app.instrument.claim()
    findComponent(app.render(), 'SofttoolHost')!.props!.onSelect('stimulus-zoom')
    await nextTick()
    assert.equal(app.stimulus()['can-zoom'], true)
    app.stimulus().choose()
    assert.equal(app.workspace()['zoom-selecting'], true)
    assert.equal(app.workspace()['graphical-zoom-selecting'], false)
    app.toolbar().onChooseGraphicalZoom()
    assert.equal(app.workspace()['zoom-selecting'], false)
    assert.equal(app.workspace()['graphical-zoom-selecting'], true)
    app.stimulus().choose()
    assert.equal(app.workspace()['zoom-selecting'], true)
    assert.equal(app.workspace()['graphical-zoom-selecting'], false)
    assert.equal(app.requests.length, 1, 'Choosing or cancelling a rectangle must not issue an instrument command')
  } finally { app.cleanup() }
})

test('App tracks zoom per diagram and resets only the selected diagram without an API write', async () => {
  const app = await graphicalApp()
  try {
    const initial = app.instrument.status.value!
    app.instrument.status.value = {
      ...initial,
      display: { ...initial.display, diagrams: [
        { number: 1, measurementNames: ['Reflect'], primaryMeasurement: 'Reflect', markerInfoVisible: true },
        { number: 2, measurementNames: ['Transmit'], primaryMeasurement: 'Transmit', markerInfoVisible: true },
      ] },
    }
    app.workspace().onGraphicalZoomChanged({ diagram: 1, zoomed: true })
    assert.equal(app.toolbar()['can-reset-graphical-zoom'], true)
    app.instrument.status.value = { ...app.instrument.status.value!, display: { ...app.instrument.status.value!.display, selectedDiagram: 2, selectedMeasurement: 'Transmit' } }
    await nextTick()
    assert.equal(app.toolbar()['can-reset-graphical-zoom'], false)
    app.workspace().onGraphicalZoomChanged({ diagram: 2, zoomed: true })
    app.toolbar().onResetGraphicalZoom()
    assert.deepEqual(JSON.parse(JSON.stringify(app.workspace()['graphical-zoom-resets'])), { 2: 1 })
    app.workspace().onGraphicalZoomChanged({ diagram: 2, zoomed: false })
    assert.equal(app.toolbar()['can-reset-graphical-zoom'], false)
    app.instrument.status.value = { ...app.instrument.status.value!, display: { ...app.instrument.status.value!.display, selectedDiagram: 1, selectedMeasurement: 'Reflect' } }
    await nextTick()
    assert.equal(app.toolbar()['can-reset-graphical-zoom'], true, 'Changing the active diagram must retain the other diagram viewport')
    app.toolbar().onResetGraphicalZoom()
    app.toolbar().onResetGraphicalZoom()
    assert.deepEqual(JSON.parse(JSON.stringify(app.workspace()['graphical-zoom-resets'])), { 1: 2, 2: 1 })
    assert.deepEqual(app.requests, [])
  } finally { app.cleanup() }
})

test('Workspace delivers independent reset generations to every pane and selection only to the active pane', async () => {
  const status = instrumentStatus()
  const props = reactive({
    ...status.display,
    diagrams: [
      { number: 1, measurementNames: ['Reflect'], primaryMeasurement: 'Reflect', markerInfoVisible: true },
      { number: 2, measurementNames: ['Transmit'], primaryMeasurement: 'Transmit', markerInfoVisible: true },
    ],
    layout: 'grid', frame: acquiredFrame(), measurements: status.measurements,
    activeTraceName: 'Reflect', selectedDiagram: 1, config: status.config,
    frameCondition: { kind: 'current', label: '', detail: '' },
    canEditDisplay: false, commitMarker: async () => false, commitReferencePosition: async () => false,
    graphicalZoomSelecting: true, graphicalZoomResets: { 1: 2, 2: 4 },
  })
  const view = await sfcHarness('src/components/diagrams/DiagramWorkspace.vue', props, {})
  const panes = () => findElements(view.render(), node => typeof node.type === 'object' && Reflect.get(node.type, 'name') === 'DiagramPane').map(node => node.props!)
  try {
    assert.deepEqual(panes().map(pane => [pane.number, pane['graphical-zoom-selecting'], pane['graphical-zoom-reset']]), [[1, true, 2], [2, false, 4]])
    props.selectedDiagram = 2
    props.graphicalZoomResets = { 1: 2, 2: 5 }
    assert.deepEqual(panes().map(pane => [pane.number, pane['graphical-zoom-selecting'], pane['graphical-zoom-reset']]), [[1, false, 2], [2, true, 5]])
    panes()[1]!.onGraphicalZoomChanged({ diagram: 2, zoomed: false })
    panes()[1]!.onCancelGraphicalZoom()
    assert.deepEqual(JSON.parse(JSON.stringify(view.emitted)), [['graphicalZoomChanged', { diagram: 2, zoomed: false }], ['cancelGraphicalZoom']])
  } finally { view.dispose() }
})
