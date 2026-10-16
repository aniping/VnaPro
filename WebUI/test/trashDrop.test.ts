import assert from 'node:assert/strict'
import test from 'node:test'
import { reactive, type VNode } from 'vue'
import {
  beginTrashDrag, hitTrashTarget, isTrashDrag, trashDropIntent, TRASH_DRAG_TYPE,
  type TrashIntent,
} from '../src/domain/trashDrop'
import { findElements, sfcHarness } from './sfcHarness'

function dragEvent(value?: unknown, type = TRASH_DRAG_TYPE) {
  const values = new Map<string, string>(value === undefined ? [] : [[type, JSON.stringify(value)]])
  let prevented = 0
  let stopped = 0
  return {
    dataTransfer: {
      types: [...values.keys()], effectAllowed: '', dropEffect: '',
      getData(key: string) { return values.get(key) ?? '' },
      setData(key: string, data: string) { values.set(key, data); if (!this.types.includes(key)) this.types.push(key) },
    },
    preventDefault() { prevented += 1 }, stopPropagation() { stopped += 1 },
    prevented: () => prevented, stopped: () => stopped,
  }
}

const source: TrashIntent = { kind: 'trace', name: 'Trc7', revision: '12', displayRevision: '34' }

test('trash token freezes an explicit source identity and does not masquerade as create or move', () => {
  const event = dragEvent()
  const trace = { ...source }
  assert.equal(beginTrashDrag(event as unknown as DragEvent, trace, true), true)
  trace.name = 'Trc1'
  assert.deepEqual(trashDropIntent(event as unknown as DragEvent), source)
  assert.equal(event.dataTransfer.effectAllowed, 'move')
  assert.equal(event.prevented(), 0)
  assert.deepEqual(event.dataTransfer.types, [TRASH_DRAG_TYPE])
  for (const type of ['application/x-vnapro-trace', 'application/x-vnapro-new-trace', 'text/plain']) {
    const other = dragEvent(source, type)
    assert.equal(isTrashDrag(other as unknown as DragEvent), false)
    assert.equal(trashDropIntent(other as unknown as DragEvent), null)
  }
  event.dataTransfer.types.push('application/x-vnapro-new-trace')
  assert.equal(trashDropIntent(event as unknown as DragEvent), null)
})

test('trash parser accepts only bounded object identities and exact versioned payloads', () => {
  for (const intent of [source,
    { kind: 'marker', name: 'Trc2', markerId: 10, displayRevision: '18446744073709551615' },
    { kind: 'markers', name: 'Trc3', displayRevision: '0' },
    { kind: 'diagram', diagram: 128, revision: '12', displayRevision: '34' },
  ]) assert.deepEqual(trashDropIntent(dragEvent(intent) as unknown as DragEvent), intent)
  for (const invalid of [null, [], {}, { ...source, kind: 'new' }, { ...source, extra: true },
    { ...source, name: '' }, { ...source, name: '..' }, { ...source, name: 'Trc/1' },
    { ...source, name: 'a'.repeat(129) }, { ...source, revision: '-1' },
    { ...source, revision: '18446744073709551616' }, { ...source, displayRevision: 34 },
    { ...source, displayRevision: '' }, { ...source, displayRevision: '1.5' },
    { kind: 'marker', name: 'Trc1', displayRevision: '34' },
    ...[0, 11, '2'].map((markerId) => ({ kind: 'marker', name: 'Trc1', markerId, displayRevision: '34' })),
    { kind: 'markers', name: 'Trc1', markerId: 1, displayRevision: '34' },
    ...[0, 129, 1.5, '2'].map((diagram) => ({ kind: 'diagram', diagram, revision: '12', displayRevision: '34' })),
  ]) assert.equal(trashDropIntent(dragEvent(invalid) as unknown as DragEvent), null, JSON.stringify(invalid))
  const malformed = dragEvent(source)
  malformed.dataTransfer.setData(TRASH_DRAG_TYPE, '{')
  assert.equal(trashDropIntent(malformed as unknown as DragEvent), null)
  const blocked = dragEvent()
  assert.equal(beginTrashDrag(blocked as unknown as DragEvent, source, false), false)
  assert.deepEqual(blocked.dataTransfer.types, [])
})

test('pointer trash hit testing uses enabled toolbar bounds in screen CSS pixels', () => {
  const saved = Object.getOwnPropertyDescriptor(globalThis, 'document')
  let enabled = 'true'
  const bounds = { left: 120, top: 25, right: 154, bottom: 59, width: 34, height: 34 }
  Object.defineProperty(globalThis, 'document', { configurable: true, value: {
    querySelector(selector: string) {
      assert.equal(selector, '[data-testid="trash-target"]')
      return { getAttribute: () => enabled, getBoundingClientRect: () => bounds }
    },
  } })
  try {
    assert.equal(hitTrashTarget(120, 25), true)
    assert.equal(hitTrashTarget(154, 59), true)
    assert.equal(hitTrashTarget(119, 40), false)
    assert.equal(hitTrashTarget(140, 60), false)
    assert.equal(hitTrashTarget(NaN, 40), false)
    enabled = 'false'
    assert.equal(hitTrashTarget(140, 40), false)
    enabled = 'true'
    bounds.width = 0
    assert.equal(hitTrashTarget(140, 40), false)
  } finally {
    if (saved) Object.defineProperty(globalThis, 'document', saved)
    else Reflect.deleteProperty(globalThis, 'document')
  }
})

