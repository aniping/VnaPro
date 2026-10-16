import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive, type VNode } from 'vue'
import type { CalibrationSnapshot } from '../src/api/calibrationTypes'
import { findElements, sfcHarness } from './sfcHarness'

function pool(count = 205): CalibrationSnapshot {
  return {
    revision: '205', collecting: false, capturePending: false, sessionId: '',
    method: 'fourPort', port1: 1, port2: 4, steps: [],
    selectedSetId: count ? '1' : '', enabled: true, compatible: true, reason: '',
    sets: Array.from({ length: count }, (_, index) => ({
      id: String(index + 1),
      name: `S5P2_S11P2-S20P1_S14P1 · CableTest ${String(index + 1).padStart(3, '0')}`,
      method: 'fourPort', port1: 1, port2: 4, compatible: index !== count - 1,
      includeIsolation: true, points: 800, startHz: 10e6, stopHz: 40e9, ifBandwidthHz: 1000,
    })),
  }
}

function managerProps(count = 205) {
  return reactive({
    calibration: pool(count), channelId: 1, selectedId: count ? '2' : '',
    disabled: false, busy: false, error: '',
  })
}

function text(node: VNode): string {
  if (typeof node.children === 'string') return node.children
  return Array.isArray(node.children)
    ? node.children.map((child) => typeof child === 'object' && child !== null ? text(child as VNode) : String(child ?? '')).join('')
    : ''
}

type Harness = Awaited<ReturnType<typeof sfcHarness>>

function elements(harness: Harness, id: string) {
  return findElements(harness.render(), (node) => node.props?.['data-testid'] === id)
}

function element(harness: Harness, id: string) {
  const node = elements(harness, id)[0]
  assert.ok(node, `Expected ${id} to be visible`)
  return node
}

function click(harness: Harness, id: string) {
  return (element(harness, id).props!.onClick as () => void)()
}

test('205 long names stay intact while search and compatibility filtering only browse local summaries', async (context) => {
  const props = managerProps()
  let reads = 0
  const harness = await sfcHarness('src/components/softtools/CalibrationSetManager.vue', props, {
    fetch: async () => { reads++; throw new Error('Browsing must not fetch calibration coefficients') },
  })
  context.after(harness.dispose)
  assert.equal(elements(harness, 'calibration-set-row').length, 205)
  const last = elements(harness, 'calibration-set-select').find((node) => node.props?.['data-set-id'] === '205')!
  assert.ok(text(last).includes(props.calibration.sets[204]!.name), 'the full stored name must reach the table')
  ;(last.props!.onClick as () => void)()
  assert.deepEqual(harness.emitted, [['update:selectedId', '205']])
  props.selectedId = '205'

  ;(element(harness, 'calibration-manager-search').props!['onUpdate:modelValue'] as (value: string) => void)('cAbLeTeSt 205')
  await nextTick()
  assert.equal(elements(harness, 'calibration-set-row').length, 1)
  ;(element(harness, 'calibration-compatible-only').props!['onUpdate:modelValue'] as (value: boolean) => void)(true)
  await nextTick()
  assert.equal(elements(harness, 'calibration-set-row').length, 0)
  assert.equal(props.selectedId, '205', 'hiding the candidate must not select another calibration')
  assert.ok(elements(harness, 'calibration-selected-hidden').length)
  assert.match(text(element(harness, 'calibration-selected-name')), /CableTest 205/)
  assert.equal(harness.emitted.length, 1, 'searching must never apply or change a selection')
  assert.equal(reads, 0)
})

test('the current badge follows the server snapshot while a different candidate is browsed', async (context) => {
  const props = managerProps()
  const harness = await sfcHarness('src/components/softtools/CalibrationSetManager.vue', props, {})
  context.after(harness.dispose)
  const currentRow = () => elements(harness, 'calibration-set-row').find((row) =>
    findElements(row, (node) => node.props?.['data-testid'] === 'calibration-current-badge').length,
  )!
  assert.equal(currentRow().props!['data-set-id'], '1')
  assert.match(text(element(harness, 'calibration-selected-name')), /CableTest 002/)
  props.selectedId = '3'
  await nextTick()
  assert.equal(currentRow().props!['data-set-id'], '1')
  props.calibration = { ...props.calibration, revision: '206', selectedSetId: '3' }
  await nextTick()
  assert.equal(currentRow().props!['data-set-id'], '3')
  assert.deepEqual(harness.emitted, [])
})

