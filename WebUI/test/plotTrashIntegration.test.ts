import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive, type Ref } from 'vue'
import type { useInstrument } from '../src/composables/useInstrument'
import type { TrashIntent } from '../src/domain/trashDrop'
import type { MarkerMove, MarkerPreviewPoint } from '../src/plot/markerDrag'
import { acquiredFrame, instrumentStatus, markerDefinitions, markerProjection, markerProjections, presentation, receiverTrace } from './displayFixtures'
import { findComponent, findElements, sfcHarness } from './sfcHarness'

async function markerPane(options: {
  multiple?: boolean
  overlapping?: boolean
  commitTrash?: (intent: TrashIntent) => Promise<boolean>
} = {}) {
  const trace = receiverTrace(undefined, [0.2, 0.4, 0.6])
  trace.projection.marker = markerProjection({
    available: true, valid: true, frequencyHz: 10e6, sampleIndex: 0,
    real: 0.2, imag: 0, value: -13.5, valueState: 'finite', reason: '',
  })
  const view = presentation()
  view.marker = { enabled: true, frequencyHz: 10e6, sampleIndex: 0, discrete: true }
  if (options.multiple) {
    view.activeMarkerId = 1
    view.markers = markerDefinitions()
    trace.projection.activeMarkerId = 1
    trace.projection.markers = markerProjections()
    for (let index = 0; index < 3; index++) {
      const sampleIndex = options.overlapping ? 0 : index
      const frequencyHz = (sampleIndex + 1) * 10e6
      view.markers[index] = { id: index + 1, enabled: true, frequencyHz, sampleIndex, discrete: true }
      trace.projection.markers[index] = {
        id: index + 1, ...trace.projection.marker, sampleIndex, frequencyHz,
        real: 0.2 * (sampleIndex + 1),
      }
    }
  }
  const moves: MarkerMove[] = []
  const trash: TrashIntent[] = []
  let targetEnabled = true
  const props = reactive({
    number: 1, layout: 'single', frame: acquiredFrame(trace),
    measurements: [trace], allMeasurements: [trace], presentations: { Reflect: view },
    activeTraceName: 'Reflect', primaryTraceName: 'Reflect',
    config: { startHz: 10e6, stopHz: 30e6, points: 3, ifBandwidthHz: 1000 },
    frameCondition: { kind: 'current', label: '', detail: '' },
    displayRevision: '17', canEditDisplay: true,
    commitMarker: async (move: MarkerMove) => { moves.push(move); return true },
    commitTrash: async (intent: TrashIntent) => {
      trash.push(intent)
      return options.commitTrash ? options.commitTrash(intent) : false
    },
  })
  const harness = await sfcHarness('src/components/diagrams/DiagramPane.vue', props, {
    window: { devicePixelRatio: 1 },
    requestAnimationFrame: () => 1, cancelAnimationFrame() {},
    document: {
      querySelector: () => ({
        getAttribute: () => String(targetEnabled),
        getBoundingClientRect: () => ({ left: 800, top: 20, right: 840, bottom: 60, width: 40, height: 40 }),
      }),
    },
  })
  const bindings = harness.bindings as unknown as {
    plotArea: Ref<unknown>
    plottedMarkers: Ref<{ markerId: number; placement: { left: number; top: number; tipX: number; tipY: number } }[]>
    markerPreviewPoints: (name: string) => MarkerPreviewPoint[]
    cancelDrag: () => void
  }
  bindings.plotArea.value = { getBoundingClientRect: () => ({ left: 0, top: 0, width: 700, height: 500 }) }
  const target = { setPointerCapture() {}, hasPointerCapture: () => false, releasePointerCapture() {} }
  const pointer = (x: number, y: number, type = 'pointermove') => ({
    type, button: 0, pointerId: 1, clientX: x, clientY: y,
    currentTarget: target, preventDefault() {}, stopPropagation() {},
  })
  const handle = (markerId = 1) => {
    const node = findElements(harness.render(), item => item.type === 'button' &&
      String(item.props?.class).includes('marker-handle') &&
      String(item.props?.['aria-label']).startsWith(`M${markerId} Reflect,`))[0]
    assert.ok(node, 'The saved marker must expose a public pointer handle')
    return node.props!
  }
  return {
    ...harness, props, moves, trash, pointer, handle,
    begin(markerId = 1) {
      const p = bindings.plottedMarkers.value.find(marker => marker.markerId === markerId)!.placement
      handle(markerId).onPointerdown(pointer(p.left + p.tipX, p.top + p.tipY, 'pointerdown'))
    },
    curvePoint(index: number) { return bindings.markerPreviewPoints('Reflect')[index]! },
    disableTrash() { targetEnabled = false },
    cleanup() { bindings.cancelDrag(); harness.dispose() },
  }
}

