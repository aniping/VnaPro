import assert from 'node:assert/strict'
import test from 'node:test'
import { reactive } from 'vue'
import { NEW_TRACE_DRAG_TYPE } from '../src/components/instrument/newTraceDrag'
import {
  beginTraceDrag,
  isTraceDrag,
  traceDragIntent,
  traceDropPosition,
  TRACE_DRAG_TYPE,
} from '../src/components/diagrams/traceDrag'
import { presentation, reflection } from './displayFixtures'
import { findElements, sfcHarness } from './sfcHarness'
import { trashDropIntent, TRASH_DRAG_TYPE } from '../src/domain/trashDrop'

function dragEvent(type?: string, data = '') {
  const values = new Map(type ? [[type, data]] : [])
  let prevented = 0
  return {
    dataTransfer: {
      types: type ? [type] : [],
      effectAllowed: '',
      getData: (key: string) => values.get(key) ?? '',
      setData(key: string, value: string) {
        values.set(key, value)
        if (!this.types.includes(key)) this.types.push(key)
      },
    },
    preventDefault() {
      prevented += 1
    },
    prevented: () => prevented,
  }
}

test('既有迹线的拖动冻结显示版本并使用移动令牌，不冒充工具栏创建令牌', () => {
  const event = dragEvent()
  const source = { name: 'Trc5', displayRevision: '18446744073709551615' }
  beginTraceDrag(event as unknown as DragEvent, source, true)
  source.name = 'Trc6'
  source.displayRevision = '99'
  assert.equal(event.dataTransfer.effectAllowed, 'move')
  assert.equal(event.prevented(), 0)
  assert.equal(isTraceDrag(event as unknown as DragEvent), true)
  assert.equal(event.dataTransfer.types.includes(NEW_TRACE_DRAG_TYPE), false)
  assert.deepEqual(traceDragIntent(event as unknown as DragEvent), {
    kind: 'move',
    name: 'Trc5',
    displayRevision: '18446744073709551615',
  })
  const creating = dragEvent(NEW_TRACE_DRAG_TYPE, 'new-trace')
  assert.equal(isTraceDrag(creating as unknown as DragEvent), false)
  assert.deepEqual(traceDragIntent(creating as unknown as DragEvent), {
    kind: 'new',
  })
})

test('只读、无显示版本及外来传输包不能移动迹线', () => {
  for (const [source, enabled] of [
    [{ name: 'Trc1', displayRevision: '7' }, false],
    [{ name: 'Trc1', displayRevision: '' }, true],
    [{ name: '', displayRevision: '7' }, true],
  ] as const) {
    const event = dragEvent()
    beginTraceDrag(event as unknown as DragEvent, source, enabled)
    assert.equal(event.prevented(), 1)
    assert.deepEqual(event.dataTransfer.types, [])
  }
  for (const event of [
    dragEvent('text/plain', 'Trc1'),
    dragEvent(NEW_TRACE_DRAG_TYPE, 'Trc1'),
    dragEvent(TRACE_DRAG_TYPE, '{'),
    dragEvent(TRACE_DRAG_TYPE, 'null'),
    dragEvent(TRACE_DRAG_TYPE, JSON.stringify({ name: 'Trc1' })),
    dragEvent(
      TRACE_DRAG_TYPE,
      JSON.stringify({ name: 'Trc1', displayRevision: 7 }),
    ),
    dragEvent(
      TRACE_DRAG_TYPE,
      JSON.stringify({ name: 'Trc1', displayRevision: 'NaN' }),
    ),
  ])
    assert.equal(traceDragIntent(event as unknown as DragEvent), null)
})

test('迹线删除令牌单独冻结双版本，旧移动包保持原合同且缺版本时不允许删除', () => {
  const event = dragEvent()
  const source = { name: 'Inactive', revision: '12', displayRevision: '34' }
  beginTraceDrag(event as unknown as DragEvent, source, true)
  source.name = 'Active'
  source.revision = '13'
  assert.deepEqual(trashDropIntent(event as unknown as DragEvent), {
    kind: 'trace', name: 'Inactive', revision: '12', displayRevision: '34',
  })
  assert.deepEqual(JSON.parse(event.dataTransfer.getData(TRACE_DRAG_TYPE)), {
    name: 'Inactive', displayRevision: '34',
  })
  const legacy = dragEvent()
  beginTraceDrag(legacy as unknown as DragEvent, { name: 'Inactive', displayRevision: '34' }, true)
  assert.equal(isTraceDrag(legacy as unknown as DragEvent), true)
  assert.equal(legacy.dataTransfer.types.includes(TRASH_DRAG_TYPE), false)
})

