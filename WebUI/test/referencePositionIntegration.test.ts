import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive, type Ref } from 'vue'
import type { DisplayScale } from '../src/domain/presentation'
import type { SelectableCurve } from '../src/plot/traceHitTest'
import type { useInstrument } from '../src/composables/useInstrument'
import {
  acquiredFrame,
  instrumentStatus,
  presentation,
  receiverTrace,
  reflection,
  transmission,
} from './displayFixtures'
import { findComponent, findElements, sfcHarness } from './sfcHarness'

async function diagram() {
  const trace = receiverTrace()
  const views = { Reflect: presentation(), Transmit: presentation('smith') }
  const props = reactive({
    number: 1,
    layout: 'single',
    frame: acquiredFrame(trace),
    measurements: [reflection, transmission],
    allMeasurements: [reflection, transmission],
    presentations: views,
    activeTraceName: 'Reflect',
    primaryTraceName: 'Reflect',
    selectedDiagram: 1,
    displayRevision: '7',
    config: instrumentStatus().config,
    frameCondition: { kind: 'current', label: '', detail: '' },
    canEditDisplay: true,
    commitMarker: async () => {
      throw new Error('参考位置不能修改 Marker')
    },
    commitReferencePosition: async () => true,
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
  const handles = () =>
    findElements(
      view.render(),
      (node) =>
        typeof node.type === 'object' &&
        Reflect.get(node.type, 'name') === 'ReferencePositionHandle',
    )
  const bindings = view.bindings as unknown as {
    axisScale: Ref<DisplayScale>
    curves: Ref<SelectableCurve[]>
  }
  return { ...view, props, handles, bindings }
}

test('参考三角仅覆盖可见直角坐标迹线，圆图、隐藏迹线和空图不产生拖动对象', async () => {
  const view = await diagram()
  try {
    assert.equal(view.handles().length, 1, '必须从实际模板接入参考位置手势')
    assert.equal(view.handles()[0]!.props!.name, 'Reflect')
    view.props.presentations.Reflect.visible = false
    await nextTick()
    assert.equal(view.handles().length, 0)
    view.props.measurements = []
    await nextTick()
    assert.equal(view.handles().length, 0)
  } finally {
    view.dispose()
  }
})

test('参考位置预览移动坐标和曲线，保留投影数组、参考值和后端 DTO，取消恢复', async () => {
  const view = await diagram()
  try {
    const handle = view.handles()[0]
    assert.ok(handle, '必须从实际模板接入参考位置手势')
    const before = JSON.stringify(view.props)
    const savedScale = { ...view.bindings.axisScale.value }
    const curve = view.bindings.curves.value.find(
      (item) => item.kind === 'cartesian',
    )!
    assert.equal(curve.kind, 'cartesian')
    const values = curve.values
    const oldPosition = savedScale.referencePosition ?? 8
    handle.props!.onActive(true)
    handle.props!.onPreview({
      name: 'Reflect',
      referencePosition: oldPosition - 2,
    })
    await nextTick()
    const scale = view.bindings.axisScale.value
    const perDivision = (savedScale.max - savedScale.min) / 10
    assert.equal(scale.referencePosition, oldPosition - 2)
    assert.equal(scale.max - scale.min, savedScale.max - savedScale.min)
    assert.equal(
      scale.min + scale.referencePosition! * perDivision,
      savedScale.min + oldPosition * perDivision,
    )
    const moved = view.bindings.curves.value.find(
      (item) => item.kind === 'cartesian',
    )!
    assert.equal(moved.kind, 'cartesian')
    assert.equal(moved.values, values, '指针预览不得重新复制或转换 4000 点投影')
    assert.equal(JSON.stringify(view.props), before)
    handle.props!.onPreview(null)
    await nextTick()
    assert.deepEqual(view.bindings.axisScale.value, savedScale)
    assert.deepEqual(view.emitted, [])
  } finally {
    view.dispose()
  }
})

test('Workspace 原样传递参考位置提交回调、显示版本和写权限', async () => {
  const status = instrumentStatus()
  const commit = async () => true
  const view = await sfcHarness(
    'src/components/diagrams/DiagramWorkspace.vue',
    {
      ...status.display,
      frame: null,
      measurements: status.measurements,
      activeTraceName: status.display.selectedMeasurement,
      displayRevision: '17',
      config: status.config,
      frameCondition: { kind: 'empty', label: '', detail: '' },
      canEditDisplay: false,
      commitMarker: async () => false,
      commitReferencePosition: commit,
    },
    {},
  )
  try {
    const pane = findComponent(view.render(), 'DiagramPane')!.props!
    assert.equal(pane['commit-reference-position'], commit)
    assert.equal(pane['display-revision'], '17')
    assert.equal(pane['can-edit-display'], false)
  } finally {
    view.dispose()
  }
})

test('App 参考位置只提交目标迹线的 Ref Pos 和冻结版本，observer/busy 不写入', async () => {
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
        assert.ok(url.endsWith('/display'))
        requests.push(JSON.parse(String(init?.body)))
        return new Response(
          JSON.stringify({
            error: { code: 'Conflict', message: 'stale gesture' },
          }),
          { status: 409 },
        )
      },
    },
  )
  try {
    const instrument = view.bindings.instrument as ReturnType<
      typeof useInstrument
    >
    const status = instrumentStatus()
    instrument.status.value = status
    instrument.connected.value = true
    const callback = () =>
      findComponent(view.render(), 'DiagramWorkspace')!.props![
        'commit-reference-position'
      ]
    const command = {
      name: 'Reflect',
      referencePosition: 5.75,
      displayRevision: '3',
    }
    assert.equal(await callback()(command), false)
    assert.equal(requests.length, 0)
    assert.equal(await instrument.claim(), true)
    instrument.writeInFlight.value = true
    assert.equal(await callback()(command), false)
    instrument.writeInFlight.value = false
    instrument.status.value = { ...status, pending: true }
    assert.equal(await callback()(command), false)
    instrument.status.value = status
    const before = JSON.stringify(status)
    assert.equal(
      await callback()(command),
      false,
      '冲突读回应由手势恢复权威刻度',
    )
    assert.equal(requests.length, 1)
    const { requestId, ...body } = requests[0]!
    assert.equal(typeof requestId, 'string')
    assert.deepEqual(
      body,
      {
        action: 'scale',
        traceName: 'Reflect',
        referencePosition: 5.75,
        expectedDisplayRevision: '3',
      },
      '不得用旧手势中的完整刻度覆盖当前 Ref Value/ScaleDiv',
    )
    assert.equal(JSON.stringify(instrument.status.value), before)
    instrument.stop()
  } finally {
    view.dispose()
  }
})
