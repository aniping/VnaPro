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

test('toolbar persistently identifies the current channel set and opens Use Cal for observers', async (context) => {
  const props = reactive({
    calibration: calibration(), layout: 'grid', hasTrace: true,
    canAutoscale: false, canEditDisplay: false, markerEnabled: false,
    canExport: false, canWrite: false, busy: false, canDelete: false, canAddTrace: false,
  })
  const harness = await sfcHarness('src/components/instrument/InstrumentToolbar.vue', props, {})
  context.after(harness.dispose)
  const button = () => findElements(harness.render(), (node) => node.props?.['data-testid'] === 'toolbar-calibration')[0]!
  assert.match(text(button()), /Ch1.*Cal On.*Cable A/)
  assert.match(button().props!.title, /Cable A.*Refl OSM.*P1/)
  assert.notEqual(button().props!.disabled, true)
  ;(button().props!.onClick as () => void)()
  assert.deepEqual(harness.emitted, [['selectTool', 'cal-use']])

  props.calibration.enabled = false
  await nextTick()
  assert.match(text(button()), /Cal Off.*Cable A/)
  props.calibration.enabled = true
  props.calibration.compatible = false
  await nextTick()
  assert.match(text(button()), /Cal Off.*Cable A/)
  props.calibration.selectedSetId = ''
  props.calibration.enabled = false
  await nextTick()
  assert.match(text(button()), /Cal Off.*No Cal Set/)
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
  const select = findElements(harness.render(), (node) => node.type === 'select')[0]!
  ;(select.props!['onUpdate:modelValue'] as (value: string) => void)('2')
  await nextTick()
  assert.match(text(current()), /Cable A.*User Cal On/)
  assert.doesNotMatch(text(current()), /Cable B/)
  assert.equal(commands.length, 0)

  const use = findElements(harness.render(), (node) => node.type === 'button' && text(node) === 'Use Set')[0]!
  const pending = (use.props!.onClick as () => Promise<void>)()
  assert.equal(commands[0]!.action, 'select')
  assert.equal('setId' in commands[0]! && commands[0].setId, '2')
  assert.match(text(current()), /Cable A/)
  complete(null)
  await pending
  assert.match(text(current()), /Cable A/, 'failed activation must preserve the current set')

  props.status.calibration = { ...calibration(), selectedSetId: '2', compatible: false, reason: 'Frequency axis mismatch' }
  await nextTick()
  assert.match(text(current()), /Cable B.*Cal Off.*P2, P4/)
  props.mode = 'cal'
  props.canCalibrate = false
  await nextTick()
  assert.match(text(current()), /Cable B.*Cal Off/, 'Cal landing and readonly users can see the current channel set')
  assert.ok(findElements(harness.render(), (node) => text(node).includes('Frequency axis mismatch')).length)
})

test('App supplies the authoritative calibration snapshot and summary keeps the set name', async (context) => {
  const harness = await sfcHarness('src/App.vue', {}, { fetch: async () => new Response('{}'), setTimeout, clearTimeout })
  context.after(harness.dispose)
  const status = harness.bindings.status as { value: InstrumentStatus | null }
  status.value = { ...instrumentStatus(), calibration: calibration() }
  const toolbar = findComponent(harness.render(), 'InstrumentToolbar')!
  assert.equal((toolbar.props!.calibration as CalibrationSnapshot).selectedSetId, '1')
  const bar = findComponent(harness.render(), 'InstrumentStatusBar')!
  assert.match(bar.props!['calibration-label'], /Cable A/)
  assert.match(calibrationSummary(status.value.calibration), /User Cal On.*Cable A/)
})
