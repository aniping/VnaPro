import assert from 'node:assert/strict'
import test from 'node:test'
import { reactive } from 'vue'
import { scaleSettingText, scaleSettingNumber } from '../src/plot/formatting'
import { decodeStatus } from '../src/api/decoders'
import { instrumentStatus, reflection } from './displayFixtures'
import { sfcHarness } from './sfcHarness'
import type { SweepConfiguration, SweepPatch } from '../src/api/types'

test('scale settings remove floating tails at the displayed division resolution without reducing Marker precision', () => {
  assert.equal(scaleSettingText(-25.9996247, 0.5, 'db', reflection), '-26 dB')
  assert.equal(scaleSettingText(1.5122124, 1.5122124, 'db', reflection), '1.51 dB')
  assert.equal(scaleSettingText(0.0005122124, 0.0005122124, 'delay'), '5.12e-4 s')
  assert.equal(scaleSettingText(-25.995, 0.5, 'db', reflection), '-25.995 dB')
  assert.equal(scaleSettingText(-1e-8, 10, 'db', reflection), '0 dB')
  assert.equal(scaleSettingText(1.23456e-9, 1e-10, 'delay'), '1.235e-9 s')
  assert.equal(scaleSettingText(-Infinity, 0.5, 'db'), '−∞ dB')
  const fineTicks = Array.from({ length: 11 }, (_, index) => scaleSettingNumber(-25.995 + index * 0.001, 0.001))
  assert.equal(new Set(fineTicks).size, 11, 'Fine divisions must not collapse into eleven identical rounded labels')
})

test('Power status retains negative fractions and fails closed on invalid physical values', () => {
  const status = instrumentStatus()
  for (const powerDbm of [-120, -17.5, 0.25, 100])
    assert.equal(decodeStatus({ ...status, config: { ...status.config, powerDbm } }).config.powerDbm, powerDbm)
  for (const powerDbm of [-120.1, 100.1, NaN, Infinity, '-10'])
    assert.throws(() => decodeStatus({ ...status, config: { ...status.config, powerDbm } }), /powerDbm/)
})

test('Power Apply submits only the target channel power with its frozen revision', async () => {
  const status = { ...instrumentStatus(), activeChannelId: 2 }
  status.config.powerDbm = -10
  const calls: unknown[] = []
  const props = reactive({ status, mode: 'power', canConfigure: true, busy: false,
    apply: async (patch: SweepPatch, revision: string, channelId?: number) => { calls.push({ patch, revision, channelId }); return status } })
  const view = await sfcHarness('src/components/softtools/ChannelSettingsSofttool.vue', props, {})
  const bindings = view.bindings as unknown as { draft: SweepConfiguration; update: () => void; commit: () => Promise<void> }
  try {
    bindings.draft.powerDbm = -17.5
    bindings.update()
    await bindings.commit()
    assert.deepEqual(JSON.parse(JSON.stringify(calls)), [{ patch: { powerDbm: -17.5 }, revision: '1', channelId: 2 }])
    bindings.draft.powerDbm = 100.1
    bindings.update()
    await bindings.commit()
    assert.equal(calls.length, 1, 'Out-of-range power cannot be sent to the device')
  } finally { view.dispose() }
})
