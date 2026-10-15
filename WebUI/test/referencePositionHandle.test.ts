import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive } from 'vue'
import type { DisplayFormat, DisplayScale } from '../src/domain/presentation'
import type { PlotRect } from '../src/plot/canvasRenderer'
import type { ReferencePositionMove } from '../src/plot/referencePositionDrag'
import { findElements, sfcHarness } from './sfcHarness'

interface HandleProps {
  name: string
  format: DisplayFormat
  scale: DisplayScale
  color: string
  active: boolean
  canEditDisplay: boolean
  canContinueGesture?: boolean
  displayRevision: string
  rect: PlotRect
  plotSize: { width: number; height: number }
  commit: (move: ReferencePositionMove) => Promise<boolean>
}

function data(value: unknown): unknown {
  return JSON.parse(JSON.stringify(value))
}

async function handle(overrides: Partial<HandleProps> = {}) {
  const requests: ReferencePositionMove[] = []
  let complete: (value: boolean) => void = () => {}
  const props = reactive<HandleProps>({
    name: 'Reflect',
    format: 'db',
    scale: { min: -80, max: 20, referencePosition: 8 },
    color: '#60e600',
    active: false,
    canEditDisplay: true,
    displayRevision: '9007199254740993',
    rect: { left: 24, top: 10, width: 744, height: 460 },
    plotSize: { width: 800, height: 500 },
    commit: (move) => {
      requests.push({ ...move })
      return new Promise((resolve) => {
        complete = resolve
      })
    },
    ...overrides,
  })
  const callbacks = new Map<number, () => void>()
  let nextFrame = 0
  const view = await sfcHarness(
    'src/components/diagrams/ReferencePositionHandle.vue',
    props,
    {
      requestAnimationFrame: (callback: () => void) => {
        callbacks.set(++nextFrame, callback)
        return nextFrame
      },
      cancelAnimationFrame: (id: number) => callbacks.delete(id),
    },
  )
  const captured = new Set<number>()
  const target = {
    parentElement: {
      getBoundingClientRect: () => ({
        left: 40,
        top: 20,
        width: 400,
        height: 250,
      }),
    },
    setPointerCapture(id: number) {
      captured.add(id)
    },
    hasPointerCapture: (id: number) => captured.has(id),
    releasePointerCapture(id: number) {
      captured.delete(id)
    },
    focus() {},
  }
  function pointer(y: number, type = 'pointermove', id = 7) {
    return {
      pointerId: id,
      button: 0,
      clientX: 420,
      clientY: y,
      type,
      currentTarget: target,
      preventDefault() {},
      stopPropagation() {},
    }
  }
  const button = () =>
    findElements(view.render(), (node) => node.type === 'button')[0]?.props
  const preview = () =>
    data(
      view.emitted.filter((event) => event[0] === 'preview').at(-1)?.[1] ??
        null,
    )
  function flushFrames() {
    for (const [id, callback] of callbacks) {
      callbacks.delete(id)
      callback()
    }
  }
  async function settle() {
    await Promise.resolve()
    await nextTick()
    await Promise.resolve()
  }
  return {
    ...view,
    props,
    requests,
    captured,
    target,
    pointer,
    button,
    preview,
    flushFrames,
    settle,
    complete: (value: boolean) => complete(value),
    dispose() {
      complete(false)
      view.dispose()
    },
  }
}

test('真实 RefPos 把手按 CSS 绘图高度拖动，预览不写，松手只提交一次冻结版本', async () => {
  const view = await handle()
  try {
    const before = JSON.stringify(view.props.scale)
    view.button()!.onPointerdown(view.pointer(100, 'pointerdown'))
    assert.equal(view.captured.has(7), true)
    assert.equal(view.preview(), null, '抓取时保留相对偏移，不把三角跳到指针')
    view.button()!.onPointermove(view.pointer(123))
    view.flushFrames()
    assert.deepEqual(view.preview(), { name: 'Reflect', referencePosition: 7 })
    assert.equal(view.requests.length, 0)
    view.props.scale = { ...view.props.scale }
    view.props.rect = { ...view.props.rect }
    view.props.plotSize = { ...view.props.plotSize }
    await view.settle()
    assert.equal(
      view.captured.has(7),
      true,
      '同值状态对象读回不能取消进行中的手势',
    )
    assert.deepEqual(view.preview(), { name: 'Reflect', referencePosition: 7 })
    const pending = view.button()!.onPointerup(view.pointer(146, 'pointerup'))
    assert.deepEqual(view.requests, [
      {
        name: 'Reflect',
        referencePosition: 6,
        displayRevision: '9007199254740993',
      },
    ])
    assert.equal(view.captured.size, 0)
    assert.deepEqual(view.preview(), { name: 'Reflect', referencePosition: 6 })
    view.button()!.onPointerup(view.pointer(169, 'pointerup'))
    assert.equal(view.requests.length, 1)
    assert.equal(
      JSON.stringify(view.props.scale),
      before,
      '临时范围不能修改权威 DTO',
    )
    view.complete(false)
    await pending
    await view.settle()
    assert.equal(view.preview(), null, '后端拒绝必须恢复保存位置')
  } finally {
    view.dispose()
  }
})

