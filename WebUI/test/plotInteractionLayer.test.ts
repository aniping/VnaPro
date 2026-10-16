import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive } from 'vue'
import type { StimulusZoomSelection } from '../src/api/types'
import type { DisplayFormat, DisplayScale } from '../src/domain/presentation'
import type { PlotRect } from '../src/plot/canvasRenderer'
import type { TracePanMove } from '../src/plot/plotGestures'
import { findElements, sfcHarness } from './sfcHarness'

interface LayerProps {
  name: string
  format: DisplayFormat
  scale: DisplayScale
  revision: string
  displayRevision: string
  range: { min: number; max: number }
  rect: PlotRect
  size: { width: number; height: number }
  canPan: boolean
  zoomSelecting: boolean
  canZoom: boolean
  canContinueGesture: boolean
  commitPan?: (value: TracePanMove) => Promise<boolean>
  commitZoom?: (value: StimulusZoomSelection) => Promise<boolean>
}
const copy = (value: unknown) => JSON.parse(JSON.stringify(value))

async function interaction(overrides: Partial<LayerProps> = {}) {
  const pans: TracePanMove[] = []
  const zooms: StimulusZoomSelection[] = []
  let complete: (value: boolean) => void = () => {}
  let fail: (reason: Error) => void = () => {}
  const pending = () => new Promise<boolean>((resolve, reject) => { complete = resolve; fail = reject })
  const props = reactive<LayerProps>({
    name: 'Reflect', format: 'db', scale: { min: -80, max: 20, referencePosition: 8 },
    revision: '9007199254740993', displayRevision: '9007199254740994',
    range: { min: 10, max: 1010 }, rect: { left: 40, top: 20, width: 720, height: 460 },
    size: { width: 800, height: 500 }, canPan: true, zoomSelecting: false,
    canZoom: true, canContinueGesture: true,
    commitPan: (value) => { pans.push({ ...value }); return pending() },
    commitZoom: (value) => { zooms.push({ ...value }); return pending() },
    ...overrides,
  })
  const frames = new Map<number, () => void>()
  let nextFrame = 0
  const view = await sfcHarness('src/components/diagrams/PlotInteractionLayer.vue', props, {
    requestAnimationFrame: (callback: () => void) => { frames.set(++nextFrame, callback); return nextFrame },
    cancelAnimationFrame: (id: number) => frames.delete(id),
  })
  const captures = new Set<number>()
  const bounds = { left: 40, top: 20, width: 400, height: 250 }
  const target = {
    getBoundingClientRect: () => ({ ...bounds }),
    setPointerCapture(id: number) { captures.add(id) },
    hasPointerCapture: (id: number) => captures.has(id),
    releasePointerCapture(id: number) { captures.delete(id) },
  }
  function pointer(x: number, y: number, id = 7) {
    return {
      pointerId: id, button: 0, isPrimary: true, clientX: x, clientY: y, currentTarget: target,
      prevented: false, stopped: false,
      preventDefault() { this.prevented = true }, stopPropagation() { this.stopped = true },
    }
  }
  const root = () => view.render().props!
  const last = (event: string) => copy(view.emitted.filter((item) => item[0] === event).at(-1)?.[1] ?? null)
  const rectangle = () => findElements(view.render(), (node) => node.type === 'rect')[0]?.props
  const flush = () => { for (const [id, callback] of frames) { frames.delete(id); callback() } }
  const settle = async () => { await Promise.resolve(); await nextTick(); await Promise.resolve() }
  function click(detail = 1) {
    const event = { detail, prevented: false, stopped: false,
      preventDefault() { this.prevented = true }, stopPropagation() { this.stopped = true } }
    root().onClick(event)
    return event
  }
  return {
    ...view, props, pans, zooms, captures, bounds, target, pointer, root, last, rectangle, flush, settle, click, frames,
    complete: (value: boolean) => complete(value), fail: () => fail(new Error('Rejected')),
    dispose() { complete(false); view.dispose() },
  }
}

