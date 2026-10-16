import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive, type Ref } from 'vue'
import type { MarkerMove } from '../src/plot/markerDrag'
import type { TrashIntent } from '../src/domain/trashDrop'
import { acquiredFrame, markerProjection, presentation, receiverTrace } from './displayFixtures'
import { findComponent, findElements, sfcHarness } from './sfcHarness'

async function markerPane() {
  const trace = receiverTrace(undefined, [0.2, 0.4, 0.6])
  trace.projection.marker = markerProjection({ available: true, valid: true, frequencyHz: 20e6,
    sampleIndex: 1, real: 0.4, imag: 0, value: -13.5, valueState: 'finite', reason: '' })
  const presentationValue = presentation()
  presentationValue.marker = { enabled: true, frequencyHz: 20e6, sampleIndex: 1, discrete: true }
  const moves: MarkerMove[] = []
  const trash: TrashIntent[] = []
  const bounds = { left: 100, top: 150, width: 350, height: 1000 }
  let trashEnabled = true
  const props = reactive({
    number: 1, layout: 'single', frame: acquiredFrame(trace),
    measurements: [trace], allMeasurements: [trace], presentations: { Reflect: presentationValue },
    activeTraceName: 'Reflect', primaryTraceName: 'Reflect', revision: '1', displayRevision: '17',
    graphicalZoomSelecting: false, zoomSelecting: false,
    config: { startHz: 10e6, stopHz: 30e6, points: 3, ifBandwidthHz: 1000 },
    frameCondition: { kind: 'current', label: '', detail: '' }, canEditDisplay: true,
    commitMarker: async (move: MarkerMove) => { moves.push(move); return true },
    commitTrash: async (intent: TrashIntent) => { trash.push(intent); return true },
  })
  const frames = new Map<number, () => void>()
  let sequence = 0
  const view = await sfcHarness('src/components/diagrams/DiagramPane.vue', props, {
    window: { devicePixelRatio: 1 },
    requestAnimationFrame: (callback: () => void) => { frames.set(++sequence, callback); return sequence },
    cancelAnimationFrame: (id: number) => frames.delete(id),
    document: { querySelector: () => ({
      getAttribute: () => String(trashEnabled),
      getBoundingClientRect: () => ({ left: 600, top: 20, width: 40, height: 40, right: 640, bottom: 60 }),
    }) },
  })
  const bindings = view.bindings as unknown as {
    plotArea: Ref<unknown>
    plottedMarkers: Ref<{ placement: { left: number; top: number; tipX: number; tipY: number } }[]>
    dragPreview: Ref<{ placement: { left: number; top: number; tipX: number; tipY: number } } | null>
    cancelDrag: () => void
    cancelDragKey: (event: { key: string }) => void
    resizePlot: () => void
  }
  bindings.plotArea.value = { getBoundingClientRect: () => ({ ...bounds }) }
  const target = { setPointerCapture() {}, hasPointerCapture: () => false, releasePointerCapture() {} }
  const pointer = (x: number, y: number, type = 'pointermove') => ({
    type, button: 0, pointerId: 1, clientX: x, clientY: y, currentTarget: target,
    preventDefault() {}, stopPropagation() {},
  })
  const handle = () => findElements(view.render(), (node) => node.type === 'button' &&
    String(node.props?.['aria-label']).startsWith('M1 Reflect,'))[0]!.props!
  const floating = () => findElements(view.render(), (node) => node.props?.['data-marker-drag'] === 'detached')[0]
  const highlight = () => findElements(view.render(), (node) => node.props?.['data-testid'] === 'marker-trash-highlight')[0]
  const flush = () => { for (const [id, callback] of frames) { frames.delete(id); callback() } }
  const tip = () => {
    const p = bindings.plottedMarkers.value[0]!.placement
    return { x: bounds.left + (p.left + p.tipX) * bounds.width / 700,
      y: bounds.top + (p.top + p.tipY) * bounds.height / 500 }
  }
  function visualTip() {
    const ghost = floating()
    if (ghost) return { x: Number.parseFloat(ghost.props!.style.left), y: Number.parseFloat(ghost.props!.style.top) }
    const p = bindings.dragPreview.value!.placement
    return { x: bounds.left + (p.left + p.tipX) * bounds.width / 700,
      y: bounds.top + (p.top + p.tipY) * bounds.height / 500 }
  }
  const settle = async () => { await new Promise((resolve) => setImmediate(resolve)); await nextTick() }
  return { ...view, bindings, props, moves, trash, bounds, pointer, handle, tip, floating, highlight, flush, visualTip, settle,
    disableTrash() { trashEnabled = false }, cleanup() { bindings.cancelDrag(); view.dispose() } }
}

