import assert from 'node:assert/strict'
import test from 'node:test'
import {
  MarkerDragController,
  createMarkerDragGeometry,
  type DragPoint,
  type MarkerDragGeometryInput,
} from '../src/plot/markerDrag'

function harness() {
  let id = 0
  const frames = new Map<number, () => void>()
  const previews: { point: DragPoint | null; committing: boolean }[] = []
  const controller = new MarkerDragController({
    requestFrame(callback) {
      frames.set(++id, callback)
      return id
    },
    cancelFrame(request) {
      frames.delete(request)
    },
    preview(point, committing) {
      previews.push({ point, committing })
    },
  })
  return {
    controller,
    previews,
    frames,
    paint() {
      const callbacks = [...frames.values()]
      frames.clear()
      callbacks.forEach((callback) => callback())
    },
  }
}

test('drag paints the latest pointer position once per display frame without a backend command', () => {
  const { controller, previews, frames, paint } = harness()
  controller.begin(1, { x: 0, y: 0 }, (point) => point)
  for (let x = 1; x <= 100; ++x) controller.move(1, { x, y: x / 2 })
  assert.equal(frames.size, 1)
  assert.deepEqual(previews, [])
  paint()
  assert.deepEqual(previews, [{ point: { x: 100, y: 50 }, committing: false }])
  controller.move(1, { x: 120, y: 60 })
  paint()
  assert.deepEqual(previews[1], { point: { x: 120, y: 60 }, committing: false })
})

test('release sends only its final position and holds the preview until the authoritative readback finishes', async () => {
  const { controller, previews, frames } = harness()
  const commits: DragPoint[] = []
  let complete!: (success: boolean) => void
  controller.begin(1, { x: 0, y: 0 }, (point) => point)
  controller.move(1, { x: 25, y: 40 })
  const finished = controller.finish(
    1,
    { x: 80, y: 90 },
    (point) => {
      commits.push(point)
      return new Promise((resolve) => {
        complete = resolve
      })
    },
    () => assert.fail('A drag must not become a selection click'),
  )
  assert.deepEqual(commits, [{ x: 80, y: 90 }])
  assert.equal(frames.size, 0)
  assert.equal(controller.dragging, false)
  assert.equal(controller.pending, true)
  assert.deepEqual(previews, [{ point: { x: 80, y: 90 }, committing: true }])
  controller.move(1, { x: 150, y: 150 })
  assert.equal(
    controller.begin(2, { x: 0, y: 0 }, (point) => point),
    false,
  )
  complete(true)
  await finished
  assert.equal(controller.pending, false)
  assert.deepEqual(previews[1], { point: null, committing: false })
})

test('drag cancellation discards unpainted motion and never creates an instrument update', async () => {
  const { controller, previews, frames, paint } = harness()
  controller.begin(1, { x: 0, y: 0 }, (point) => point)
  controller.move(1, { x: 50, y: 70 })
  controller.cancel()
  assert.equal(frames.size, 0)
  paint()
  await controller.finish(
    1,
    { x: 100, y: 100 },
    async () => {
      assert.fail('A canceled gesture must not commit')
    },
    () => assert.fail('A canceled gesture must not select'),
  )
  assert.deepEqual(previews, [{ point: null, committing: false }])
})

test('clicks and unrelated pointer IDs cannot produce drag writes', async () => {
  const { controller, previews, paint } = harness()
  let selections = 0
  controller.begin(1, { x: 20, y: 20 }, (point) => point)
  controller.move(2, { x: 200, y: 200 })
  paint()
  await controller.finish(
    2,
    { x: 200, y: 200 },
    async () => {
      assert.fail('Wrong pointer must not commit')
    },
    () => assert.fail('Wrong pointer must not select'),
  )
  await controller.finish(
    1,
    { x: 21, y: 20 },
    async () => {
      assert.fail('Click must not commit')
    },
    () => {
      selections += 1
    },
  )
  assert.equal(selections, 1)
  assert.deepEqual(previews, [])
})