test('dragging a marker on the last trace to the toolbar submits only its frozen disable intent', async () => {
  const pane = await markerPane()
  try {
    const before = JSON.stringify(pane.props.presentations)
    pane.begin()
    pane.handle().onPointermove(pane.pointer(690, 150))
    pane.handle().onPointerup(pane.pointer(820, 40, 'pointerup'))
    pane.handle().onPointerup(pane.pointer(820, 40, 'pointerup'))
    await new Promise(resolve => setImmediate(resolve))
    assert.deepEqual(JSON.parse(JSON.stringify(pane.trash)), [{ kind: 'marker', name: 'Reflect', markerId: 1, displayRevision: '17' }])
    assert.deepEqual(pane.moves, [], 'The same release must never move the marker frequency')
    assert.equal(JSON.stringify(pane.props.presentations), before, 'Even a refused delete cannot locally erase saved marker properties')
    assert.deepEqual(pane.emitted.filter(event => event[0] === 'selectMarker'), [])
  } finally { pane.cleanup() }
})

test('a changed display revision cancels marker trash dragging instead of reusing the obsolete source snapshot', async () => {
  const pane = await markerPane()
  try {
    pane.begin()
    pane.handle().onPointermove(pane.pointer(820, 40))
    pane.props.displayRevision = '18'
    await nextTick()
    pane.handle().onPointerup(pane.pointer(820, 40, 'pointerup'))
    await new Promise(resolve => setImmediate(resolve))
    assert.deepEqual(pane.trash, [])
    assert.deepEqual(pane.moves, [])
  } finally { pane.cleanup() }
})

test('ordinary marker release outside the trash keeps the frequency move path', async () => {
  const pane = await markerPane()
  try {
    pane.begin()
    const point = pane.curvePoint(2)
    pane.handle().onPointermove(pane.pointer(point.x, point.y))
    pane.handle().onPointerup(pane.pointer(point.x, point.y, 'pointerup'))
    await new Promise(resolve => setImmediate(resolve))
    assert.deepEqual(pane.trash, [])
    assert.equal(pane.moves.length, 1)
    assert.equal(pane.moves[0]!.name, 'Reflect')
    assert.equal(pane.moves[0]!.markerId, 1)
    assert.equal(pane.moves[0]!.frequencyHz, 30e6)
  } finally { pane.cleanup() }
})

test('pointer cancellation and a detached drop on a disabled trash target cannot delete or move a marker', async () => {
  const pane = await markerPane()
  try {
    pane.begin()
    pane.handle().onPointermove(pane.pointer(820, 40))
    pane.handle().onPointercancel(pane.pointer(820, 40, 'pointercancel'))
    assert.deepEqual(pane.trash, [])
    assert.deepEqual(pane.moves, [])
    pane.disableTrash()
    pane.begin()
    pane.handle().onPointermove(pane.pointer(820, 40))
    pane.handle().onPointerup(pane.pointer(820, 40, 'pointerup'))
    await new Promise(resolve => setImmediate(resolve))
    assert.deepEqual(pane.trash, [])
    assert.deepEqual(pane.moves, [], 'A detached invalid drop must not snap back into a frequency move')
  } finally { pane.cleanup() }
})

