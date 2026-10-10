import assert from 'node:assert/strict'
import test from 'node:test'
import { decodeFrame, decodeStatus } from '../src/api/decoders'
import { ApiError, InstrumentApi } from '../src/api/instrumentApi'
import type { InstrumentGateway } from '../src/api/instrumentApi'
import type {
  AcceptedOperation, ClaimResponse, FrameResponse, InstrumentStatus, MeasurementCandidate,
  OperationSnapshot, RunAction, SweepConfiguration,
} from '../src/api/types'
import { useInstrument } from '../src/composables/useInstrument'

function snapshot(revision = '1'): InstrumentStatus {
  return {
    identity: 'VnaPro,NativeSimulator,0,0.2', backend: 'NativeSimulator', state: 'Ready', error: '', pending: false,
    revision, runId: '1', activeConfigValid: true,
    config: { startHz: 1e6, stopHz: 100e6, points: 201, ifBandwidthHz: 1000, triggerSource: 'IMM', sweepMode: 'HOLD' },
    measurements: [{ name: 'DefaultA', receiverKind: 'a', receiverPort: 1, sourcePort: 1, parameter: 'a1,1' }],
    control: { owner: 'web', hasControl: true, releasing: false, leaseRemainingMs: 30000, readOnlyReason: '' },
  }
}

function deferred<T>() {
  let resolve!: (value: T) => void
  let reject!: (error: unknown) => void
  const promise = new Promise<T>((accept, fail) => { resolve = accept; reject = fail })
  return { promise, resolve, reject }
}

class FakeGateway implements InstrumentGateway {
  sessionId = 'original-token'
  current = snapshot()
  claims = 0
  aborts = 0
  operationOwners: string[] = []
  statusLoader: () => Promise<InstrumentStatus> = async () => structuredClone(this.current)
  operationLoader: () => Promise<OperationSnapshot> = async () => ({ operationId: 'op-1', state: 'pending', result: null })
  submitter: () => Promise<AcceptedOperation> = async () => ({ operationId: 'op-1', state: 'pending' })
  status() { return this.statusLoader() }
  frame(): Promise<FrameResponse> { return Promise.resolve({ available: false, current: false, reason: 'No completed frame' }) }
  claim(): Promise<ClaimResponse> {
    this.claims++
    return Promise.resolve({ sessionId: this.sessionId, hasControl: true, leaseSeconds: 30 })
  }
  release(): Promise<void> { return Promise.resolve() }
  configure(_candidate: SweepConfiguration, _revision: string) { return this.submitter() }
  operate(_action: RunAction) { return this.submitter() }
  operation(_id: string, owner: string) { this.operationOwners.push(owner); return this.operationLoader() }
  addMeasurement(_candidate: MeasurementCandidate, _revision: string) { return this.submitter() }
  deleteMeasurement(_name: string, _revision: string) { return this.submitter() }
  abortRequests() { this.aborts++ }
}

async function ready(api: FakeGateway, options: { now?: () => number } = {}) {
  const model = useInstrument({ api, pollMs: 1e6, retryMs: 1e6, ...options })
  model.start()
  await model.refresh()
  assert.equal(model.canWrite.value, true)
  return model
}

async function microtasks() { await Promise.resolve(); await Promise.resolve(); await Promise.resolve() }

test('initial successful connection clears connecting notice and lease renewal preserves later notices', async (context) => {
  const api = new FakeGateway()
  let time = 1000
  const model = await ready(api, { now: () => time })
  context.after(() => model.stop())
  assert.equal(model.connected.value, true)
  assert.equal(model.hasControl.value, true)
  assert.equal(model.notice.value?.text, '原始接收波 · 未校准')
  model.notice.value = { text: 'Preserve this operation warning', tone: 'warning' }
  time += 12000
  await model.refresh()
  assert.equal(api.claims, 2)
  assert.equal(model.notice.value?.text, 'Preserve this operation warning')
})

test('transport keeps uint64 identities as strings and rejects malformed receiver data', () => {
  const status = snapshot('18446744073709551615')
  status.runId = '9007199254740993'
  assert.equal(decodeStatus(status).revision, '18446744073709551615')
  assert.equal(decodeStatus(status).runId, '9007199254740993')
  assert.throws(() => decodeStatus({ ...status, revision: 9007199254740992 }))
  const frame = {
    available: true, current: true, channelId: 1, runId: '9007199254740993', revision: '1', frameSeq: '18446744073709551615',
    originalPointCount: 2, displayPointCount: 2, decimated: false, frequencyHz: [1e6, 2e6],
    traces: [{ ...status.measurements[0], available: true, real: [1, null], imag: [0, 0], valid: [true, false] }],
  }
  assert.equal(decodeFrame(frame).available, true)
  assert.throws(() => decodeFrame({ ...frame, traces: [{ ...frame.traces[0], valid: [true, true] }] }))
  assert.throws(() => decodeFrame({ ...frame, frequencyHz: [2e6, 1e6] }))
})

