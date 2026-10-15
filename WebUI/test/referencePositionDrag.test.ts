import assert from 'node:assert/strict'
import test from 'node:test'
import {
  createReferencePositionGeometry,
  previewReferenceScale,
  ReferencePositionDragController,
  type ReferencePositionGeometry,
  type ReferencePositionGeometryInput,
  type ReferencePositionMove,
} from '../src/plot/referencePositionDrag'
import type { DisplayFormat, DisplayScale } from '../src/domain/presentation'

function input(overrides: Partial<ReferencePositionGeometryInput> = {}) {
  return {
    name: 'Trc2', displayRevision: '18446744073709551615',
    startClientY: 200, startPosition: 8, plotHeightCSS: 200,
    ...overrides,
  }
}

function geometry(overrides: Partial<ReferencePositionGeometryInput> = {}): ReferencePositionGeometry {
  const value = createReferencePositionGeometry(input(overrides))
  assert.ok(value)
  return value
}

function near(actual: number, expected: number) {
  assert.ok(Math.abs(actual - expected) <= 1e-12 * Math.max(Math.abs(expected), Number.MIN_VALUE),
    `expected ${expected}, got ${actual}`)
}

function deferred() {
  let resolve!: (success: boolean) => void
  let reject!: (error: Error) => void
  const promise = new Promise<boolean>((yes, no) => { resolve = yes; reject = no })
  return { promise, resolve, reject }
}

function harness() {
  let sequence = 0
  const frames = new Map<number, () => void>()
  const previews: { position: number | null; committing: boolean }[] = []
  const controller = new ReferencePositionDragController({
    requestFrame(callback) { frames.set(++sequence, callback); return sequence },
    cancelFrame(request) { frames.delete(request) },
    preview(position, committing) { previews.push({ position, committing }) },
  })
  return {
    controller, previews, frames,
    paint() {
      const callbacks = [...frames.values()]
      frames.clear()
      callbacks.forEach((callback) => callback())
    },
  }
}

test('RefPos向上增加、向下减小，端点为上10下0并钳制到合法范围', () => {
  const value = geometry({ startPosition: 5, startClientY: 300, plotHeightCSS: 400 })
  near(value.positionAt(100)!, 10)
  near(value.positionAt(500)!, 0)
  near(value.positionAt(280)!, 5.5)
  assert.equal(value.positionAt(-1000), 10)
  assert.equal(value.positionAt(2000), 0)
  assert.equal(value.positionAt(NaN), null)
  assert.equal(value.commandAt(Infinity), null)
  assert.deepEqual(value.commandAt(280), {
    name: 'Trc2', referencePosition: 5.5, displayRevision: '18446744073709551615',
  })
})

test('非均匀CSS缩放用屏幕图高，保留抓取点偏移并冻结手势输入', () => {
  // 逻辑高100经Y×3为300CSSpx；X缩放不参与纵向RefPos。实际尖端Y304，抓在尖端下7px。
  const source = input({ startClientY: 311, startPosition: 2, plotHeightCSS: 300 })
  const value = createReferencePositionGeometry(source)!
  near(value.positionAt(311)!, 2)
  near(value.positionAt(251)!, 4)
  const movedTipY = 64 + (1 - value.positionAt(251)! / 10) * 300
  near(251 - movedTipY, 7)
  source.name = 'Reused'
  source.displayRevision = '99'
  source.startPosition = 10
  source.startClientY = -800
  source.plotHeightCSS = 1
  assert.deepEqual(value.commandAt(251), {
    name: 'Trc2', referencePosition: 4, displayRevision: '18446744073709551615',
  })
})

test('无效几何不产生手势，旧权威版本保持数字字符串', () => {
  for (const invalid of [
    { name: '' }, { displayRevision: '' }, { displayRevision: '9.2' },
    { startPosition: -1 }, { startPosition: 11 }, { startPosition: NaN },
    { startClientY: Infinity }, { plotHeightCSS: 0 }, { plotHeightCSS: -1 },
    { plotHeightCSS: NaN },
  ]) assert.equal(createReferencePositionGeometry(input(invalid)), null)
})

