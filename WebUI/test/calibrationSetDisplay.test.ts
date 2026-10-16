import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive, type VNode } from 'vue'
import type { CalibrationCommand, CalibrationSnapshot } from '../src/api/calibrationTypes'
import type { InstrumentStatus } from '../src/api/types'
import { calibrationSummary } from '../src/domain/calibration'
import { instrumentStatus } from './displayFixtures'
import { findComponent, findElements, sfcHarness } from './sfcHarness'

function calibration(): CalibrationSnapshot {
  return {
    revision: '5', collecting: false, capturePending: false, sessionId: '',
    method: 'onePort', port1: 1, port2: 0, steps: [],
    selectedSetId: '1', enabled: true, compatible: true, reason: '',
    sets: [
      { id: '1', name: 'Cable A · 2026-10-14', method: 'onePort', port1: 1, port2: 0, compatible: true },
      { id: '2', name: 'Cable B', method: 'twoPort', port1: 2, port2: 4, compatible: true },
    ],
  }
}

function text(node: VNode): string {
  if (typeof node.children === 'string') return node.children
  return Array.isArray(node.children)
    ? node.children.map((child) => typeof child === 'object' && child !== null ? text(child as VNode) : String(child ?? '')).join('')
    : ''
}

test('toolbar keeps the instrument identity without a calibration card', async (context) => {
  const props = reactive({
    backend: 'NativeSimulator', layout: 'grid', hasTrace: true,
    canAutoscale: false, canEditDisplay: false, markerEnabled: false,
    canExport: false, canWrite: false, busy: false, canDelete: false, canAddTrace: false,
  })
  const harness = await sfcHarness('src/components/instrument/InstrumentToolbar.vue', props, {})
  context.after(harness.dispose)
  const identity = findElements(harness.render(), (node) => node.props?.class === 'toolbar-identity')[0]!
  assert.equal(text(identity), 'VnaPro')
  assert.equal(identity.props!.title, 'VnaPro · NativeSimulator')
  assert.equal(findElements(harness.render(), (node) => node.props?.['data-testid'] === 'toolbar-calibration').length, 0)
  assert.deepEqual(harness.emitted, [])
})

test('Cal Manager distinguishes browsing a pool set from the channel current set until readback', async (context) => {
  let complete!: (status: InstrumentStatus | null) => void
  const commands: CalibrationCommand[] = []
  const props = reactive({
    mode: 'cal-use' as 'cal' | 'cal-use', status: { ...instrumentStatus(), calibration: calibration() },
    canCalibrate: true, busy: false,
    command: (command: CalibrationCommand) => {
      commands.push(command)
      return new Promise<InstrumentStatus | null>((resolve) => { complete = resolve })
    },
    exportSet: async () => ({}),
  })
  const harness = await sfcHarness('src/components/softtools/CalibrationSofttool.vue', props, {})
  context.after(harness.dispose)
  const current = () => findElements(harness.render(), (node) => node.props?.['data-testid'] === 'current-calibration-set')[0]!
  const entry = findElements(harness.render(), (node) => node.props?.['data-testid'] === 'calibration-manager-entry')[0]!
  ;(entry.props!.onClick as () => void)()
  await nextTick()
  const manager = () => findComponent(harness.render(), 'CalibrationSetManager')!
  ;(manager().props!['onUpdate:selectedId'] as (value: string) => void)('2')
  await nextTick()
  assert.match(text(current()), /Cable A.*User Cal On/)
  assert.doesNotMatch(text(current()), /Cable B/)
  assert.equal(commands.length, 0)

  const pending = (manager().props!.onApply as () => Promise<void>)()
  assert.equal(commands[0]!.action, 'select')
  assert.equal('setId' in commands[0]! && commands[0].setId, '2')
  assert.match(text(current()), /Cable A/)
  complete(null)
  await pending
  assert.match(text(current()), /Cable A/, 'failed activation must preserve the current set')
  assert.equal(manager().props!.selectedId ?? manager().props!['selected-id'], '2', 'failed activation keeps the browsed candidate')
  assert.ok(manager().props!.error, 'operation failure stays visible in the open manager')

  props.status.calibration = { ...calibration(), selectedSetId: '2', compatible: false, reason: 'Frequency axis mismatch' }
  await nextTick()
  assert.match(text(current()), /Cable B.*Cal Off.*P2, P4/)
  props.mode = 'cal'
  props.canCalibrate = false
  await nextTick()
  assert.match(text(current()), /Cable B.*Cal Off/, 'Cal landing and readonly users can see the current channel set')
  assert.ok(findElements(harness.render(), (node) => text(node).includes('Frequency axis mismatch')).length)
})