test('the pool has one row tab stop and arrow navigation only changes the browsed candidate', async (context) => {
  const props = managerProps()
  const harness = await sfcHarness('src/components/softtools/CalibrationSetManager.vue', props, {})
  context.after(harness.dispose)
  const tabStops = () => elements(harness, 'calibration-set-select').filter((node) => node.props?.tabindex === 0)
  assert.deepEqual(tabStops().map((node) => node.props!['data-set-id']), ['2'])
  let prevented = 0
  const navigate = (node: VNode, key: string) => (node.props!.onKeydown as (event: unknown) => void)({
    key, preventDefault() { prevented++ },
  })
  navigate(tabStops()[0]!, 'ArrowDown')
  assert.deepEqual(harness.emitted, [['update:selectedId', '3']])
  props.selectedId = '3'
  await nextTick()
  assert.deepEqual(tabStops().map((node) => node.props!['data-set-id']), ['3'])

  ;(element(harness, 'calibration-manager-search').props!['onUpdate:modelValue'] as (value: string) => void)('CableTest 20')
  await nextTick()
  assert.deepEqual(tabStops().map((node) => node.props!['data-set-id']), ['200'], 'a hidden candidate leaves the first visible row reachable')
  assert.equal(props.selectedId, '3', 'filtering does not replace the hidden candidate')
  navigate(tabStops()[0]!, 'End')
  assert.deepEqual(harness.emitted.at(-1), ['update:selectedId', '205'])
  props.selectedId = '205'
  await nextTick()
  navigate(tabStops()[0]!, 'Home')
  assert.deepEqual(harness.emitted.at(-1), ['update:selectedId', '200'])
  assert.equal(prevented, 3)
  props.busy = true
  await nextTick()
  navigate(elements(harness, 'calibration-set-select')[0]!, 'ArrowDown')
  assert.equal(harness.emitted.length, 3, 'a busy operation ignores navigation and never emits apply')
})

test('read-only users can browse and export while writes remain guarded at the event source', async (context) => {
  const props = managerProps()
  props.disabled = true
  const harness = await sfcHarness('src/components/softtools/CalibrationSetManager.vue', props, {})
  context.after(harness.dispose)
  for (const id of ['calibration-apply', 'calibration-delete', 'calibration-import']) {
    assert.equal(element(harness, id).props!.disabled, true)
    click(harness, id)
  }
  assert.equal(element(harness, 'calibration-export').props!.disabled, false)
  click(harness, 'calibration-export')
  const candidate = elements(harness, 'calibration-set-select').find((node) => node.props?.['data-set-id'] === '3')!
  ;(candidate.props!.onClick as () => void)()
  assert.deepEqual(harness.emitted, [['export'], ['update:selectedId', '3']])
})

test('busy submissions cannot close the dialog, change candidate, export or repeat an apply', async (context) => {
  const props = managerProps()
  props.busy = true
  const harness = await sfcHarness('src/components/softtools/CalibrationSetManager.vue', props, {})
  context.after(harness.dispose)
  for (const id of ['calibration-manager-close', 'calibration-apply', 'calibration-export']) {
    assert.equal(element(harness, id).props!.disabled, true)
    click(harness, id)
  }
  const candidate = elements(harness, 'calibration-set-select')[0]!
  ;(candidate.props!.onClick as () => void)()
  assert.deepEqual(harness.emitted, [])
  props.busy = false
  await nextTick()
  click(harness, 'calibration-manager-close')
  assert.deepEqual(harness.emitted, [['close']])
})

test('older sets show missing metadata explicitly instead of fabricated values', async (context) => {
  const props = managerProps(1)
  props.selectedId = '1'
  props.calibration.sets = [{ id: '1', name: 'Legacy set', method: 'twoPort', port1: 2, port2: 4, compatible: true }]
  const harness = await sfcHarness('src/components/softtools/CalibrationSetManager.vue', props, {})
  context.after(harness.dispose)
  const content = text(harness.render())
  assert.match(content, /Legacy set/)
  assert.match(content, /P2.*P4/)
  assert.match(content, /未记录/)
  assert.match(content, /未说明/)
  assert.doesNotMatch(content, /undefined|NaN/)
})

test('an empty pool and a search with no matches have distinct recovery messages', async (context) => {
  const props = managerProps(0)
  const harness = await sfcHarness('src/components/softtools/CalibrationSetManager.vue', props, {})
  context.after(harness.dispose)
  const emptyPool = text(harness.render())
  assert.match(emptyPool, /没有校准集|暂无校准集|No calibration/)
  props.calibration = pool()
  ;(element(harness, 'calibration-manager-search').props!['onUpdate:modelValue'] as (value: string) => void)('missing-cable-xyz')
  await nextTick()
  const noMatches = text(harness.render())
  assert.match(noMatches, /没有匹配|无匹配|No matching/)
  assert.notEqual(noMatches, emptyPool)
  assert.equal(elements(harness, 'calibration-set-row').length, 0)
  assert.deepEqual(harness.emitted, [])
})

test('deleting requires a fresh explicit confirmation and invalidates stale selection, revision or permission', async (context) => {
  const props = managerProps()
  const harness = await sfcHarness('src/components/softtools/CalibrationSetManager.vue', props, {})
  context.after(harness.dispose)
  for (const change of [
    () => { props.selectedId = '3' },
    () => { props.calibration = { ...props.calibration, revision: '206' } },
    () => { props.disabled = true },
  ]) {
    click(harness, 'calibration-delete')
    await nextTick()
    const staleConfirm = element(harness, 'calibration-confirm-delete').props!.onClick as () => void
    assert.deepEqual(harness.emitted, [], 'the first delete action only asks for confirmation')
    change()
    await nextTick()
    assert.equal(elements(harness, 'calibration-delete-confirmation').length, 0)
    staleConfirm()
    assert.deepEqual(harness.emitted, [], 'a saved callback cannot submit a stale delete intention')
    props.disabled = false
    await nextTick()
  }
  click(harness, 'calibration-delete')
  await nextTick()
  click(harness, 'calibration-confirm-delete')
  assert.deepEqual(harness.emitted, [['remove']])
})