test('HTTP 202 stays pending until terminal operation and a fresh authoritative status readback', async (context) => {
  const api = new FakeGateway()
  const model = await ready(api)
  context.after(() => model.stop())
  const readback = deferred<InstrumentStatus>()
  let operationFinished = false
  api.operationLoader = async () => {
    operationFinished = true
    return { operationId: 'op-1', state: 'completed', result: { ok: true, errorCode: 0, message: '' } }
  }
  api.statusLoader = () => operationFinished ? readback.promise : Promise.resolve(snapshot())
  let settled = false
  const configured = model.configure(snapshot().config, '1').then((value) => { settled = true; return value })
  await microtasks()
  assert.equal(model.pendingOperation.value?.id, 'op-1')
  assert.equal(model.status.value?.revision, '1')
  assert.equal(model.canWrite.value, false)
  const polling = model.refresh()
  await microtasks()
  assert.equal(model.pendingOperation.value?.readingBack, true)
  assert.equal(settled, false)
  readback.resolve(snapshot('2'))
  await polling
  assert.equal((await configured)?.revision, '2')
  assert.equal(model.pendingOperation.value, null)
})

test('an older in-flight status response cannot clear an uncertain write gate', async (context) => {
  const api = new FakeGateway()
  const model = await ready(api)
  context.after(() => model.stop())
  const oldStatus = deferred<InstrumentStatus>()
  api.statusLoader = () => oldStatus.promise
  const olderRead = model.refresh()
  api.submitter = async () => { throw new ApiError('network unavailable', 0, '', 'transport') }
  assert.equal(await model.run('single'), null)
  assert.equal(model.uncertainWrite.value, true)
  oldStatus.resolve(snapshot())
  await olderRead
  assert.equal(model.uncertainWrite.value, true)
  api.statusLoader = async () => snapshot()
  await model.refresh()
  assert.equal(model.uncertainWrite.value, false)
})

test('release intent prevents renewal from stale hasControl=true snapshots', async (context) => {
  const api = new FakeGateway()
  let time = 1000
  const model = await ready(api, { now: () => time })
  context.after(() => model.stop())
  const claims = api.claims
  await model.release()
  time += 30000
  await model.refresh()
  assert.equal(api.claims, claims)
  assert.equal(model.hasControl.value, false)
})

test('accepted operation observation keeps the original token after release and token replacement', async (context) => {
  const api = new FakeGateway()
  const model = await ready(api)
  context.after(() => model.stop())
  const outcome = model.run('single')
  await microtasks()
  await model.release()
  api.sessionId = 'replacement-token'
  await model.refresh()
  assert.deepEqual(api.operationOwners, ['original-token'])
  model.stop()
  assert.equal(await outcome, null)
  assert.equal(model.pendingOperation.value?.id, 'op-1')
})

test('dispose aborts observation and ignores delayed responses without claiming device cancellation', async () => {
  const api = new FakeGateway()
  const pendingRead = deferred<InstrumentStatus>()
  api.statusLoader = () => pendingRead.promise
  const model = useInstrument({ api, pollMs: 1e6 })
  model.start()
  const polling = model.refresh()
  model.stop()
  pendingRead.resolve(snapshot('7'))
  await polling
  assert.equal(api.aborts, 1)
  assert.equal(model.connected.value, false)
  assert.equal(model.status.value, null)
})

test('expired lease recovery accepts only a newly server-issued token; rejected writes never retry', async () => {
  const requests: { path: string; body: unknown; token: string | null }[] = []
  const storage = {
    getItem: () => 'expired-token', setItem: (_key: string, _value: string) => {}, removeItem: (_key: string) => {},
  }
  const fetcher: typeof fetch = async (input, init) => {
    const path = String(input)
    const body: unknown = init?.body ? JSON.parse(String(init.body)) : undefined
    const token = new Headers(init?.headers).get('X-Vna-Session')
    requests.push({ path, body, token })
    if (requests.length === 1) return new Response(JSON.stringify({ error: { code: 'invalid_session', message: 'expired' } }), { status: 403 })
    if (path.endsWith('/control/claim')) return new Response(JSON.stringify({ sessionId: 'server-token', hasControl: true, leaseSeconds: 30 }))
    return new Response(JSON.stringify({ error: { code: 'revision_conflict', message: 'changed' } }), { status: 409 })
  }
  const api = new InstrumentApi({ fetch: fetcher, storage })
  await api.claim()
  assert.equal(api.sessionId, 'server-token')
  assert.deepEqual(requests[1]?.body, {})
  assert.equal(requests[1]?.token, null)
  await assert.rejects(api.configure(snapshot().config, '18446744073709551615'), (error: unknown) =>
    error instanceof ApiError && error.status === 409)
  assert.equal(requests.length, 3)
  assert.equal((requests[2]?.body as Record<string, unknown>).expectedRevision, '18446744073709551615')
  assert.equal(Object.hasOwn(requests[2]?.body as Record<string, unknown>, 'triggerSource'), false)
  assert.equal(requests[2]?.token, 'server-token')
})
