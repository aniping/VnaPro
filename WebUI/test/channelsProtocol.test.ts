import assert from 'node:assert/strict'
import test from 'node:test'
import { decodeFrame, decodeStatus } from '../src/api/decoders'
import { InstrumentApi } from '../src/api/instrumentApi'
import type { AcquiredFrame, ChannelStatus, InstrumentStatus } from '../src/api/types'
import { channelFrameCondition, frameForMeasurement } from '../src/domain/channels'
import { useInstrument } from '../src/composables/useInstrument'
import { acquiredFrame, instrumentStatus, receiverTrace, reflection } from './displayFixtures'
import { findComponent, findElements, sfcHarness } from './sfcHarness'
import { reactive } from 'vue'

function fixture() {
  const a = { ...reflection, name: 'Wide', channelId: 1 }
  const b = { ...reflection, name: 'Zoomed', channelId: 2 }
  const status = instrumentStatus([a, b])
  const first = acquiredFrame(receiverTrace(a))
  const second: AcquiredFrame = {
    ...acquiredFrame(receiverTrace(b, [0.4, 0.5, 0.6])), channelId: 2,
    revision: '91', runId: '88', frequencyHz: [2e9, 2.5e9, 3e9],
  }
  const ch1: ChannelStatus = { channelId: 1, name: 'Ch1', state: 'Ready', error: '', activeConfigValid: true, acquisitionRevision: '1', runId: '1', config: status.config }
  const ch2: ChannelStatus = { ...ch1, channelId: 2, name: 'Ch2', acquisitionRevision: '91', runId: '88', config: { ...status.config, startHz: 2e9, stopHz: 3e9, points: 3 } }
  status.channels = [ch1, ch2]
  Object.assign(status, { activeChannelId: 2, config: ch2.config, acquisitionRevision: ch2.acquisitionRevision, runId: ch2.runId })
  status.display.selectedMeasurement = b.name
  return { status, first, second, bundle: { ...second, channelFrames: [first, second] } }
}

test('multi-channel decoding preserves independent identities, point counts and frequency axes', () => {
  const { status, first, second, bundle } = fixture()
  assert.equal(decodeStatus(status).measurements[1]?.channelId, 2)
  const decoded = decodeFrame(bundle)
  assert.deepEqual(decoded.channelFrames, [decodeFrame(first), decodeFrame(second)])
  const frames = { 1: first, 2: second }
  assert.equal(frameForMeasurement(status.measurements[0], frames, second), first)
  assert.equal(frameForMeasurement(status.measurements[0], undefined, second), null, 'Never borrow the active channel frame for another trace')
  assert.equal(frameForMeasurement(status.measurements[0], { 1: second }, second), null)
  assert.equal(channelFrameCondition(status, 1, first, true).kind, 'current')
  assert.equal(channelFrameCondition(status, 2, second, true).kind, 'current')
  status.channels![1]!.acquisitionRevision = '92'
  assert.equal(channelFrameCondition(status, 2, second, true).kind, 'stale')
  assert.equal(channelFrameCondition(status, 1, first, true).kind, 'current', 'Ch2 reconfiguration cannot stale Ch1')
})

test('channel metadata rejects duplicate, missing, unknown and mismatched frame ownership', () => {
  const { status, first, second, bundle } = fixture()
  for (const channels of [[], [status.channels![0], status.channels![0]], [status.channels![0]]])
    assert.throws(() => decodeStatus({ ...status, channels }), /channel/)
  assert.throws(() => decodeStatus({ ...status, measurements: status.measurements.map((trace) => ({ ...trace, channelId: 3 })) }), /channel/)
  assert.throws(() => decodeFrame({ ...bundle, channelFrames: [first, first] }), /channel/)
  assert.throws(() => decodeFrame({ ...bundle, channelFrames: [first, { ...second, traces: second.traces.map((trace) => ({ ...trace, channelId: 1 })) }] }), /channel/)
  const unavailable = { available: false, current: false, channelId: 2, reason: 'Waiting for manual trigger' }
  assert.deepEqual(decodeFrame({ ...first, channelFrames: [first, unavailable] }).channelFrames?.[1], unavailable)
})