test('plot layer gates Cartesian input, ignores invalid starts, and lets ordinary clicks bubble', async () => {
  const view = await interaction()
  try {
    assert.equal(view.root().style.cursor, 'default', 'An idle diagram must retain the ordinary pointer')
    for (const format of ['smith', 'polar', 'invSmith'] as const) {
      view.props.format = format
      assert.equal(view.root().style.pointerEvents, 'none')
      view.root().onPointerdown(view.pointer(100, 100))
      assert.equal(view.captures.size, 0)
    }
    view.props.format = 'db'
    view.props.canPan = false
    assert.equal(view.root().style.pointerEvents, 'none')
    view.props.zoomSelecting = true
    assert.equal(view.root().style.pointerEvents, 'auto')
    assert.equal(view.root().style.cursor, 'crosshair')
    view.props.canZoom = false
    assert.equal(view.root().style.pointerEvents, 'none', 'Zoom mode must not fall back to panning')
    view.props.zoomSelecting = false
    view.props.canPan = true
    for (const event of [view.pointer(45, 25), { ...view.pointer(100, 100), button: 2 },
      { ...view.pointer(100, 100), isPrimary: false }]) {
      view.root().onPointerdown(event)
      assert.equal(view.captures.size, 0)
    }
    const down = view.pointer(100, 100)
    view.root().onPointerdown(down)
    assert.equal(down.prevented, false)
    assert.equal(view.root().style.cursor, 'grabbing')
    view.root().onPointerup(view.pointer(100, 102))
    assert.equal(view.pans.length, 0)
    assert.equal(view.click().stopped, false)
    assert.equal(view.last('active'), false)
    assert.equal(view.root().style.cursor, 'default')
    view.props.rect.width = 0
    assert.equal(view.root().style.pointerEvents, 'none')
  } finally { view.dispose() }
})

test('pan freezes CSS geometry and versions, coalesces preview, and commits once on out-of-plot release', async () => {
  const view = await interaction()
  try {
    const original = JSON.stringify(view.props.scale)
    view.root().onPointerdown(view.pointer(140, 100))
    assert.deepEqual([...view.captures], [7])
    assert.equal(view.last('active'), true)
    view.root().onPointermove(view.pointer(140, 102))
    assert.equal(view.frames.size, 0)
    view.root().onPointermove(view.pointer(140, 112))
    view.root().onPointermove(view.pointer(140, 123))
    assert.equal(view.frames.size, 1)
    assert.equal(view.last('preview'), null)
    view.flush()
    assert.deepEqual(view.last('preview'), { min: -70, max: 30, referencePosition: 8 })
    assert.equal(view.pans.length, 0)
    view.props.scale = { ...view.props.scale }
    view.props.rect = { ...view.props.rect }
    view.props.range = { ...view.props.range }
    assert.equal(view.captures.size, 1, 'Equivalent readback objects do not cancel a gesture')
    view.root().onPointerdown(view.pointer(200, 200, 8))
    view.root().onPointermove(view.pointer(200, 250, 8))
    assert.deepEqual([...view.captures], [7])
    const pending = view.root().onPointerup(view.pointer(900, 330))
    assert.deepEqual(view.pans, [{ name: 'Reflect', reference: 100, displayRevision: '9007199254740994' }])
    assert.equal(JSON.stringify(view.props.scale), original)
    assert.deepEqual(view.last('preview'), { min: 20, max: 120, referencePosition: 8 })
    assert.equal(view.captures.size, 0)
    assert.equal(view.last('active'), true, 'Keep exclusive interaction ownership through readback')
    view.root().onPointerup(view.pointer(900, 330))
    view.root().onLostpointercapture(view.pointer(900, 330))
    view.props.canPan = false
    view.props.displayRevision = '9007199254740995'
    view.props.scale = { min: 20, max: 120, referencePosition: 8 }
    assert.deepEqual(view.last('preview'), { min: 20, max: 120, referencePosition: 8 })
    assert.equal(view.click().prevented, true)
    view.complete(true)
    await pending
    assert.equal(view.last('preview'), null)
    assert.equal(view.last('active'), false)
    assert.equal(view.pans.length, 1)
  } finally { view.dispose() }
})

test('pointer cancellation, stale geometry and true permission loss discard previews without writing', async () => {
  const mutations: ((props: LayerProps) => void)[] = [
    (props) => { props.name = 'Other' }, (props) => { props.format = 'phase' },
    (props) => { props.revision = '9007199254740995' },
    (props) => { props.displayRevision = '9007199254740995' },
    (props) => { props.scale.max = 30 }, (props) => { props.scale.referencePosition = 7 },
    (props) => { props.range.max = 2000 }, (props) => { props.rect.width = 700 },
    (props) => { props.size.height = 600 }, (props) => { props.canContinueGesture = false },
  ]
  for (const mutate of mutations) {
    const view = await interaction()
    try {
      view.root().onPointerdown(view.pointer(140, 100))
      view.root().onPointermove(view.pointer(140, 123))
      mutate(view.props)
      view.flush()
      view.root().onPointerup(view.pointer(140, 146))
      assert.equal(view.captures.size, 0)
      assert.equal(view.last('preview'), null)
      assert.equal(view.pans.length, 0)
    } finally { view.dispose() }
  }
  for (const cancel of ['onPointercancel', 'onLostpointercapture', 'resize', 'escape']) {
    const view = await interaction()
    try {
      view.root().onPointerdown(view.pointer(140, 100))
      view.root().onPointermove(view.pointer(140, 123))
      view.flush()
      if (cancel === 'resize') (view.bindings.resized as () => void)()
      else if (cancel === 'escape') view.root().onKeydown({ key: 'Escape', preventDefault() {}, stopPropagation() {} })
      else view.root()[cancel](view.pointer(140, 123))
      view.root().onPointerup(view.pointer(140, 146))
      assert.equal(view.pans.length, 0)
      assert.equal(view.captures.size, 0)
      assert.equal(view.last('active'), false)
    } finally { view.dispose() }
  }
})