test('failed commands restore the backend marker and stale readback cannot erase a new gesture', async () => {
  const { controller, previews, paint } = harness()
  let reject!: (error: Error) => void
  controller.begin(1, { x: 0, y: 0 }, (point) => point)
  const finished = controller.finish(
    1,
    { x: 30, y: 40 },
    () =>
      new Promise((_resolve, rejected) => {
        reject = rejected
      }),
    () => assert.fail('Drag must not select'),
  )
  controller.cancel()
  controller.begin(2, { x: 0, y: 0 }, (point) => ({
    x: point.x,
    y: point.y + 2,
  }))
  controller.move(2, { x: 80, y: 90 })
  paint()
  reject(new Error('Display revision changed'))
  await assert.rejects(finished, /Display revision changed/)
  assert.equal(controller.dragging, true)
  assert.deepEqual(previews.at(-1), {
    point: { x: 80, y: 92 },
    committing: false,
  })
  controller.cancel()
})

test('a backend rejection ends pending feedback and restores authoritative rendering', async () => {
  const { controller, previews } = harness()
  controller.begin(1, { x: 0, y: 0 }, (point) => point)
  await controller.finish(
    1,
    { x: 10, y: 20 },
    async () => false,
    () => {},
  )
  assert.equal(controller.pending, false)
  assert.deepEqual(previews.at(-1), { point: null, committing: false })
})

function geometryInput(smith: boolean): MarkerDragGeometryInput {
  return {
    name: 'Trc1',
    smith,
    points: [
      { x: 20, y: 20, sampleIndex: 40 },
      { x: 80, y: 80, sampleIndex: 140 },
    ],
    bounds: { left: 100, top: 50, width: 200, height: 300 },
    area: { left: 0, top: 0, width: 100, height: 100 },
    size: { width: 100, height: 100 },
    scale: { x: 2, y: 3 },
    offset: { x: 0, y: 0 },
    range: { min: 10e6, max: 8e9 },
  }
}

test('Smith preview and release share the original sample index across subsequent CONT frames', () => {
  const input = geometryInput(true)
  const geometry = createMarkerDragGeometry(input)
  const pointer = { x: 260, y: 290 }
  assert.deepEqual(geometry.project(pointer), {
    x: 80,
    y: 80,
    sampleIndex: 140,
  })
  input.points = [{ x: 80, y: 80, sampleIndex: 180 }]
  input.smith = false
  input.size.width = 800
  input.area.width = 800
  input.bounds.left = 0
  input.range.max = 1e9
  assert.deepEqual(geometry.command(pointer), {
    name: 'Trc1',
    sampleIndex: 140,
    discrete: true,
  })
  assert.deepEqual(geometry.project(pointer), {
    x: 80,
    y: 80,
    sampleIndex: 140,
  })
})

test('Cartesian preview uses displayed projection while final frequency keeps full-frame snapping in C++', () => {
  const geometry = createMarkerDragGeometry(geometryInput(false))
  const pointer = { x: 200, y: 200 }
  assert.deepEqual(geometry.project(pointer), { x: 20, y: 20, sampleIndex: 40 })
  assert.deepEqual(geometry.command(pointer), {
    name: 'Trc1',
    frequencyHz: 4005e6,
    discrete: true,
  })
  assert.equal(geometry.command({ x: -100, y: 0 })?.frequencyHz, 10e6)
  assert.equal(geometry.command({ x: 900, y: 0 })?.frequencyHz, 8e9)
})

test('geometry reads DOMRect accessors and refuses empty Smith sample selection', () => {
  const input = geometryInput(true)
  const bounds = input.bounds
  input.bounds = Object.create({
    get left() {
      return bounds.left
    },
    get top() {
      return bounds.top
    },
    get width() {
      return bounds.width
    },
    get height() {
      return bounds.height
    },
  })
  assert.deepEqual(
    createMarkerDragGeometry(input).command({ x: 260, y: 290 }),
    { name: 'Trc1', sampleIndex: 140, discrete: true },
  )
  input.points = []
  assert.equal(
    createMarkerDragGeometry(input).command({ x: 260, y: 290 }),
    null,
  )
})
