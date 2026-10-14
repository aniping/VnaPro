import assert from 'node:assert/strict'
import test from 'node:test'
import { reactive } from 'vue'
import type { useInstrument } from '../src/composables/useInstrument'
import { NEW_TRACE_DRAG_TYPE } from '../src/components/instrument/newTraceDrag'
import { TRACE_DRAG_TYPE } from '../src/components/diagrams/traceDrag'
import {
  acquiredFrame,
  instrumentStatus,
  presentation,
  receiverTrace,
  reflection,
  transmission,
} from './displayFixtures'
import { findComponent, findElements, sfcHarness } from './sfcHarness'

const positions = ['center', 'above', 'below', 'left', 'right'] as const
const points = {
  center: [250, 180],
  above: [250, 36],
  below: [250, 324],
  left: [50, 180],
  right: [450, 180],
}
const revision = '18446744073709551615'

// SFC 在隔离 VM 中创建载荷，比较传输数据不比较跨上下文对象原型。
function payload(value: unknown): unknown {
  return JSON.parse(JSON.stringify(value))
}

function transfer(
  type: string,
  data: string,
  position: (typeof positions)[number] = 'center',
) {
  let prevented = 0
  return {
    clientX: 40 + points[position]![0]!,
    clientY: 20 + points[position]![1]!,
    currentTarget: {
      getBoundingClientRect: () => ({
        left: 40,
        top: 20,
        width: 500,
        height: 360,
      }),
    },
    dataTransfer: {
      types: [type],
      dropEffect: '',
      getData: (key: string) => (key === type ? data : ''),
    },
    preventDefault() {
      prevented += 1
    },
    prevented: () => prevented,
  }
}

function moving(
  position: (typeof positions)[number] = 'center',
  version = revision,
  name = 'Transmit',
) {
  return transfer(
    TRACE_DRAG_TYPE,
    JSON.stringify({ name, displayRevision: version }),
    position,
  )
}

async function pane() {
  const props = reactive({
    number: 2,
    layout: 'grid',
    frame: acquiredFrame(receiverTrace()),
    measurements: [reflection],
    allMeasurements: [reflection, transmission],
    presentations: { Reflect: presentation(), Transmit: presentation() },
    activeTraceName: 'Transmit',
    primaryTraceName: 'Reflect',
    selectedDiagram: 1,
    displayRevision: revision,
    canAddTrace: true,
    canAddDiagram: true,
    config: instrumentStatus().config,
    frameCondition: { kind: 'current', label: '', detail: '' },
    canEditDisplay: true,
    commitMarker: async () => {
      throw new Error('迹线移动不能修改 Marker')
    },
  })
  const view = await sfcHarness(
    'src/components/diagrams/DiagramPane.vue',
    props,
    {
      window: { devicePixelRatio: 1 },
      requestAnimationFrame: () => 1,
      cancelAnimationFrame() {},
    },
  )
  const article = () =>
    findElements(view.render(), (node) => node.type === 'article')[0]!.props!
  return { ...view, props, article }
}

test('实际图窗五区 New Trace 事件保留目的地与方向，移动态不得提前改变 DTO', async () => {
  const view = await pane()
  try {
    const before = JSON.stringify(view.props)
    for (const position of positions) {
      const event = transfer(NEW_TRACE_DRAG_TYPE, 'new-trace', position)
      view.article().onDragover(event)
      assert.equal(event.dataTransfer.dropEffect, 'copy')
      const overlay = findComponent(view.render(), 'TraceDropZones')!
      assert.equal(overlay.props!.position, position)
      assert.equal(overlay.props!.moving, false)
      view.article().onDrop(event)
      assert.deepEqual(payload(view.emitted.at(-1)), [
        'newTrace',
        {
          diagram: 2,
          ...(position === 'center' ? {} : { position }),
        },
      ])
      assert.equal(findComponent(view.render(), 'TraceDropZones'), undefined)
    }
    assert.equal(view.emitted.length, 5)
    assert.equal(JSON.stringify(view.props), before)
  } finally {
    view.dispose()
  }
})

test('已有迹线五区移动沿用 uint64 字符串版本和身份，不复制、切选择或丢失标记', async () => {
  const view = await pane()
  try {
    const before = JSON.stringify(view.props)
    for (const position of positions) {
      const event = moving(position)
      view.article().onDragover(event)
      assert.equal(event.dataTransfer.dropEffect, 'move')
      assert.equal(
        findComponent(view.render(), 'TraceDropZones')!.props!.moving,
        true,
      )
      view.article().onDrop(event)
      assert.deepEqual(payload(view.emitted.at(-1)), [
        'moveTrace',
        {
          name: 'Transmit',
          diagram: 2,
          position,
          displayRevision: revision,
        },
      ])
    }
    assert.equal(view.emitted.length, 5)
    assert.equal(JSON.stringify(view.props), before)
  } finally {
    view.dispose()
  }
})

