import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive, type Ref } from 'vue'
import { decodeStatus } from '../src/api/decoders'
import { InstrumentApi } from '../src/api/instrumentApi'
import type { SweepConfiguration, SweepPatch } from '../src/api/types'
import { instrumentStatus } from './displayFixtures'
import { findElements, sfcHarness } from './sfcHarness'

function timedStatus() {
  const status = instrumentStatus()
  Object.assign(status.config, { sweepTimeAuto: true, sweepTimeSeconds: 2 })
  Object.assign(status.capabilities, { manualSweepTimeSupported: true })
  Object.assign(status, { lastSweepTimeSeconds: 0.032 })
  return status
}

test('扫时状态保留Auto、请求时间及独立实测时间，拒绝非法值', () => {
  const decoded = decodeStatus(timedStatus())
  assert.equal(Reflect.get(decoded.config, 'sweepTimeAuto'), true)
  assert.equal(Reflect.get(decoded.config, 'sweepTimeSeconds'), 2)
  assert.equal(Reflect.get(decoded, 'lastSweepTimeSeconds'), 0.032)
  for (const seconds of [0, -1, Infinity, NaN, 100001]) {
    const status = timedStatus()
    Object.assign(status.config, { sweepTimeSeconds: seconds })
    assert.throws(() => decodeStatus(status), /sweepTime/)
  }
})

test('配置传输包含扫时字段，保留通道与预期版本', async () => {
  let sent: Record<string, unknown> = {}
  const api = new InstrumentApi({ storage: null, fetch: async (_input, init) => {
    sent = JSON.parse(String(init?.body))
    return new Response(JSON.stringify({ operationId: 'time-config', state: 'pending' }), { status: 202 })
  } })
  await api.configure(Object.assign({ points: 201 }, { sweepTimeAuto: false, sweepTimeSeconds: 5 }), '7', 2)
  assert.equal(sent.sweepTimeAuto, false)
  assert.equal(sent.sweepTimeSeconds, 5)
  assert.equal(sent.channelId, 2)
  assert.equal(sent.expectedRevision, '7')
})

test('Sweep Params独立扫时草稿不覆盖点数，轮询不覆盖草稿，按冻结版本提交', async () => {
  const calls: unknown[] = []
  const props = reactive({ status: { ...timedStatus(), activeChannelId: 2 }, mode: 'points',
    canConfigure: true, busy: false,
    apply: async (patch: SweepPatch, revision: string, channelId?: number) => {
      calls.push({ patch, revision, channelId }); return null
    } })
  const view = await sfcHarness('src/components/softtools/ChannelSettingsSofttool.vue', props, {})
  const state = view.bindings as unknown as {
    draft: SweepConfiguration; update: () => void; commit: () => Promise<void>; discard: () => void
  }
  try {
    Object.assign(state.draft, { sweepTimeAuto: false, sweepTimeSeconds: 5 })
    state.update()
    const later = timedStatus()
    later.revision = '9'
    props.status = { ...later, activeChannelId: 2 }
    await nextTick()
    assert.equal(Reflect.get(state.draft, 'sweepTimeSeconds'), 5)
    await state.commit()
    assert.deepEqual(JSON.parse(JSON.stringify(calls)), [{ patch: { sweepTimeAuto: false, sweepTimeSeconds: 5 }, revision: '1', channelId: 2 }])
    props.canConfigure = false
    Object.assign(state.draft, { sweepTimeSeconds: 6 })
    state.update()
    await state.commit()
    assert.equal(calls.length, 1)
    state.discard()
    assert.equal(Reflect.get(state.draft, 'sweepTimeSeconds'), 2)
  } finally { view.dispose() }
})

test('Sweep Time支持秒与毫秒，Auto和只读状态不发送手动时间', async () => {
  const props = reactive({ automatic: false, seconds: 2.5, actualSeconds: 2.502, disabled: false })
  const view = await sfcHarness('src/components/shared/SweepTimeFields.vue', props, {})
  const state = view.bindings as unknown as { unit: Ref<'s' | 'ms'>; displayValue: Ref<number> }
  try {
    assert.equal(state.displayValue.value, 2.5)
    state.unit.value = 'ms'
    assert.equal(state.displayValue.value, 2500)
    assert.equal(view.emitted.length, 0, '换显示单位不修改设备设置')
    state.displayValue.value = 500
    assert.equal(view.emitted[0]![0], 'update:seconds')
    assert.equal(view.emitted[0]![1], 0.5)
    props.automatic = true
    state.displayValue.value = 100
    assert.equal(view.emitted.length, 1)
    assert.ok(findElements(view.render(), node => node.props?.['aria-label'] === 'Automatic Sweep Time').length)
    props.automatic = false
    props.disabled = true
    state.displayValue.value = 100
    assert.equal(view.emitted.length, 1)
    const checkbox = findElements(view.render(), node => node.props?.id === 'sweep-time-auto')[0]!
    assert.equal(checkbox.props!.disabled, true)
  } finally { view.dispose() }
})

test('扫时草稿拒绝非法最小时间，Auto切回不复写请求值，通道切换清草稿', async () => {
  const calls: SweepPatch[] = []
  const props = reactive({ status: { ...timedStatus(), activeChannelId: 1 }, mode: 'points',
    canConfigure: true, busy: false, apply: async (patch: SweepPatch) => { calls.push(patch); return null } })
  const view = await sfcHarness('src/components/softtools/ChannelSettingsSofttool.vue', props, {})
  const state = view.bindings as unknown as { draft: SweepConfiguration; update: () => void; commit: () => Promise<void> }
  try {
    for (const seconds of [0, -1, NaN, Infinity, 100001]) {
      state.draft.sweepTimeAuto = false
      state.draft.sweepTimeSeconds = seconds
      state.update()
      await state.commit()
    }
    assert.equal(calls.length, 0)
    state.draft.sweepTimeSeconds = 5
    state.update()
    props.status = { ...timedStatus(), activeChannelId: 2 }
    await nextTick()
    assert.equal(state.draft.sweepTimeAuto, true)
    assert.equal(state.draft.sweepTimeSeconds, 2)
    props.status = { ...props.status, config: { ...props.status.config, sweepTimeAuto: false } }
    await nextTick()
    state.draft.sweepTimeAuto = true
    state.update()
    await state.commit()
    assert.deepEqual(JSON.parse(JSON.stringify(calls)), [{ sweepTimeAuto: true }])
  } finally { view.dispose() }
})
