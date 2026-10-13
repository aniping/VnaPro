import assert from 'node:assert/strict'
import test from 'node:test'
import { reactive, type VNode } from 'vue'
import type { InstrumentStatus, TraceCreationOptions } from '../src/api/types'
import {
  NEW_TRACE_DRAG_TYPE,
  isNewTraceDrag,
} from '../src/components/instrument/newTraceDrag'
import { MARKER_INFO_DRAG_TYPE } from '../src/domain/markerReadouts'
import {
  acquiredFrame,
  instrumentStatus,
  presentation,
  receiverTrace,
  reflection,
  transmission,
} from './displayFixtures'
import { findComponent, findElements, sfcHarness } from './sfcHarness'

interface CreationRequest {
  options: TraceCreationOptions
  revision: string
  displayRevision: string
}

function workflowProps(portCount = 4) {
  const status = instrumentStatus()
  status.revision = '31'
  status.display.revision = '71'
  status.capabilities.portCount = portCount
  status.display.diagrams.push({
    number: 2,
    measurementNames: [],
    primaryMeasurement: '',
  })
  return reactive({
    status,
    destination: { diagram: 2 },
    disabled: false,
    create: async (
      _options: TraceCreationOptions,
      _revision: string,
      _displayRevision: string,
    ): Promise<InstrumentStatus | null> => null,
  })
}

function dialog(view: Awaited<ReturnType<typeof sfcHarness>>) {
  const child = findComponent(view.render(), 'SParameterDialog')
  assert.ok(child, 'New Trace 必须通过实际矩阵弹框发出选择动作')
  return child.props!
}

test('New Trace 选 S44 只提交一次显式目标和打开时双版本，待结果期间不能重入或关闭', async () => {
  const props = workflowProps()
  const requests: CreationRequest[] = []
  let finish: (value: InstrumentStatus | null) => void = () => {}
  props.create = (options, revision, displayRevision) => {
    requests.push({ options: { ...options }, revision, displayRevision })
    return new Promise((resolve) => {
      finish = resolve
    })
  }
  const view = await sfcHarness(
    'src/components/softtools/NewTraceWorkflow.vue',
    props,
    {},
  )
  try {
    const before = JSON.stringify(props.status)
    const opened = dialog(view)
    assert.equal(opened.purpose, 'new')
    assert.equal(opened['port-count'], 4)
    assert.equal(requests.length, 0, '打开弹框不能先创建默认迹线')
    props.destination.diagram = 1
    const creating = opened.onSelect('S44')
    assert.deepEqual(requests, [
      {
        options: { diagram: 2, parameter: 'S44' },
        revision: '31',
        displayRevision: '71',
      },
    ])
    assert.equal(dialog(view).disabled, true)
    assert.equal(dialog(view).busy, true)
    await opened.onSelect('S11')
    opened.onClose()
    assert.equal(requests.length, 1)
    assert.deepEqual(view.emitted, [])
    assert.equal(JSON.stringify(props.status), before)
    finish(instrumentStatus())
    await creating
    assert.deepEqual(view.emitted, [['close']])
    assert.equal(
      JSON.stringify(props.status),
      before,
      '成功仍等待外层后端快照读回，不直接写 DTO',
    )
  } finally {
    finish(null)
    view.dispose()
  }
})

test('后端拒绝 New Trace 保留弹框和保存状态，用户关闭不产生第二次请求', async () => {
  const props = workflowProps()
  let calls = 0
  props.create = async () => {
    calls += 1
    return null
  }
  const view = await sfcHarness(
    'src/components/softtools/NewTraceWorkflow.vue',
    props,
    {},
  )
  try {
    const before = JSON.stringify(props.status)
    await dialog(view).onSelect('S34')
    assert.equal(calls, 1)
    assert.deepEqual(view.emitted, [])
    assert.ok(dialog(view).error)
    assert.equal(dialog(view).busy, false)
    assert.equal(JSON.stringify(props.status), before)
    dialog(view).onClose()
    assert.deepEqual(view.emitted, [['close']])
    assert.equal(calls, 1)
  } finally {
    view.dispose()
  }
})