test('旧拖动版本、非法包和已移除迹线不能写入重编号后的图窗', async () => {
  const view = await pane()
  try {
    const frozen = moving()
    view.article().onDragover(frozen)
    view.props.displayRevision = '2'
    const before = JSON.stringify(view.props)
    const invalid = [
      frozen,
      moving('center', '2', 'RemovedTrace'),
      transfer('text/plain', 'Transmit'),
      transfer(NEW_TRACE_DRAG_TYPE, 'forged'),
      transfer(TRACE_DRAG_TYPE, '{'),
      transfer(
        TRACE_DRAG_TYPE,
        JSON.stringify({ name: 'Transmit', displayRevision: 2 }),
      ),
      transfer(
        TRACE_DRAG_TYPE,
        JSON.stringify({ name: 'Transmit', displayRevision: 'NaN' }),
      ),
    ]
    for (const event of invalid) {
      view.article().onDrop(event)
      assert.equal(findComponent(view.render(), 'TraceDropZones'), undefined)
    }
    assert.deepEqual(view.emitted, [])
    assert.equal(JSON.stringify(view.props), before)
  } finally {
    view.dispose()
  }
})

test('只读图窗拒绝 stale dragover/drop，图窗容量不足仍允许中心移动且不占测量容量', async () => {
  const view = await pane()
  try {
    view.props.canEditDisplay = false
    for (const event of [
      moving(),
      transfer(NEW_TRACE_DRAG_TYPE, 'new-trace'),
    ]) {
      view.article().onDragover(event)
      view.article().onDrop(event)
      assert.equal(event.prevented(), 0)
      assert.equal(findComponent(view.render(), 'TraceDropZones'), undefined)
    }
    assert.deepEqual(view.emitted, [])
    view.props.canEditDisplay = true
    view.props.canAddTrace = false
    view.props.canAddDiagram = false
    for (const position of positions.filter(
      (position) => position !== 'center',
    )) {
      view.article().onDrop(moving(position))
      view
        .article()
        .onDrop(transfer(NEW_TRACE_DRAG_TYPE, 'new-trace', position))
    }
    view.article().onDrop(transfer(NEW_TRACE_DRAG_TYPE, 'new-trace'))
    assert.deepEqual(view.emitted, [])
    const centered = moving()
    view.article().onDragover(centered)
    assert.equal(
      findComponent(view.render(), 'TraceDropZones')!.props!['allow-split'],
      false,
    )
    view.article().onDrop(centered)
    assert.deepEqual(payload(view.emitted), [
      [
        'moveTrace',
        {
          name: 'Transmit',
          diagram: 2,
          position: 'center',
          displayRevision: revision,
        },
      ],
    ])
    view.props.canAddTrace = true
    view.article().onDrop(transfer(NEW_TRACE_DRAG_TYPE, 'new-trace'))
    assert.deepEqual(payload(view.emitted.at(-1)), ['newTrace', { diagram: 2 }])
  } finally {
    view.dispose()
  }
})

test('App 实际控制门禁阻止 observer、待写入和设备 busy，合法移动传送冻结版本', async () => {
  const requests: Record<string, unknown>[] = []
  const view = await sfcHarness(
    'src/App.vue',
    {},
    {
      setTimeout,
      clearTimeout,
      AbortController,
      fetch: async (url: string, init?: RequestInit) => {
        if (url.endsWith('/control/claim'))
          return new Response(
            JSON.stringify({
              sessionId: 'test-lease',
              hasControl: true,
              leaseSeconds: 30,
            }),
          )
        assert.ok(url.endsWith('/display'), `移动只能请求显示入口：${url}`)
        requests.push(JSON.parse(String(init?.body)))
        return new Response(
          JSON.stringify({
            error: { code: 'Conflict', message: '测试后端拒绝' },
          }),
          { status: 409 },
        )
      },
    },
  )
  try {
    const model = view.bindings.instrument as ReturnType<typeof useInstrument>
    const status = instrumentStatus()
    model.status.value = status
    model.connected.value = true
    const request = {
      name: 'Reflect',
      diagram: 1,
      position: 'center',
      displayRevision: revision,
    }
    const handler = () =>
      findComponent(view.render(), 'DiagramWorkspace')!.props!.onMoveTrace
    await handler()(request)
    assert.equal(requests.length, 0, '未持有租约的 observer 不得写入')
    assert.equal(await model.claim(), true)
    assert.equal(model.canEditDisplay.value, true)
    model.writeInFlight.value = true
    await handler()(request)
    model.writeInFlight.value = false
    model.status.value = { ...status, pending: true }
    await handler()(request)
    assert.equal(requests.length, 0, '本地写入及设备 busy 期间旧事件不得提交')
    model.status.value = status
    const before = JSON.stringify(status)
    await handler()(request)
    assert.equal(requests.length, 1)
    assert.deepEqual(
      {
        action: requests[0]!.action,
        traceName: requests[0]!.traceName,
        diagram: requests[0]!.diagram,
        position: requests[0]!.position,
        expectedDisplayRevision: requests[0]!.expectedDisplayRevision,
      },
      {
        action: 'traceMove',
        traceName: 'Reflect',
        diagram: 1,
        position: 'center',
        expectedDisplayRevision: revision,
      },
    )
    assert.equal(
      JSON.stringify(status),
      before,
      '后端拒绝后保持测量、显示、Marker 全部保存值',
    )
  } finally {
    view.dispose()
  }
})