test('a detached marker released outside any trash target cancels without changing its saved frequency', async () => {
  const pane = await markerPane()
  try {
    const before = JSON.stringify(pane.props.presentations)
    pane.begin()
    pane.handle().onPointermove(pane.pointer(450, -40))
    pane.handle().onPointerup(pane.pointer(450, -40, 'pointerup'))
    await new Promise(resolve => setImmediate(resolve))
    assert.deepEqual(pane.trash, [])
    assert.deepEqual(pane.moves, [])
    assert.equal(JSON.stringify(pane.props.presentations), before)
  } finally { pane.cleanup() }
})

async function trashApp() {
  const requests: { path: string; method?: string; body: Record<string, unknown> }[] = []
  const harness = await sfcHarness('src/App.vue', {}, {
    setTimeout, clearTimeout, AbortController,
    fetch: async (url: string, init?: RequestInit) => {
      if (url.endsWith('/control/claim')) return new Response(JSON.stringify({ sessionId: 'trash-owner', hasControl: true, leaseSeconds: 30 }))
      requests.push({ path: url, method: init?.method, body: JSON.parse(String(init?.body)) })
      return new Response(JSON.stringify({ error: { code: 'Conflict', message: 'No saved state changed' } }), { status: 409 })
    },
  })
  const instrument = harness.bindings.instrument as ReturnType<typeof useInstrument>
  instrument.status.value = instrumentStatus()
  instrument.connected.value = true
  return {
    ...harness, instrument, requests,
    drop: (intent: TrashIntent) => findComponent(harness.render(), 'InstrumentToolbar')!.props!.onTrashDrop(intent) as Promise<boolean>,
    pointerTrash: (intent: TrashIntent) => findComponent(harness.render(), 'DiagramWorkspace')!.props!['commit-trash'](intent) as Promise<boolean>,
    cleanup() { instrument.stop(); harness.dispose() },
  }
}

test('M1 M2 M3 enabled: the real M2 pointer handle and App dispatch disable only M2, even if selection changes during the drag', async () => {
  const app = await trashApp()
  const pane = await markerPane({ multiple: true, commitTrash: app.pointerTrash })
  try {
    const status = instrumentStatus([pane.props.measurements[0]!])
    status.display.revision = '17'
    status.display.presentations.Reflect = pane.props.presentations.Reflect
    app.instrument.status.value = status
    assert.equal(await app.instrument.claim(), true)
    assert.deepEqual(pane.props.presentations.Reflect.markers!.slice(0, 3).map(marker => marker.enabled), [true, true, true])
    const before = JSON.stringify(pane.props.presentations.Reflect.markers)
    assert.notEqual(pane.handle(2).draggable, true, 'A plot symbol must not start the marker-info native drag')
    assert.equal(pane.handle(2).onDragstart, undefined)
    pane.begin(2)
    pane.handle(2).onPointermove(pane.pointer(820, 40))
    pane.props.presentations.Reflect.activeMarkerId = 3
    pane.props.frame.traces[0]!.projection.activeMarkerId = 3
    await nextTick()
    pane.handle(2).onPointerup(pane.pointer(820, 40, 'pointerup'))
    pane.handle(2).onPointerup(pane.pointer(820, 40, 'pointerup'))
    await new Promise(resolve => setImmediate(resolve))
    assert.deepEqual(JSON.parse(JSON.stringify(pane.trash)), [{ kind: 'marker', name: 'Reflect', markerId: 2, displayRevision: '17' }])
    assert.deepEqual(pane.moves, [])
    assert.equal(app.requests.length, 1)
    const { requestId, ...body } = app.requests[0]!.body
    assert.equal(typeof requestId, 'string')
    assert.ok(app.requests[0]!.path.endsWith('/display'))
    assert.deepEqual(body, { action: 'marker', traceName: 'Reflect', markerId: 2, enabled: false, expectedDisplayRevision: '17' })
    assert.equal(JSON.stringify(pane.props.presentations.Reflect.markers), before, 'A rejected single-marker request must not clear any saved marker locally')
    assert.equal(pane.props.presentations.Reflect.activeMarkerId, 3)
  } finally { pane.cleanup(); app.cleanup() }
})