test('单击 RefPos 把手只选择迹线，不提交尺度，也不开启临时预览', async () => {
  const view = await handle()
  try {
    const before = JSON.stringify(view.props)
    view.button()!.onPointerdown(view.pointer(100, 'pointerdown'))
    view.button()!.onPointerup(view.pointer(101, 'pointerup'))
    view
      .button()!
      .onClick({ detail: 1, preventDefault() {}, stopPropagation() {} })
    await view.settle()
    assert.equal(view.requests.length, 0)
    assert.deepEqual(
      view.emitted.filter((event) => event[0] === 'select'),
      [['select', 'Reflect']],
    )
    assert.equal(view.preview(), null)
    assert.equal(JSON.stringify(view.props), before)
  } finally {
    view.dispose()
  }
})

test('辅助技术激活只选择一次，非主指针或鼠标右键不能启动尺度手势', async () => {
  const view = await handle()
  try {
    for (const override of [{ button: 2 }, { isPrimary: false }]) {
      const button = view.button()!
      button.onPointerdown({ ...view.pointer(100, 'pointerdown'), ...override })
      button.onPointerup(view.pointer(146, 'pointerup'))
    }
    assert.equal(view.captured.size, 0)
    assert.deepEqual(view.emitted, [])
    view
      .button()!
      .onClick({ detail: 0, preventDefault() {}, stopPropagation() {} })
    assert.deepEqual(view.emitted, [['select', 'Reflect']])
    assert.equal(view.requests.length, 0)
  } finally {
    view.dispose()
  }
})

test('圆图、观察器、非法 revision 与非法尺度的旧模板事件不能开始手势', async () => {
  const cases: Partial<HandleProps>[] = [
    ...(['smith', 'invSmith', 'polar'] as const).map((format) => ({ format })),
    { canEditDisplay: false },
    { displayRevision: '' },
    { displayRevision: 'NaN' },
    { displayRevision: '-1' },
    { displayRevision: '1e3' },
    { scale: { min: 0, max: 0, referencePosition: 8 } },
    { scale: { min: -80, max: 20, referencePosition: 11 } },
    { scale: { min: NaN, max: 20, referencePosition: 8 } },
  ]
  for (const overrides of cases) {
    const view = await handle(overrides)
    try {
      const button = view.button()
      if (button) {
        button.onPointerdown(view.pointer(100, 'pointerdown'))
        button.onPointermove(view.pointer(146))
        view.flushFrames()
        button.onPointerup(view.pointer(146, 'pointerup'))
        button.onClick({ detail: 0, preventDefault() {}, stopPropagation() {} })
      }
      assert.equal(view.requests.length, 0, JSON.stringify(overrides))
      assert.equal(view.preview(), null)
      assert.equal(view.captured.size, 0)
      assert.equal(
        view.emitted.some((event) => event[0] === 'select'),
        false,
      )
    } finally {
      view.dispose()
    }
  }
})

test('取消或几何、身份、格式、尺度、权限失效会撤销拖动预览，旧松手不写入', async () => {
  const cases = [
    'escape',
    'pointercancel',
    'lostcapture',
    'rect',
    'size',
    'format',
    'scale',
    'revision',
    'name',
    'readonly',
  ] as const
  for (const reason of cases) {
    const view = await handle()
    try {
      const source = view.button()!
      source.onPointerdown(view.pointer(100, 'pointerdown'))
      source.onPointermove(view.pointer(146))
      view.flushFrames()
      assert.deepEqual(view.preview(), {
        name: 'Reflect',
        referencePosition: 6,
      })
      if (reason === 'escape')
        source.onKeydown({
          key: 'Escape',
          preventDefault() {},
          stopPropagation() {},
        })
      else if (reason === 'pointercancel')
        source.onPointercancel(view.pointer(146, 'pointercancel'))
      else if (reason === 'lostcapture') {
        view.captured.clear()
        source.onLostpointercapture(view.pointer(146, 'lostpointercapture'))
      } else if (reason === 'rect') view.props.rect.height = 440
      else if (reason === 'size') view.props.plotSize.height = 600
      else if (reason === 'format') view.props.format = 'phase'
      else if (reason === 'scale')
        view.props.scale = { min: -40, max: 60, referencePosition: 8 }
      else if (reason === 'revision') view.props.displayRevision = '2'
      else if (reason === 'name') view.props.name = 'Transmit'
      else view.props.canEditDisplay = false
      await view.settle()
      assert.equal(view.preview(), null, reason)
      source.onPointerup(view.pointer(146, 'pointerup'))
      assert.equal(view.requests.length, 0, reason)
    } finally {
      view.dispose()
    }
  }
})