test('channel actions and acquisition writes carry an explicit frozen channel and both CAS values', async () => {
  const calls: { path: string; body: Record<string, unknown> }[] = []
  const api = new InstrumentApi({ storage: null, fetch: async (input, init) => {
    calls.push({ path: String(input), body: JSON.parse(String(init?.body)) })
    return new Response(JSON.stringify({ operationId: 'op', state: 'pending' }), { status: 202 })
  } })
  await api.channel({ action: 'copy', channelId: 2, newDiagram: true }, '9007199254740993', '29')
  await api.configure({ spanHz: 25e6 }, '42', 2)
  await api.sweepControl({ action: 'trigger' }, 2)
  await api.stimulusZoom({ action: 'out' }, '42', '29', 2)
  assert.ok(calls[0]!.path.endsWith('/channels'))
  const { requestId, ...copy } = calls[0]!.body
  assert.deepEqual(copy, { action: 'copy', channelId: 2, newDiagram: true, expectedRevision: '9007199254740993', expectedDisplayRevision: '29' })
  assert.equal(typeof requestId, 'string')
  assert.ok(calls.every((call) => call.body.channelId === 2))
  assert.equal(new Set(calls.map((call) => call.body.requestId)).size, 4)
})

test('a racing top-level frame alias cannot put Ch1 data into the newly selected Ch2', async (context) => {
  const { status, first, second } = fixture()
  let current: InstrumentStatus = status
  const api = new InstrumentApi({ storage: null, fetch: async (input) => {
    const path = String(input)
    const payload = path.includes('/control/claim') ? { sessionId: 'owner', hasControl: true, leaseSeconds: 30 }
      : path.includes('/frame') ? { ...first, channelFrames: [first, second] } : current
    return new Response(JSON.stringify(payload), { status: 200 })
  } })
  const model = useInstrument({ api, pollMs: 1e6 })
  context.after(() => model.stop())
  model.start()
  await model.refresh()
  assert.equal(model.frame.value?.channelId, 2)
  assert.deepEqual(model.frame.value?.frequencyHz, second.frequencyHz)
  assert.equal(model.frameCondition.value.kind, 'current')
  current = { ...status, activeChannelId: 1, config: status.channels![0]!.config, acquisitionRevision: '1', runId: '1' }
  await model.refresh()
  assert.equal(model.frame.value?.channelId, 1)
  assert.deepEqual(model.channelFrames.value[2], decodeFrame(second), 'Selection retains the other channel cache')
})

test('Channel Config distinguishes default new channels from full copies and protects the last channel', async () => {
  const { status } = fixture()
  const calls: unknown[] = []
  const props = reactive({ status, canWrite: true, busy: false, perform: async (command: unknown) => { calls.push(command); return status } })
  const view = await sfcHarness('src/components/softtools/ChannelConfigSofttool.vue', props, {})
  try {
    const button = (id: string) => findElements(view.render(), (node) => node.props?.['data-testid'] === id)[0]!.props!
    await button('channel-new-diagram').onClick()
    await button('channel-copy-diagram').onClick()
    assert.deepEqual(JSON.parse(JSON.stringify(calls)), [{ action: 'new', newDiagram: true }, { action: 'copy', channelId: 2, newDiagram: true }])
    props.status = { ...status, channels: [status.channels![0]!], activeChannelId: 1 }
    assert.equal(button('channel-delete').disabled, true)
  } finally { view.dispose() }
})

test('All S-Params confirmation counts only the frozen target channel', async () => {
  const { status } = fixture()
  const props = reactive({ status, activeTraceName: 'Zoomed', disabled: false, create: async () => null })
  const view = await sfcHarness('src/components/softtools/AllSParametersWorkflow.vue', props, {})
  try {
    const dialog = findComponent(view.render(), 'ConfirmationDialog')!
    assert.match(String(dialog.props!.message), /Ch2 的 1 条旧测量定义/)
    assert.match(String(dialog.props!.message), /保留其他通道/)
    props.status = { ...status, activeChannelId: 1 }
    assert.equal(findComponent(view.render(), 'ConfirmationDialog')!.props!.disabled, true)
  } finally { view.dispose() }
})