test('预览只平移显示范围，dB和线性刻度保留参考值与每格跨度', () => {
  for (const [format, scale, position, expected] of [
    ['db', { min: -90, max: 10, referencePosition: 9 }, 5, [-50, 50]],
    ['magnitude', { min: 1, max: 11, referencePosition: 8 }, 2, [7, 17]],
    ['swr', { min: 0.1, max: 2.1, referencePosition: 5 }, 10, [-0.9, 1.1]],
    ['phase', { min: 0, max: 10 }, 0, [8, 18]],
  ] as [DisplayFormat, DisplayScale, number, number[]][]) {
    const before = { ...scale }
    const next = previewReferenceScale(scale, format, position)
    assert.ok(next)
    near(next.min, expected[0]!)
    near(next.max, expected[1]!)
    near(next.max - next.min, scale.max - scale.min)
    near(next.min + position * (next.max - next.min) / 10,
      scale.min + (scale.referencePosition ?? 8) * (scale.max - scale.min) / 10)
    assert.equal(next.referencePosition, position)
    assert.deepEqual(scale, before)
  }
})

test('Log Mag按decade平移并保留正参考幅度，拒绝非法坐标和数值溢出', () => {
  const source = { min: 0.01, max: 100, referencePosition: 5 }
  const next = previewReferenceScale(source, 'logMagnitude', 2.5)
  assert.ok(next)
  near(next.min, 0.1)
  near(next.max, 1000)
  near(Math.log10(next.max / next.min), 4)
  near(10 ** (Math.log10(next.min) + 0.25 * Math.log10(next.max / next.min)), 1)
  for (const [scale, format, position] of [
    [{ min: 0, max: 100 }, 'logMagnitude', 5],
    [{ min: -1, max: 10 }, 'logMagnitude', 5],
    [{ min: Number.MIN_VALUE, max: 1e-320, referencePosition: 0 }, 'logMagnitude', 10],
    [{ min: 1e304, max: 1e308, referencePosition: 10 }, 'logMagnitude', 0],
    [{ min: 1, max: 1 }, 'db', 5],
    [{ min: 1, max: Infinity }, 'db', 5],
    [{ min: 0, max: 10, referencePosition: 11 }, 'db', 5],
    [{ min: 0, max: 10 }, 'db', NaN],
    [{ min: 0, max: 10 }, 'db', -1],
    [{ min: 0, max: 10 }, 'db', 11],
    [{ min: -1, max: 1 }, 'smith', 5],
    [{ min: -1, max: 1 }, 'polar', 5],
    [{ min: -1, max: 1 }, 'invSmith', 5],
  ] as [DisplayScale, DisplayFormat, number][])
    assert.equal(previewReferenceScale(scale, format, position), null)
  assert.deepEqual(source, { min: 0.01, max: 100, referencePosition: 5 })
})

test('只有纵向越过阈值才是RefPos手势，横移及小抖动不写尺度', async () => {
  for (const ys of [[200, 200, 200, 200], [201, 202, 202.9]]) {
    const view = harness()
    let selections = 0
    view.controller.begin(7, 200, geometry())
    for (const y of ys) view.controller.move(7, y)
    assert.equal(view.frames.size, 0)
    await view.controller.finish(7, ys.at(-1)!, async () => assert.fail('微动不能提交RefPos'), () => { selections++ })
    assert.equal(selections, 1)
    assert.equal(view.controller.pending, false)
    assert.equal(view.previews.filter((entry) => entry.position !== null).length, 0)
  }
})

test('不依赖采集帧，千次pointer移动仅排一帧并预览最后Y坐标', () => {
  const view = harness()
  assert.equal(view.controller.begin(1, 200, geometry({ startPosition: 5, plotHeightCSS: 1000 })), true)
  for (let step = 1; step <= 1000; step++) view.controller.move(1, 200 + step / 10)
  assert.equal(view.frames.size, 1)
  assert.deepEqual(view.previews, [])
  view.paint()
  assert.deepEqual(view.previews, [{ position: 4, committing: false }])
  view.controller.move(1, 100)
  view.paint()
  assert.deepEqual(view.previews.at(-1), { position: 6, committing: false })
  view.controller.cancel()
})