test('二端口非法 S44 和只读旧事件不创建迹线，未提交弹框允许直接关闭', async () => {
  const props = workflowProps(2)
  let calls = 0
  props.create = async () => {
    calls += 1
    return null
  }
  const view = await sfcHarness(
    'src/components/softtools/NewTraceWorkflow.vue',
    props,
    {},
  )
  try {
    const before = JSON.stringify(props.status)
    const opened = dialog(view)
    assert.equal(opened['port-count'], 2)
    await opened.onSelect('S44')
    props.disabled = true
    await opened.onSelect('S11')
    assert.equal(dialog(view).disabled, true)
    assert.equal(calls, 0)
    assert.equal(JSON.stringify(props.status), before)
    opened.onClose()
    assert.deepEqual(view.emitted, [['close']])
  } finally {
    view.dispose()
  }
})

test('目录或显示版本变化后旧弹框不能写入重编号图窗，但仍可关闭', async () => {
  for (const changed of ['measurement', 'display'] as const) {
    const props = workflowProps()
    let calls = 0
    props.create = async () => {
      calls += 1
      return null
    }
    const view = await sfcHarness(
      'src/components/softtools/NewTraceWorkflow.vue',
      props,
      {},
    )
    try {
      const opened = dialog(view)
      if (changed === 'measurement') props.status.revision = '32'
      else {
        props.status.display.revision = '72'
        props.status.display.diagrams[1]!.measurementNames = ['Reflect']
      }
      const current = JSON.stringify(props.status)
      assert.equal(dialog(view).disabled, true)
      assert.ok(dialog(view).error, '版本冲突必须显示重新打开的原因')
      await opened.onSelect('S44')
      assert.equal(calls, 0)
      assert.equal(JSON.stringify(props.status), current)
      opened.onClose()
      assert.deepEqual(view.emitted, [['close']])
    } finally {
      view.dispose()
    }
  }
})

/** 只模拟浏览器传输包，实际 Toolbar / DiagramPane 的模板执行拖放控制。 */
function dragEvent(type?: string, value = 'new-trace') {
  const values = new Map(type ? [[type, value]] : [])
  let prevented = 0
  return {
    dataTransfer: {
      types: type ? [type] : [],
      effectAllowed: '',
      dropEffect: '',
      getData: (key: string) => values.get(key) ?? '',
      setData(key: string, data: string) {
        values.set(key, data)
        if (!this.types.includes(key)) this.types.push(key)
      },
    },
    preventDefault() {
      prevented += 1
    },
    prevented: () => prevented,
  }
}

function control(render: () => VNode, id: string) {
  const node = findElements(
    render(),
    (item) => item.props?.['data-testid'] === id,
  )[0]
  assert.ok(node, `缺少真实模板控件 ${id}`)
  return node.props!
}

test('工具栏单击保持复制意图，拖拽只设置独立新建令牌不发出创建动作', async () => {
  const props = reactive({
    layout: 'single',
    hasTrace: true,
    canAutoscale: true,
    canEditDisplay: true,
    markerEnabled: false,
    canExport: true,
    canWrite: true,
    busy: false,
    canDelete: true,
    canAddTrace: true,
  })
  const view = await sfcHarness(
    'src/components/instrument/InstrumentToolbar.vue',
    props,
    {},
  )
  try {
    const button = control(view.render, 'toolbar-add-trace')
    button.onClick()
    assert.deepEqual(view.emitted, [['addTrace']])
    const dragging = dragEvent()
    assert.equal(button.draggable, true)
    button.onDragstart(dragging)
    assert.equal(
      dragging.dataTransfer.getData(NEW_TRACE_DRAG_TYPE),
      'new-trace',
    )
    assert.equal(dragging.dataTransfer.effectAllowed, 'copy')
    assert.equal(isNewTraceDrag(dragging as unknown as DragEvent), true)
    assert.deepEqual(view.emitted, [['addTrace']], '拖拽时不复制或创建默认迹线')
    assert.equal(
      isNewTraceDrag(dragEvent('text/plain') as unknown as DragEvent),
      false,
    )
    for (const mode of ['readonly', 'busy'] as const) {
      props.canAddTrace = mode !== 'readonly'
      props.canWrite = mode !== 'readonly'
      props.busy = mode === 'busy'
      const blocked = control(view.render, 'toolbar-add-trace')
      assert.equal(blocked.disabled, true)
      assert.equal(blocked.draggable, false)
      const attempted = dragEvent()
      blocked.onDragstart(attempted)
      blocked.onClick()
      assert.equal(attempted.prevented(), 1)
      assert.deepEqual(attempted.dataTransfer.types, [])
      assert.deepEqual(view.emitted, [['addTrace']])
    }
  } finally {
    view.dispose()
  }
})