test('五个图窗落区随宽高缩放，中心归入原窗，四边保持实际分割方向', () => {
  const probes = [
    [0.5, 0.5, 'center'],
    [0.5, 0.1, 'above'],
    [0.5, 0.9, 'below'],
    [0.1, 0.5, 'left'],
    [0.9, 0.5, 'right'],
    [0.25, 0.1, 'above'],
    [0.1, 0.25, 'left'],
  ] as const
  for (const [width, height] of [
    [900, 180],
    [280, 500],
    [0.5, 0.5],
  ]) {
    for (const [x, y, expected] of probes)
      assert.equal(
        traceDropPosition(x * width!, y * height!, width!, height!),
        expected,
      )
    assert.equal(
      traceDropPosition(width! / 2, height! / 2, width!, height!, false),
      'center',
    )
    for (const [x, y] of probes.filter((probe) => probe[2] !== 'center'))
      assert.equal(
        traceDropPosition(x * width!, y * height!, width!, height!, false),
        null,
      )
  }
  for (const geometry of [
    [-1, 50, 100, 100],
    [101, 50, 100, 100],
    [50, -1, 100, 100],
    [50, 101, 100, 100],
    [50, 50, 0, 100],
    [50, 50, 100, -1],
    [NaN, 50, 100, 100],
    [50, 50, Infinity, 100],
  ])
    assert.equal(
      traceDropPosition(geometry[0]!, geometry[1]!, geometry[2]!, geometry[3]!),
      null,
    )
})

test('真实迹线标题提供拖动但不提前发送选择、创建或修改标记动作', async () => {
  const props = reactive({
    measurement: reflection,
    presentation: presentation(),
    scale: { min: -100, max: 0 },
    color: '#8ee31d',
    active: false,
    smithRadius: 1,
    canEditDisplay: true,
    displayRevision: '17',
    revision: '12',
  })
  const view = await sfcHarness(
    'src/components/diagrams/TraceStrip.vue',
    props,
    {},
  )
  try {
    const button = () =>
      findElements(view.render(), (node) => node.type === 'button')[0]!.props!
    const before = JSON.stringify(props)
    assert.equal(button().draggable, true)
    const event = dragEvent()
    button().onDragstart(event)
    assert.deepEqual(traceDragIntent(event as unknown as DragEvent), {
      kind: 'move',
      name: 'Reflect',
      displayRevision: '17',
    })
    assert.deepEqual(trashDropIntent(event as unknown as DragEvent), {
      kind: 'trace', name: 'Reflect', revision: '12', displayRevision: '17',
    })
    assert.deepEqual(view.emitted, [])
    assert.equal(JSON.stringify(props), before)
    props.canEditDisplay = false
    assert.equal(button().draggable, false)
    const blocked = dragEvent()
    button().onDragstart(blocked)
    assert.equal(blocked.prevented(), 1)
    assert.deepEqual(blocked.dataTransfer.types, [])
    assert.deepEqual(view.emitted, [])
  } finally {
    view.dispose()
  }
})

test('五区提示仅呈现可用落区，移动意图不显示创建语义', async () => {
  const props = reactive({ position: 'left', allowSplit: true, moving: true })
  const view = await sfcHarness(
    'src/components/diagrams/TraceDropZones.vue',
    props,
    {},
  )
  try {
    assert.equal(
      findElements(view.render(), (node) => node.type === 'polygon').length,
      5,
    )
    const labels = findElements(view.render(), (node) => node.type === 'span')
    assert.equal(labels.length, 5)
    assert.ok(
      labels.some((node) => String(node.children).includes('Move trace')),
    )
    assert.ok(
      labels.every(
        (node) => !String(node.children).toLowerCase().includes('add trace'),
      ),
    )
    props.allowSplit = false
    assert.equal(
      findElements(view.render(), (node) => node.type === 'polygon').length,
      1,
    )
    assert.equal(
      findElements(view.render(), (node) => node.type === 'span').length,
      1,
    )
    props.moving = false
    assert.ok(
      String(
        findElements(view.render(), (node) => node.type === 'span')[0]!
          .children,
      ).includes('Add trace'),
    )
  } finally {
    view.dispose()
  }
})