test('释放使用未赶上rAF的最后坐标，只写一次并持有预览直到权威读回', async () => {
  const view = harness()
  const done = deferred()
  const writes: ReferencePositionMove[] = []
  view.controller.begin(1, 200, geometry())
  view.controller.move(1, 190)
  const finished = view.controller.finish(1, 170, (move) => { writes.push(move); return done.promise }, () => assert.fail('拖动不能当点击'))
  assert.deepEqual(writes, [{ name: 'Trc2', referencePosition: 9.5, displayRevision: '18446744073709551615' }])
  assert.equal(view.frames.size, 0)
  assert.equal(view.controller.dragging, false)
  assert.equal(view.controller.pending, true)
  assert.deepEqual(view.previews.at(-1), { position: 9.5, committing: true })
  view.controller.move(1, 250)
  assert.equal(view.controller.begin(2, 200, geometry()), false)
  await view.controller.finish(1, 250, async () => assert.fail('同手势不能重复提交'), () => {})
  assert.equal(writes.length, 1)
  done.resolve(true)
  await finished
  assert.equal(view.controller.pending, false)
  assert.deepEqual(view.previews.at(-1), { position: null, committing: false })
  assert.equal(view.controller.begin(2, 200, geometry()), true)
  view.controller.cancel()
})

test('取消未提交手势、拖回原位及边界外钳位不变均不产生尺度写入', async () => {
  const view = harness()
  view.controller.begin(1, 200, geometry())
  view.controller.move(1, 150)
  view.controller.cancel()
  assert.equal(view.frames.size, 0)
  view.paint()
  await view.controller.finish(1, 150, async () => assert.fail('取消不能提交'), () => assert.fail('取消不能选择'))
  assert.equal(view.controller.begin(2, 200, geometry()), true)
  view.controller.move(2, 150)
  view.paint()
  await view.controller.finish(2, 200, async () => assert.fail('回原位不能写'), () => {})
  assert.deepEqual(view.previews.at(-1), { position: null, committing: false })
  view.controller.begin(3, 200, geometry({ startPosition: 10 }))
  await view.controller.finish(3, 180, async () => assert.fail('钳位仍为10不能写'), () => {})
  assert.equal(view.controller.pending, false)
})

test('非所属pointer不能移动、结束或替换当前手势', async () => {
  const view = harness()
  view.controller.begin(1, 200, geometry())
  assert.equal(view.controller.begin(2, 200, geometry()), false)
  view.controller.move(2, 120)
  await view.controller.finish(2, 120, async () => assert.fail('错误pointer不能提交'), () => assert.fail('错误pointer不能选择'))
  assert.equal(view.controller.dragging, true)
  assert.equal(view.frames.size, 0)
  assert.deepEqual(view.previews, [])
  let writes = 0
  await view.controller.finish(1, 180, async () => { writes++; return true }, () => {})
  assert.equal(writes, 1)
})

test('后端拒绝或抛异常都回滚预览并释放pending，失败不重复写入', async () => {
  for (const throwing of [false, true]) {
    const view = harness()
    let writes = 0
    view.controller.begin(1, 200, geometry())
    const finished = view.controller.finish(1, 180, async () => {
      writes++
      if (throwing) throw new Error('Display revision changed')
      return false
    }, () => {})
    if (throwing) await assert.rejects(finished, /Display revision changed/)
    else await finished
    assert.equal(writes, 1)
    assert.equal(view.controller.pending, false)
    assert.equal(view.controller.dragging, false)
    assert.deepEqual(view.previews.at(-1), { position: null, committing: false })
  }
})

test('pending中取消仅清局部预览，已受理写入结束前不允许下一次手势', async () => {
  for (const outcome of ['success', 'reject', 'throw'] as const) {
    const view = harness()
    const done = deferred()
    view.controller.begin(1, 200, geometry())
    const finished = view.controller.finish(1, 180, () => done.promise, () => {})
    const observed = outcome === 'throw' ? assert.rejects(finished, /readback failed/) : finished
    view.controller.cancel()
    assert.equal(view.controller.dragging, false)
    assert.equal(view.controller.pending, true)
    assert.equal(view.previews.at(-1)?.position, null)
    assert.equal(view.controller.begin(2, 200, geometry()), false)
    if (outcome === 'throw') done.reject(new Error('readback failed'))
    else done.resolve(outcome === 'success')
    await observed
    assert.equal(view.controller.pending, false)
    assert.equal(view.controller.begin(2, 200, geometry()), true)
    view.controller.move(2, 190)
    view.paint()
    assert.deepEqual(view.previews.at(-1), { position: 8.5, committing: false })
    view.controller.cancel()
  }
})