async function diagram() {
  const status = instrumentStatus()
  const saved = presentation()
  saved.marker = {
    enabled: true,
    frequencyHz: 10e6,
    sampleIndex: 0,
    discrete: true,
  }
  const props = reactive({
    number: 2,
    layout: 'grid',
    frame: acquiredFrame(receiverTrace()),
    measurements: [reflection],
    allMeasurements: [reflection, transmission],
    presentations: { Reflect: saved },
    activeTraceName: 'Transmit',
    primaryTraceName: 'Reflect',
    selectedDiagram: 1,
    canAddTrace: true,
    config: status.config,
    frameCondition: { kind: 'current', label: '', detail: '' },
    canEditDisplay: true,
    commitMarker: async () => {
      throw new Error('拖放参数入口不能修改 Marker')
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
  const pane = () =>
    findElements(view.render(), (node) => node.type === 'article')[0]!.props!
  return { ...view, props, pane }
}

test('新迹线拖放只打开明确目标图窗的参数框，Marker Info 拖放仍执行原迁移意图', async () => {
  const view = await diagram()
  try {
    const before = JSON.stringify(view.props)
    const newTrace = dragEvent(NEW_TRACE_DRAG_TYPE)
    view.pane().onDragover(newTrace)
    assert.equal(newTrace.dataTransfer.dropEffect, 'copy')
    view.pane().onDrop(newTrace)
    assert.deepEqual(view.emitted, [['newTrace', 2]])
    assert.equal(JSON.stringify(view.props), before)
    const markerInfo = dragEvent(MARKER_INFO_DRAG_TYPE, 'Reflect')
    view.pane().onDragover(markerInfo)
    assert.equal(markerInfo.dataTransfer.dropEffect, 'move')
    view.pane().onDrop(markerInfo)
    assert.deepEqual(JSON.parse(JSON.stringify(view.emitted)), [
      ['newTrace', 2],
      ['relocateMarkerInfo', { name: 'Reflect', location: 'diagram' }],
    ])
    assert.equal(
      JSON.stringify(view.props),
      before,
      '两种拖放不能本地改变选择、标记或归属',
    )
  } finally {
    view.dispose()
  }
})

test('图窗拒绝外部拖放、伪造令牌及只读或容量不足的新迹线意图', async () => {
  const view = await diagram()
  try {
    const before = JSON.stringify(view.props)
    for (const event of [
      dragEvent('text/plain'),
      dragEvent(NEW_TRACE_DRAG_TYPE, 'invalid'),
    ])
      view.pane().onDrop(event)
    for (const mode of ['readonly', 'capacity'] as const) {
      view.props.canEditDisplay = mode !== 'readonly'
      view.props.canAddTrace = mode !== 'capacity'
      const attempt = dragEvent(NEW_TRACE_DRAG_TYPE)
      view.pane().onDragover(attempt)
      view.pane().onDrop(attempt)
      assert.equal(attempt.prevented(), 0)
    }
    assert.deepEqual(view.emitted, [])
    view.props.canEditDisplay = true
    view.props.canAddTrace = true
    assert.equal(JSON.stringify(view.props), before)
  } finally {
    view.dispose()
  }
})