test('an import waits inside its manager and discards a file read after the active channel changes', async (context) => {
  let read!: (contents: string) => void
  const commands: CalibrationCommand[] = []
  const props = reactive({
    mode: 'cal-use', status: { ...instrumentStatus(), calibration: calibration() },
    canCalibrate: true, busy: false,
    command: async (command: CalibrationCommand) => { commands.push(command); return props.status },
    exportSet: async () => ({}),
  })
  const harness = await sfcHarness('src/components/softtools/CalibrationSofttool.vue', props, {})
  context.after(harness.dispose)
  const entry = findElements(harness.render(), (node) => node.props?.['data-testid'] === 'calibration-manager-entry')[0]!
  ;(entry.props!.onClick as () => void)()
  const manager = () => findComponent(harness.render(), 'CalibrationSetManager')
  const pending = (harness.bindings.importSelected as (event: Event) => Promise<void>)({
    target: { value: 'cal.json', files: [{ size: 64, text: () => new Promise<string>((resolve) => { read = resolve }) }] },
  } as unknown as Event)
  await nextTick()
  assert.equal(manager()!.props!.busy, true, 'file reading reserves the manager before parsing or writing')
  ;(manager()!.props!.onClose as () => void)()
  await nextTick()
  assert.ok(manager(), 'closing cannot hide an import that is still reading its file')
  props.status = { ...props.status, activeChannelId: 2, calibration: { ...calibration(), selectedSetId: '2' } }
  await nextTick()
  assert.equal(manager(), undefined)
  read('{"schema":2,"id":"original-channel-set"}')
  await pending
  assert.deepEqual(commands, [], 'the old file must not be imported into the newly active channel')
})

test('an export captures its original set and cannot download another channel under a changed identity', async (context) => {
  let complete!: (data: Record<string, unknown>) => void
  const exports: string[] = []
  const downloads: string[] = []
  let objectUrls = 0
  const props = reactive({
    mode: 'cal-use', status: { ...instrumentStatus(), calibration: calibration() },
    canCalibrate: false, busy: false, command: async () => null,
    exportSet: (setId: string) => {
      exports.push(setId)
      return new Promise<Record<string, unknown>>((resolve) => { complete = resolve })
    },
  })
  const harness = await sfcHarness('src/components/softtools/CalibrationSofttool.vue', props, {
    Blob,
    URL: { createObjectURL: () => { objectUrls++; return 'blob:calibration' }, revokeObjectURL() {} },
    document: { createElement: () => ({ href: '', download: '', click() { downloads.push(this.download) } }) },
    setTimeout: () => 0,
  })
  context.after(harness.dispose)
  const pending = (harness.bindings.exportSelected as () => Promise<void>)()
  assert.deepEqual(exports, ['1'])
  props.status = { ...props.status, activeChannelId: 2, calibration: { ...calibration(), selectedSetId: '2' } }
  await nextTick()
  complete({ schema: 3, id: '1' })
  await pending
  assert.equal(objectUrls, 0, 'a superseded export does not build a download for the new channel')
  assert.deepEqual(downloads, [])
})

test('App keeps calibration details outside the toolbar and summary preserves the current set name', async (context) => {
  const harness = await sfcHarness('src/App.vue', {}, { fetch: async () => new Response('{}'), setTimeout, clearTimeout })
  context.after(harness.dispose)
  const status = harness.bindings.status as { value: InstrumentStatus | null }
  status.value = { ...instrumentStatus(), calibration: calibration() }
  const toolbar = findComponent(harness.render(), 'InstrumentToolbar')!
  assert.equal(toolbar.props!.calibration, undefined)
  const bar = findComponent(harness.render(), 'InstrumentStatusBar')!
  assert.match(bar.props!['calibration-label'], /Cable A/)
  assert.match(calibrationSummary(status.value.calibration), /User Cal On.*Cable A/)
})