test('overlapping marker handles retain their pointerdown source identity instead of the active marker identity', async () => {
  const pane = await markerPane({ multiple: true, overlapping: true })
  try {
    assert.ok(pane.handle(1).style.zIndex > pane.handle(2).style.zIndex, 'The active marker owns the frontmost overlapping hit target')
    pane.begin(2)
    pane.handle(2).onPointermove(pane.pointer(820, 40))
    pane.handle(2).onPointerup(pane.pointer(820, 40, 'pointerup'))
    await new Promise(resolve => setImmediate(resolve))
    assert.deepEqual(JSON.parse(JSON.stringify(pane.trash)), [{ kind: 'marker', name: 'Reflect', markerId: 2, displayRevision: '17' }])
    assert.deepEqual(pane.moves, [])
  } finally { pane.cleanup() }
})

test('App deletes the named inactive trace without selecting it and rejects stale drag versions', async () => {
  const app = await trashApp()
  try {
    const intent: TrashIntent = { kind: 'trace', name: 'Transmit', revision: '1', displayRevision: '1' }
    assert.equal(await app.drop(intent), false)
    assert.equal(app.requests.length, 0)
    assert.equal(await app.instrument.claim(), true)
    for (const stale of [{ ...intent, revision: '0' }, { ...intent, displayRevision: '0' }, { ...intent, name: 'Missing' }])
      assert.equal(await app.drop(stale), false)
    app.instrument.writeInFlight.value = true
    assert.equal(await app.drop(intent), false)
    app.instrument.writeInFlight.value = false
    assert.equal(app.requests.length, 0)
    const before = JSON.stringify(app.instrument.status.value)
    assert.equal(await app.drop(intent), false)
    assert.equal(app.requests.length, 1)
    assert.ok(app.requests[0]!.path.endsWith('/measurements/Transmit'))
    assert.equal(app.requests[0]!.method, 'DELETE')
    const { requestId, ...body } = app.requests[0]!.body
    assert.equal(typeof requestId, 'string')
    assert.deepEqual(body, { expectedRevision: '1' })
    assert.equal(JSON.stringify(app.instrument.status.value), before)
  } finally { app.cleanup() }
})

test('App protects the last trace and diagram while marker trash still disables saved markers', async () => {
  const app = await trashApp()
  try {
    app.instrument.status.value = instrumentStatus([app.instrument.status.value!.measurements[0]!])
    assert.equal(await app.instrument.claim(), true)
    await app.drop({ kind: 'trace', name: 'Reflect', revision: '1', displayRevision: '1' })
    await app.drop({ kind: 'diagram', diagram: 1, revision: '1', displayRevision: '1' })
    assert.equal(app.requests.length, 0)
    await app.drop({ kind: 'marker', name: 'Reflect', markerId: 2, displayRevision: '1' })
    await app.drop({ kind: 'markers', name: 'Reflect', displayRevision: '1' })
    assert.equal(app.requests.length, 2)
    for (const request of app.requests) assert.ok(request.path.endsWith('/display'))
    assert.deepEqual(app.requests.map(({ body: { requestId, ...body } }) => body), [
      { action: 'marker', traceName: 'Reflect', markerId: 2, enabled: false, expectedDisplayRevision: '1' },
      { action: 'marker', traceName: 'Reflect', markerAction: 'allOff', expectedDisplayRevision: '1' },
    ])
  } finally { app.cleanup() }
})

test('App diagram trash forwards both frozen revisions to the diagram delete operation', async () => {
  const app = await trashApp()
  try {
    app.instrument.status.value!.display.diagrams.push({ number: 2, measurementNames: ['Transmit'], primaryMeasurement: 'Transmit', markerInfoVisible: true })
    assert.equal(await app.instrument.claim(), true)
    await app.drop({ kind: 'diagram', diagram: 2, revision: '1', displayRevision: '1' })
    assert.equal(app.requests.length, 1)
    assert.ok(app.requests[0]!.path.endsWith('/measurement-actions/delete-diagram'))
    assert.equal(app.requests[0]!.method, 'POST')
    const { requestId, ...body } = app.requests[0]!.body
    assert.equal(typeof requestId, 'string')
    assert.deepEqual(body, { diagram: 2, expectedRevision: '1', expectedDisplayRevision: '1' })
  } finally { app.cleanup() }
})