function toolbarProps() {
  return reactive({
    layout: 'grid', hasTrace: true, canAutoscale: true, canEditDisplay: true,
    markerEnabled: false, canExport: true, canWrite: true, busy: false,
    canDelete: true, canAddTrace: true, revision: '12' as string | undefined,
    displayRevision: '34' as string | undefined,
  })
}

function control(render: () => VNode, id: string) {
  const node = findElements(render(), (item) => item.props?.['data-testid'] === id)[0]
  assert.ok(node, `Missing ${id}`)
  return node.props!
}

test('toolbar drop names the dragged inactive object and accepts markers even on the last trace', async () => {
  const props = toolbarProps()
  const view = await sfcHarness('src/components/instrument/InstrumentToolbar.vue', props, {})
  try {
    const before = JSON.stringify(props)
    const protectedEvent = dragEvent(source)
    protectedEvent.dataTransfer.getData = () => assert.fail('dragover cannot read protected drag data')
    control(view.render, 'trash-target').onDragover(protectedEvent)
    assert.equal(protectedEvent.prevented(), 1)
    assert.equal(protectedEvent.dataTransfer.dropEffect, 'move')
    assert.match(String(control(view.render, 'trash-target').class), /is-dropping/)
    for (const intent of [source, { kind: 'diagram', diagram: 3, revision: '12', displayRevision: '34' }]) {
      const event = dragEvent(intent)
      control(view.render, 'trash-target').onDrop(event)
      assert.deepEqual(JSON.parse(JSON.stringify(view.emitted.at(-1))), ['trashDrop', intent])
      assert.equal(event.prevented(), 1)
      assert.equal(event.stopped(), 1)
    }
    props.canDelete = false
    assert.equal(control(view.render, 'toolbar-delete-trace').disabled, true)
    assert.equal(control(view.render, 'trash-target')['data-trash-enabled'], true)
    for (const intent of [{ kind: 'markers', name: 'Trc7', displayRevision: '34' },
      { kind: 'marker', name: 'Trc7', markerId: 2, displayRevision: '34' }]) {
      control(view.render, 'trash-target').onDrop(dragEvent(intent))
      assert.deepEqual(JSON.parse(JSON.stringify(view.emitted.at(-1))), ['trashDrop', intent])
    }
    const count = view.emitted.length
    control(view.render, 'trash-target').onDrop(dragEvent(source))
    control(view.render, 'toolbar-delete-trace').onClick()
    assert.equal(view.emitted.length, count)
    props.canDelete = true
    assert.equal(JSON.stringify(props), before)
    control(view.render, 'toolbar-delete-trace').onClick()
    assert.deepEqual(view.emitted.at(-1), ['deleteTrace'])
  } finally { view.dispose() }
})

test('toolbar ignores read-only, busy, stale, unsupported and malformed drops', async () => {
  const props = toolbarProps()
  const view = await sfcHarness('src/components/instrument/InstrumentToolbar.vue', props, {})
  try {
    for (const mutate of [() => { props.canWrite = false; props.canEditDisplay = false }, () => { props.busy = true },
      () => { props.displayRevision = undefined }]) {
      mutate()
      const event = dragEvent(source)
      control(view.render, 'trash-target').onDragover(event)
      control(view.render, 'trash-target').onDrop(event)
      assert.equal(event.prevented(), 0)
      assert.equal(control(view.render, 'trash-target')['data-trash-enabled'], false)
      props.canWrite = true; props.canEditDisplay = true; props.busy = false; props.displayRevision = '34'
    }
    for (const intent of [{ ...source, revision: '11' }, { ...source, displayRevision: '33' },
      { kind: 'markers', name: 'Trc7', displayRevision: '33' }, { ...source, kind: 'reorder' }])
      control(view.render, 'trash-target').onDrop(dragEvent(intent))
    for (const type of ['application/x-vnapro-new-trace', 'application/x-vnapro-trace', 'text/plain'])
      control(view.render, 'trash-target').onDrop(dragEvent(source, type))
    assert.deepEqual(view.emitted, [])
  } finally { view.dispose() }
})

test('display-only permission permits disabling markers but cannot delete acquisition objects', async () => {
  const props = toolbarProps()
  props.canWrite = false
  const view = await sfcHarness('src/components/instrument/InstrumentToolbar.vue', props, {})
  try {
    for (const intent of [source, { kind: 'diagram', diagram: 3, revision: '12', displayRevision: '34' }])
      control(view.render, 'trash-target').onDrop(dragEvent(intent))
    assert.deepEqual(view.emitted, [])
    const marker = { kind: 'marker', name: 'Trc7', markerId: 2, displayRevision: '34' }
    control(view.render, 'trash-target').onDrop(dragEvent(marker))
    assert.deepEqual(JSON.parse(JSON.stringify(view.emitted)), [['trashDrop', marker]])
  } finally { view.dispose() }
})