test('renewal pauses new pan input while retaining captured movement; rejected commit clears pending state', async () => {
  const view = await interaction()
  try {
    view.root().onPointerdown(view.pointer(140, 100))
    view.props.canPan = false
    assert.equal(view.root().style.pointerEvents, 'auto')
    view.root().onPointerdown(view.pointer(150, 100, 8))
    view.root().onPointermove(view.pointer(140, 123))
    view.flush()
    const pending = view.root().onPointerup(view.pointer(140, 146))
    assert.equal(view.pans.length, 1)
    view.props.canPan = true
    view.root().onPointerdown(view.pointer(140, 100))
    assert.equal(view.captures.size, 0, 'Pending write cannot be reentered')
    view.fail()
    await pending
    assert.equal(view.last('preview'), null)
    assert.equal(view.last('active'), false)
    assert.equal(view.root().style.pointerEvents, 'auto')
    view.root().onPointerdown(view.pointer(140, 100))
    const retry = view.root().onPointerup(view.pointer(140, 123))
    assert.equal(view.pans.length, 2)
    view.complete(false)
    await retry
    assert.equal(view.last('preview'), null)
    assert.equal(view.click(0).stopped, false, 'Keyboard click does not inherit a pointer drag')
  } finally { view.dispose() }
})

test('reverse zoom draws a clamped logical rectangle and submits one frozen physical selection', async () => {
  const view = await interaction({ zoomSelecting: true })
  try {
    view.root().onPointerdown(view.pointer(420, 260))
    view.root().onPointermove(view.pointer(-100, -100))
    assert.equal(view.rectangle(), undefined, 'Zoom preview is batched on animation frames')
    view.flush()
    const rect = view.rectangle()!
    assert.deepEqual([rect.x, rect.y, rect.width, rect.height], [40, 20, 720, 460])
    const pending = view.root().onPointerup(view.pointer(-100, -100))
    assert.deepEqual(view.zooms, [{ traceName: 'Reflect', startHz: 10, stopHz: 1010, min: -80, max: 20,
      revision: '9007199254740993', displayRevision: '9007199254740994' }])
    assert.equal(view.pans.length, 0)
    view.props.zoomSelecting = false
    view.props.canZoom = false
    view.props.revision = '9007199254740995'
    assert.ok(view.rectangle(), 'The submitted box remains until authoritative readback')
    view.complete(true)
    await pending
    assert.equal(view.rectangle(), undefined)
    assert.equal(view.last('active'), false)
    assert.equal(view.click().stopped, true)
  } finally { view.dispose() }
})

test('narrow zooms never write, Escape exits zoom, and failed capture creates no active gesture', async () => {
  const view = await interaction({ zoomSelecting: true })
  try {
    for (const end of [[200, 102], [142, 200], [144, 104]]) {
      view.root().onPointerdown(view.pointer(140, 100))
      await view.root().onPointerup(view.pointer(end[0]!, end[1]!))
      assert.equal(view.zooms.length, 0)
      assert.equal(view.rectangle(), undefined)
      assert.equal(view.captures.size, 0)
    }
    view.root().onPointerdown(view.pointer(140, 100))
    view.root().onPointermove(view.pointer(240, 200))
    view.flush()
    view.root().onKeydown({ key: 'Escape', preventDefault() {}, stopPropagation() {} })
    assert.ok(view.emitted.some((event) => event[0] === 'cancelZoom'))
    assert.equal(view.rectangle(), undefined)
    view.root().onPointerup(view.pointer(240, 200))
    assert.equal(view.zooms.length, 0)
    view.target.setPointerCapture = () => { throw new Error('Detached target') }
    view.root().onPointerdown(view.pointer(140, 100))
    assert.equal(view.captures.size, 0)
    assert.equal(view.last('active'), false)
  } finally { view.dispose() }
})