test('自身写入门禁和正常丢失 capture 不清 pending 预览，权威读回结束后才清理', async () => {
  const view = await handle()
  try {
    const button = view.button()!
    button.onPointerdown(view.pointer(100, 'pointerdown'))
    const pending = button.onPointerup(view.pointer(146, 'pointerup'))
    assert.equal(view.requests.length, 1)
    view.props.canEditDisplay = false
    button.onLostpointercapture(view.pointer(146, 'lostpointercapture'))
    await view.settle()
    assert.deepEqual(view.preview(), { name: 'Reflect', referencePosition: 6 })
    view.props.canEditDisplay = true
    button.onPointerdown(view.pointer(200, 'pointerdown'))
    button.onPointerup(view.pointer(246, 'pointerup'))
    assert.equal(view.requests.length, 1, '读回前不能重入另一次拖动')
    // commit 回调契约与 Marker 相同：终态及权威读回完成后才 resolve。
    view.props.scale = { min: -60, max: 40, referencePosition: 6 }
    view.props.displayRevision = '9007199254740994'
    await view.settle()
    assert.deepEqual(view.preview(), { name: 'Reflect', referencePosition: 6 })
    view.complete(true)
    await pending
    await view.settle()
    assert.equal(view.preview(), null)
  } finally {
    view.dispose()
  }
})

test('续租只暂停新写入口，不打断已有 capture 和预览，恢复后松手仍只提交一次', async () => {
  const view = await handle({ canContinueGesture: true })
  try {
    const button = view.button()!
    button.onPointerdown(view.pointer(100, 'pointerdown'))
    button.onPointermove(view.pointer(123))
    view.flushFrames()
    view.props.canEditDisplay = false
    await view.settle()
    assert.equal(view.captured.has(7), true)
    assert.deepEqual(view.preview(), { name: 'Reflect', referencePosition: 7 })
    assert.equal(view.button()!.disabled, false, '已捕获手势仍须接收 pointerup')
    button.onPointerdown(view.pointer(200, 'pointerdown', 8))
    button.onPointerup(view.pointer(246, 'pointerup', 8))
    assert.deepEqual([...view.captured], [7], '续租期间不能开始第二个指针')
    assert.equal(view.requests.length, 0)
    view.props.canEditDisplay = true
    button.onPointermove(view.pointer(146))
    view.flushFrames()
    const pending = button.onPointerup(view.pointer(146, 'pointerup'))
    assert.deepEqual(view.requests, [
      {
        name: 'Reflect',
        referencePosition: 6,
        displayRevision: '9007199254740993',
      },
    ])
    view.props.scale = { min: -60, max: 40, referencePosition: 6 }
    view.props.displayRevision = '9007199254740994'
    view.complete(true)
    await pending
    await view.settle()
    assert.equal(view.preview(), null)
    assert.equal(view.captured.size, 0)
    view.props.canEditDisplay = false
    assert.equal(view.button()!.disabled, true, '可继续手势不代表允许新手势')
    view.button()!.onPointerdown(view.pointer(100, 'pointerdown', 8))
    view.button()!.onPointerup(view.pointer(146, 'pointerup', 8))
    assert.equal(view.requests.length, 1)
  } finally {
    view.dispose()
  }
})

test('真正失去继续权限会立即取消手势，旧松手不能借续租豁免写入', async () => {
  const view = await handle({ canContinueGesture: true })
  try {
    const button = view.button()!
    button.onPointerdown(view.pointer(100, 'pointerdown'))
    button.onPointermove(view.pointer(146))
    view.flushFrames()
    assert.deepEqual(view.preview(), { name: 'Reflect', referencePosition: 6 })
    view.props.canEditDisplay = false
    view.props.canContinueGesture = false
    await view.settle()
    assert.equal(view.preview(), null)
    assert.equal(view.captured.size, 0)
    button.onPointerup(view.pointer(146, 'pointerup'))
    assert.equal(view.requests.length, 0)
  } finally {
    view.dispose()
  }
})
