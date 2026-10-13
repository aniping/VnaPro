import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive, type Ref } from 'vue'
import type { MarkerPreviewPoint } from '../src/plot/markerDrag'
import type { SelectableCurve } from '../src/plot/traceHitTest'
import {
  acquiredFrame,
  instrumentStatus,
  presentation,
  projection,
  receiverTrace,
  reflection,
  transmission,
} from './displayFixtures'
import { findElements, sfcHarness } from './sfcHarness'

interface SelectionBindings {
  plotArea: Ref<unknown>
  curves: Ref<SelectableCurve[]>
  markerPreviewPoints: (name: string) => MarkerPreviewPoint[]
}

/** 两条重合曲线属于非活动图窗，局部选择只能来自 C++ 的 primary。 */
async function rememberedDiagram() {
  const measurements = [reflection, transmission]
  const traces = measurements.map((measurement) => {
    const trace = receiverTrace(measurement, [0.1, 0.2, 0.3])
    trace.projection = projection([-24, -20, -22])
    return trace
  })
  const frame = acquiredFrame(traces[0])
  frame.traces = traces
  const props = reactive({
    number: 1,
    layout: 'grid',
    frame,
    measurements,
    allMeasurements: measurements,
    presentations: Object.fromEntries(
      measurements.map((measurement) => [measurement.name, presentation()]),
    ),
    activeTraceName: 'OtherDiagramTrace',
    primaryTraceName: 'Reflect',
    selectedDiagram: 2,
    config: instrumentStatus().config,
    frameCondition: { kind: 'current', label: '', detail: '' },
    canEditDisplay: true,
    commitMarker: async () => true,
  })
  let paintRequests = 0
  const harness = await sfcHarness(
    'src/components/diagrams/DiagramPane.vue',
    props,
    {
      window: { devicePixelRatio: 1 },
      requestAnimationFrame: () => ++paintRequests,
      cancelAnimationFrame() {},
    },
  )
  const bindings = harness.bindings as unknown as SelectionBindings
  bindings.plotArea.value = {
    getBoundingClientRect: () => ({ left: 0, top: 0, width: 700, height: 500 }),
  }
  function strips() {
    return findElements(
      harness.render(),
      (node) =>
        typeof node.type === 'object' &&
        'name' in node.type &&
        node.type.name === 'TraceStrip',
    )
  }
  return {
    ...harness,
    props,
    bindings,
    strips,
    paintRequests: () => paintRequests,
  }
}

test('非活动图窗保留局部灰色选中，激活该窗后同一自然标题槽变为蓝色', async () => {
  const pane = await rememberedDiagram()
  let title: Awaited<ReturnType<typeof sfcHarness>> | undefined
  try {
    const before = JSON.stringify(pane.props)
    const remembered = pane.strips()[0]!
    assert.equal(remembered.props!.active, false)
    assert.equal(remembered.props!.remembered, true)
    assert.equal(pane.strips()[1]!.props!.remembered, false)
    title = await sfcHarness(
      'src/components/diagrams/TraceStrip.vue',
      {
        ...remembered.props,
        canEditDisplay: remembered.props!['can-edit-display'],
        smithRadius: remembered.props!['smith-radius'],
      },
      {},
    )
    assert.ok(
      String(title.render().props!.class).split(/\s+/).includes('remembered'),
    )
    const button = findElements(
      title.render(),
      (node) => node.type === 'button',
    )[0]!
    assert.equal(button.props!['aria-pressed'], true)
    button.props!.onClick()
    assert.deepEqual(title.emitted, [['select', 'Reflect']])
    assert.equal(JSON.stringify(pane.props), before)
    pane.props.selectedDiagram = 1
    pane.props.activeTraceName = 'Reflect'
    assert.equal(pane.strips()[0]!.props!.active, true)
    assert.equal(pane.strips()[0]!.props!.remembered, true)
    assert.equal(pane.strips()[1]!.props!.active, false)
  } finally {
    title?.dispose()
    pane.dispose()
  }
})

test('非活动图窗重合曲线优先命中局部选择，点击仅请求后端切换活动迹线', async () => {
  const pane = await rememberedDiagram()
  try {
    const before = JSON.stringify(pane.props)
    const [first, second] = pane.bindings.markerPreviewPoints('Reflect')
    const plot = findElements(
      pane.render(),
      (node) => node.type === 'div' && node.props?.class === 'plot-area',
    )[0]!
    plot.props!.onClick({
      button: 0,
      clientX: (first!.x + second!.x) / 2,
      clientY: (first!.y + second!.y) / 2,
      target: { closest: () => null },
    })
    assert.deepEqual(pane.emitted, [['selectTrace', 'Reflect']])
    assert.deepEqual(
      Array.from(pane.bindings.curves.value, (curve) => [
        curve.name,
        curve.active,
      ]),
      [
        ['Reflect', true],
        ['Transmit', false],
      ],
    )
    assert.equal(JSON.stringify(pane.props), before)
  } finally {
    pane.dispose()
  }
})

test('后端删除靠前的非局部选择迹线后，剩余局部选择不因颜色位置变化而丢失', async () => {
  const pane = await rememberedDiagram()
  try {
    pane.props.primaryTraceName = 'Transmit'
    const oldColor = pane.strips()[1]!.props!.color
    pane.props.measurements = [transmission]
    const remaining = pane.strips()[0]!
    assert.equal(remaining.props!.measurement.name, 'Transmit')
    assert.equal(remaining.props!.remembered, true)
    assert.equal(remaining.props!.active, false)
    assert.notEqual(
      remaining.props!.color,
      oldColor,
      '默认颜色仍遵循图窗成员顺序',
    )
    assert.equal(pane.props.primaryTraceName, 'Transmit')
    assert.equal(pane.props.activeTraceName, 'OtherDiagramTrace')
    assert.deepEqual(pane.emitted, [])
  } finally {
    pane.dispose()
  }
})

test('只有局部 primary 改变也重新绘图，无需改变全局活动迹线或等待新采集帧', async () => {
  const pane = await rememberedDiagram()
  try {
    await nextTick()
    const before = pane.paintRequests()
    pane.props.primaryTraceName = 'Transmit'
    await nextTick()
    assert.ok(pane.paintRequests() > before)
    assert.equal(pane.props.activeTraceName, 'OtherDiagramTrace')
    assert.deepEqual(
      Array.from(pane.bindings.curves.value, (curve) => [
        curve.name,
        curve.active,
      ]),
      [
        ['Reflect', false],
        ['Transmit', true],
      ],
    )
  } finally {
    pane.dispose()
  }
})
