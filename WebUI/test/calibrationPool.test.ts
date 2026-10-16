import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive } from 'vue'
import { decodeCalibration } from '../src/api/calibrationDecoders'
import type { CalibrationCommand, CalibrationSnapshot } from '../src/api/calibrationTypes'
import { instrumentStatus } from './displayFixtures'
import { findComponent, findElements, sfcHarness } from './sfcHarness'

function pool(count: number): CalibrationSnapshot {
  return {
    revision: '205', collecting: false, capturePending: false, sessionId: '0',
    method: 'fourPort', port1: 1, port2: 4, steps: [],
    selectedSetId: String(count), enabled: false, compatible: true, reason: '',
    sets: Array.from({ length: count }, (_, index) => ({
      id: String(index + 1), name: 'CableTest pool ' + String(index + 1).padStart(3, '0'),
      method: 'fourPort', port1: 1, port2: 4, compatible: true, includeIsolation: true,
      points: 800, startHz: 1e6, stopHz: 100e6, ifBandwidthHz: 1000,
    })),
  }
}

test('204 calibration names and the final selected identity survive decoding without truncation', () => {
  const decoded = decodeCalibration(pool(204))
  assert.equal(decoded.sets.length, 204)
  assert.equal(decoded.sets[0]!.name, 'CableTest pool 001')
  assert.equal(decoded.sets[203]!.name, 'CableTest pool 204')
  assert.equal(decoded.selectedSetId, '204')
  assert.equal(new Set(decoded.sets.map((entry) => entry.name)).size, 204)
  assert.equal(decodeCalibration(pool(256)).sets.length, 256)
  assert.throws(() => decodeCalibration(pool(257)), /calibration.sets/)
})

test('the manager opens the full pool without writing and applies the browsed final set explicitly', async (context) => {
  const calls: CalibrationCommand[] = []
  const calibration = pool(204)
  calibration.selectedSetId = '1'
  const props = reactive({ mode: 'cal-use', status: { ...instrumentStatus(), calibration },
    canCalibrate: true, busy: false,
    command: async (command: CalibrationCommand) => { calls.push(command); return props.status },
    exportSet: async () => ({}),
  })
  const harness = await sfcHarness('src/components/softtools/CalibrationSofttool.vue', props, {})
  context.after(harness.dispose)
  assert.equal(findComponent(harness.render(), 'CalibrationSetManager'), undefined)
  const entry = findElements(harness.render(), (node) => node.props?.['data-testid'] === 'calibration-manager-entry')[0]!
  ;(entry.props!.onClick as () => void)()
  await nextTick()
  const manager = () => findComponent(harness.render(), 'CalibrationSetManager')!
  assert.equal((manager().props!.calibration as CalibrationSnapshot).sets.length, 204)
  assert.equal(manager().props!.channelId ?? manager().props!['channel-id'], 1)
  assert.equal(findElements(harness.render(), (node) => node.type === 'select').length, 0)
  assert.equal(calls.length, 0, 'opening a manager only reads the existing status summaries')
  ;(manager().props!['onUpdate:selectedId'] as (value: string) => void)('204')
  await nextTick()
  assert.equal(calls.length, 0, 'browsing the list must not activate calibration')
  assert.equal(manager().props!.disabled, false)
  await (manager().props!.onApply as () => Promise<void>)()
  assert.deepEqual(JSON.parse(JSON.stringify(calls)), [{ action: 'select', setId: '204' }])
  props.canCalibrate = false
  await nextTick()
  assert.equal(manager().props!.disabled, true)
  await (manager().props!.onApply as () => Promise<void>)()
  assert.equal(calls.length, 1, 'parent must also enforce the write permission')
})