test('a marker dragged vertically away from its trace follows the pointer with its grab offset, beyond the plot clip', async () => {
  const view = await markerPane()
  try {
    const tip = view.tip()
    view.handle().onPointerdown(view.pointer(tip.x + 3, tip.y - 5, 'pointerdown'))
    view.handle().onPointermove(view.pointer(tip.x + 28, tip.y - 85))
    view.flush()
    assert.deepEqual(view.visualTip(), { x: tip.x + 25, y: tip.y - 80 }, 'Detached preview must follow CSS pointer coordinates instead of snapping back to the measured curve')
    assert.ok(view.floating(), 'The detached symbol must be rendered outside the clipped plot')
    view.handle().onPointermove(view.pointer(20, 10))
    view.flush()
    assert.deepEqual(view.visualTip(), { x: 17, y: 15 }, 'The floating preview must not clamp to plot bounds')
    assert.deepEqual(view.moves, [])
    assert.deepEqual(view.trash, [])
  } finally { view.cleanup() }
})

test('normal horizontal dragging preserves the grab offset without detaching and still submits one frequency move', async () => {
  const view = await markerPane()
  try {
    const tip = view.tip()
    view.handle().onPointerdown(view.pointer(tip.x + 3, tip.y - 30, 'pointerdown'))
    view.handle().onPointermove(view.pointer(tip.x + 80, tip.y - 30))
    view.flush()
    assert.equal(view.floating(), undefined, 'A label grab offset is not a vertical departure from the trace')
    view.handle().onPointerup(view.pointer(tip.x + 80, tip.y - 30, 'pointerup'))
    await view.settle()
    assert.equal(view.moves.length, 1)
    assert.equal(view.moves[0]!.markerId, 1)
    assert.deepEqual(view.trash, [])
  } finally { view.cleanup() }
})

test('a stationary click selects its marker even when the precise marker reading differs from the nearest plotted sample', async () => {
  const view = await markerPane()
  try {
    view.props.frame.traces[0]!.projection.values = [-80, -40, 0]
    const tip = view.tip()
    view.handle().onPointerdown(view.pointer(tip.x, tip.y, 'pointerdown'))
    view.handle().onPointerup(view.pointer(tip.x, tip.y, 'pointerup'))
    await view.settle()
    assert.equal(view.floating(), undefined)
    assert.deepEqual(view.moves, [])
    assert.deepEqual(view.trash, [])
    assert.deepEqual(JSON.parse(JSON.stringify(view.emitted.filter((event) => event[0] === 'selectMarker'))),
      [['selectMarker', { name: 'Reflect', markerId: 1 }]])
  } finally { view.cleanup() }
})

test('an omitted optional display revision preserves normal marker click and frequency drag compatibility', async () => {
  const view = await markerPane()
  try {
    Reflect.deleteProperty(view.props, 'displayRevision')
    const tip = view.tip()
    view.handle().onPointerdown(view.pointer(tip.x, tip.y, 'pointerdown'))
    view.handle().onPointerup(view.pointer(tip.x, tip.y, 'pointerup'))
    await view.settle()
    assert.equal(view.emitted.filter((event) => event[0] === 'selectMarker').length, 1)
    view.handle().onPointerdown(view.pointer(tip.x, tip.y, 'pointerdown'))
    view.handle().onPointermove(view.pointer(tip.x + 80, tip.y))
    view.handle().onPointerup(view.pointer(tip.x + 80, tip.y, 'pointerup'))
    await view.settle()
    assert.equal(view.moves.length, 1)
    assert.deepEqual(view.trash, [])
  } finally { view.cleanup() }
})

test('a horizontal move cannot detach merely because the nearest sample has a steeply different response', async () => {
  const view = await markerPane()
  try {
    view.props.frame.traces[0]!.projection.values = [-80, -40, 0]
    const tip = view.tip()
    view.handle().onPointerdown(view.pointer(tip.x, tip.y, 'pointerdown'))
    view.handle().onPointermove(view.pointer(tip.x + 150, tip.y))
    view.flush()
    assert.equal(view.floating(), undefined)
    view.handle().onPointerup(view.pointer(tip.x + 150, tip.y, 'pointerup'))
    await view.settle()
    assert.equal(view.moves.length, 1)
    assert.deepEqual(view.trash, [])
  } finally { view.cleanup() }
})

test('detach threshold is measured in CSS pixels and an invalid drop cancels without changing the saved frequency', async () => {
  const view = await markerPane()
  try {
    const before = JSON.stringify(view.props.presentations)
    const tip = view.tip()
    view.handle().onPointerdown(view.pointer(tip.x, tip.y, 'pointerdown'))
    view.handle().onPointermove(view.pointer(tip.x + 10, tip.y + 11.5))
    view.flush()
    assert.equal(view.floating(), undefined)
    view.handle().onPointermove(view.pointer(tip.x + 10, tip.y + 12.5))
    view.flush()
    assert.ok(view.floating())
    view.handle().onPointerup(view.pointer(tip.x + 10, tip.y + 12.5, 'pointerup'))
    await view.settle()
    assert.equal(view.floating(), undefined)
    assert.deepEqual(view.moves, [])
    assert.deepEqual(view.trash, [])
    assert.equal(JSON.stringify(view.props.presentations), before)
  } finally { view.cleanup() }
})

