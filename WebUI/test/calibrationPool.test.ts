import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive } from 'vue'
import { decodeCalibration } from '../src/api/calibrationDecoders'
import type { CalibrationCommand, CalibrationSnapshot } from '../src/api/calibrationTypes'
import { instrumentStatus } from './displayFixtures'
import { findElements, sfcHarness } from './sfcHarness'

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

test('large calibration pools use a bounded native scrolling list and allow the last set to be selected', async (context) => {
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
  const select = () => findElements(harness.render(), (node) => node.type === 'select')[0]!
  assert.equal(select().props!.size, 6)
  assert.match(String(select().props!.class), /calibration-pool/)
  const options = findElements(harness.render(), (node) => node.type === 'option')
  assert.equal(options.length, 205)
  assert.equal(options.at(-1)!.props!.value, '204')
  assert.equal(options.at(-1)!.children, 'CableTest pool 204')
  ;(select().props!['onUpdate:modelValue'] as (value: string) => void)('204')
  await nextTick()
  assert.equal(calls.length, 0, 'browsing the list must not activate calibration')
  const use = findElements(harness.render(), (node) => node.type === 'button' && node.children === 'Use Set')[0]!
  assert.equal(use.props!.disabled, false)
  await (use.props!.onClick as () => Promise<void>)()
  assert.deepEqual(JSON.parse(JSON.stringify(calls)), [{ action: 'select', setId: '204' }])
  props.canCalibrate = false
  await nextTick()
  const readonly = findElements(harness.render(), (node) => node.type === 'button' && node.children === 'Use Set')[0]!
  assert.equal(readonly.props!.disabled, true)
})