test('a detached marker highlights the enabled toolbar target and drops only its frozen single-marker identity', async () => {
  const view = await markerPane()
  try {
    const tip = view.tip()
    view.handle().onPointerdown(view.pointer(tip.x + 3, tip.y - 5, 'pointerdown'))
    view.handle().onPointermove(view.pointer(620, 40))
    view.flush()
    assert.deepEqual(view.visualTip(), { x: 617, y: 45 })
    assert.ok(view.highlight())
    assert.deepEqual(JSON.parse(JSON.stringify(view.highlight()!.props!.style)),
      { left: '600px', top: '20px', width: '40px', height: '40px' })
    const teleport = findElements(view.render(), (node) => typeof node.type === 'object' &&
      Reflect.get(node.type, '__isTeleport') === true)[0]
    assert.equal(teleport?.props?.to, 'body', 'Fixed drag feedback must escape the transformed, clipped instrument canvas')
    view.handle().onPointerup(view.pointer(620, 40, 'pointerup'))
    view.handle().onPointerup(view.pointer(620, 40, 'pointerup'))
    await view.settle()
    assert.deepEqual(JSON.parse(JSON.stringify(view.trash)), [{ kind: 'marker', name: 'Reflect', markerId: 1, displayRevision: '17' }])
    assert.deepEqual(view.moves, [])
    assert.equal(view.floating(), undefined)
    assert.equal(view.highlight(), undefined)
  } finally { view.cleanup() }
})

test('disabled trash and every cancellation path clear detached feedback without deleting or moving the marker', async () => {
  for (const cancel of ['disabledTrash', 'pointercancel', 'escape', 'permission', 'revision', 'geometry', 'resize'] as const) {
    const view = await markerPane()
    try {
      const tip = view.tip()
      view.handle().onPointerdown(view.pointer(tip.x, tip.y, 'pointerdown'))
      view.handle().onPointermove(view.pointer(620, 40))
      view.flush()
      assert.ok(view.floating())
      if (cancel === 'disabledTrash') {
        view.disableTrash()
        view.handle().onPointermove(view.pointer(621, 40))
        view.flush()
        assert.equal(view.highlight(), undefined)
      } else if (cancel === 'pointercancel') view.handle().onPointercancel(view.pointer(620, 40, 'pointercancel'))
      else if (cancel === 'escape') view.bindings.cancelDragKey({ key: 'Escape' })
      else if (cancel === 'permission') view.props.canEditDisplay = false
      else if (cancel === 'revision') view.props.displayRevision = '18'
      else if (cancel === 'geometry') view.props.frame = { ...view.props.frame, frequencyHz: [10e6, 30e6, 50e6] }
      else view.bindings.resizePlot()
      await nextTick()
      view.handle().onPointerup(view.pointer(620, 40, 'pointerup'))
      await view.settle()
      assert.equal(view.floating(), undefined, cancel)
      assert.equal(view.highlight(), undefined, cancel)
      assert.deepEqual(view.trash, [], cancel)
      assert.deepEqual(view.moves, [], cancel)
    } finally { view.cleanup() }
  }
})

test('graphically zoomed marker detachment uses the displayed coordinates while active zoom selection cannot steal the marker', async () => {
  const view = await markerPane()
  try {
    view.props.graphicalZoomSelecting = true
    const layer = findComponent(view.render(), 'PlotInteractionLayer')!.props!
    assert.equal(await layer['commit-zoom']({ traceName: 'Reflect', startHz: 15e6, stopHz: 25e6,
      min: -40, max: 0, revision: '1', displayRevision: '17' }), true)
    view.props.graphicalZoomSelecting = false
    const tip = view.tip()
    view.handle().onPointerdown(view.pointer(tip.x + 3, tip.y - 5, 'pointerdown'))
    view.handle().onPointermove(view.pointer(tip.x + 23, tip.y - 45))
    view.flush()
    assert.deepEqual(view.visualTip(), { x: tip.x + 20, y: tip.y - 40 })
    view.bindings.cancelDrag()
    for (const mode of ['graphical', 'stimulus', 'readonly'] as const) {
      view.props.graphicalZoomSelecting = mode === 'graphical'
      view.props.zoomSelecting = mode === 'stimulus'
      view.props.canEditDisplay = mode !== 'readonly'
      view.handle().onPointerdown(view.pointer(tip.x, tip.y, 'pointerdown'))
      view.handle().onPointermove(view.pointer(620, 40))
      view.flush()
      assert.equal(view.floating(), undefined)
    }
    assert.deepEqual(view.moves, [])
    assert.deepEqual(view.trash, [])
  } finally { view.cleanup() }
})
