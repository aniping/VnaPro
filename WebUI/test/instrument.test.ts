import { legacyProtocolFields } from './displayFixtures'
import assert from 'node:assert/strict'
import test from 'node:test'
import { decodeFrame, decodeStatus } from '../src/api/decoders'
import { ApiError, InstrumentApi } from '../src/api/instrumentApi'
import type { InstrumentGateway } from '../src/api/instrumentApi'
import type {
  AcceptedOperation,
  ClaimResponse,
  FrameResponse,
  InstrumentStatus,
  MeasurementCandidate,
  OperationSnapshot,
  RunAction,
  SweepControlCommand,
  StimulusZoomCommand,
  SweepConfiguration,
} from '../src/api/types'
import { useInstrument } from '../src/composables/useInstrument'
import {
  acquiredFrame,
  displaySnapshot,
  markerProjection,
  projection,
  receiverTrace,
} from './displayFixtures'
import type { DisplayCommand } from '../src/api/types'
import type { CalibrationSnapshot } from '../src/api/calibrationTypes'

function snapshot(revision = '1'): InstrumentStatus {
  return {
    identity: 'VnaPro,NativeSimulator,0,0.2',
    backend: 'NativeSimulator',
    state: 'Ready',
    error: '',
    pending: false,
    revision,
    acquisitionRevision: revision,
    runId: '1',
    activeConfigValid: true,
    capabilities: {
      portCount: 4,
      minFrequencyHz: 1,
      maxFrequencyHz: 8e9,
      minSweepPoints: 2,
      maxSweepPoints: 10001,
    },
    config: {
      startHz: 1e6,
      stopHz: 100e6,
      points: 201,
      ifBandwidthHz: 1000,
      triggerSource: 'IMM',
      sweepMode: 'HOLD',
      selectedSweepMode: 'CONT',
    },
    measurements: [
      {
        name: 'DefaultA',
        kind: 'receiver',
        receiverKind: 'a',
        receiverPort: 1,
        sourcePort: 1,
        parameter: 'a1,1',
        unit: 'a.u.',
        calibration: 'raw',
      },
    ],
    display: displaySnapshot([{ name: 'DefaultA' }]),
    control: {
      owner: 'web',
      hasControl: true,
      releasing: false,
      leaseRemainingMs: 30000,
      readOnlyReason: '',
    },
  }
}

function deferred<T>() {
  let resolve!: (value: T) => void
  let reject!: (error: unknown) => void
  const promise = new Promise<T>((accept, fail) => {
    resolve = accept
    reject = fail
  })
  return { promise, resolve, reject }
}

class FakeGateway implements InstrumentGateway {
  sessionId = 'original-token'
  current = snapshot()
  claims = 0
  aborts = 0
  operationOwners: string[] = []
  sweepCommands: SweepControlCommand[] = []
  statusLoader: () => Promise<InstrumentStatus> = async () =>
    structuredClone(this.current)
  operationLoader: () => Promise<OperationSnapshot> = async () => ({
    operationId: 'op-1',
    state: 'pending',
    result: null,
  })
  submitter: () => Promise<AcceptedOperation> = async () => ({
    operationId: 'op-1',
    state: 'pending',
  })
  status() {
    return this.statusLoader()
  }
  frame(): Promise<FrameResponse> {
    return Promise.resolve({
      available: false,
      current: false,
      reason: 'No completed frame',
    })
  }
  claim(): Promise<ClaimResponse> {
    this.claims++
    return Promise.resolve({
      sessionId: this.sessionId,
      hasControl: true,
      leaseSeconds: 30,
    })
  }
  release(): Promise<void> {
    return Promise.resolve()
  }
  configure(_candidate: SweepConfiguration, _revision: string) {
    return this.submitter()
  }
  operate(_action: RunAction) {
    return this.submitter()
  }
  sweepControl(command: SweepControlCommand) {
    this.sweepCommands.push(command)
    return this.submitter()
  }
  operation(_id: string, owner: string) {
    this.operationOwners.push(owner)
    return this.operationLoader()
  }
  addMeasurement(_candidate: MeasurementCandidate, _revision: string) {
    return this.submitter()
  }
  updateMeasurement(_candidate: MeasurementCandidate, _revision: string) {
    return this.submitter()
  }
  deleteMeasurement(_name: string, _revision: string) {
    return this.submitter()
  }
  display(_command: DisplayCommand, _revision: string) {
    return this.submitter()
  }
  cloneMeasurement(
    _source: string,
    _revision: string,
    _displayRevision: string,
  ) {
    return this.submitter()
  }
  createTrace(_revision: string, _displayRevision: string) {
    return this.submitter()
  }
  createAllSParameters(_revision: string, _displayRevision: string) {
    return this.submitter()
  }
  deleteDiagram(_diagram: number, _revision: string, _displayRevision: string) {
    return this.submitter()
  }
  abortRequests() {
    this.aborts++
  }
}

async function ready(api: FakeGateway, options: { now?: () => number } = {}) {
  const model = useInstrument({ api, pollMs: 1e6, retryMs: 1e6, ...options })
  model.start()
  await model.refresh()
  assert.equal(model.canWrite.value, true)
  return model
}

async function microtasks() {
  await Promise.resolve()
  await Promise.resolve()
  await Promise.resolve()
}

test('慢扫旁带跟随最新轮询，保留完整帧但不残留失败或已撤销的进度', async (context) => {
  const api = new FakeGateway()
  const initial = acquiredFrame(receiverTrace(api.current.measurements[0]!))
  api.frame = async () => initial
  const model = await ready(api)
  context.after(() => model.stop())
  const progress = { completedPoints: 1, frame: { ...initial, current: false, frameSeq: '2' } }
  api.frame = async () => ({ available: false, current: false, reason: 'No completed frame',
    channelId: 1, sweepProgress: progress })
  await model.refresh()
  assert.equal(model.channelFrames.value[1]!.sweepProgress, progress)
  assert.equal(model.frame.value?.frameSeq, initial.frameSeq)
  assert.deepEqual(model.frame.value?.traces, initial.traces)
  api.frame = async () => ({ available: false, current: false, reason: 'Stopped', channelId: 1 })
  await model.refresh()
  assert.equal(model.channelFrames.value[1]!.sweepProgress, undefined)
  api.frame = async () => ({ ...initial, sweepProgress: progress })
  await model.refresh()
  assert.equal(model.channelFrames.value[1]!.sweepProgress, progress)
  api.frame = async () => { throw new ApiError('Frame unavailable', 503) }
  await model.refresh()
  assert.equal(model.channelFrames.value[1]!.sweepProgress, undefined)
  assert.equal(model.frame.value?.frameSeq, initial.frameSeq)
})

test('长Single等待时仍可Stop，停止观察不覆盖Single原操作和完成Promise', async (context) => {
  const api = new FakeGateway()
  let submits = 0
  api.submitter = async () => ({ operationId: ++submits === 1 ? 'single-time' : 'stop-time', state: 'pending' })
  let completed = false
  api.operation = async (id, owner) => {
    api.operationOwners.push(owner)
    return completed ? { operationId: id, state: 'completed', result: { ok: true, errorCode: 0, message: '' } }
      : { operationId: id, state: 'pending', result: null }
  }
  const model = await ready(api)
  context.after(() => model.stop())
  const single = model.sweepControl({ action: 'single' })
  await microtasks()
  api.current.state = 'Running'
  api.current.config.sweepMode = 'SING'
  await model.refresh()
  assert.equal(model.canRun.value, false)
  assert.equal(model.canStopSweep.value, true)
  const stopping = model.sweepControl({ action: 'stop' })
  await microtasks()
  assert.equal(model.pendingOperation.value?.id, 'single-time')
  assert.equal(model.canStopSweep.value, false)
  assert.equal(model.canConfigure.value, false)
  assert.equal(await model.sweepControl({ action: 'stop' }), null)
  completed = true
  api.current.state = 'Ready'
  api.current.config.sweepMode = 'HOLD'
  await model.refresh()
  assert.equal((await single)?.state, 'Ready')
  assert.equal((await stopping)?.state, 'Ready')
  assert.equal(submits, 2)
})

test('已受理Stop的查询与读回临时失败保留凭据和写门禁，不重发停止', async (context) => {
  const api = new FakeGateway()
  api.current.state = 'Running'
  api.current.config.sweepMode = 'CONT'
  const model = await ready(api)
  context.after(() => model.stop())
  let settled = false
  const stopping = model.sweepControl({ action: 'stop' }).then(value => { settled = true; return value })
  await microtasks()
  api.operationLoader = async () => { throw new ApiError('Temporary operation failure', 503) }
  await model.refresh()
  assert.equal(settled, false)
  assert.equal(model.canConfigure.value, false)
  api.operationLoader = async () => ({ operationId: 'op-1', state: 'completed', result: { ok: true, errorCode: 0, message: '' } })
  let reads = 0
  api.statusLoader = async () => {
    if (++reads === 2) throw new ApiError('Temporary readback failure', 503)
    return structuredClone(api.current)
  }
  await model.refresh()
  assert.equal(settled, false)
  assert.equal(model.canConfigure.value, false)
  api.current.state = 'Ready'
  api.current.config.sweepMode = 'HOLD'
  await model.refresh()
  assert.equal((await stopping)?.state, 'Ready')
  assert.deepEqual(api.sweepCommands, [{ action: 'stop' }])
})

test('trace moves submit the drag-start display revision even after a status refresh', async (context) => {
  const api = new FakeGateway()
  const calls: { command: DisplayCommand; revision: string }[] = []
  api.display = (command, revision) => {
    calls.push({ command, revision })
    return api.submitter()
  }
  api.operationLoader = async () => ({
    operationId: 'op-1',
    state: 'failed',
    result: {
      ok: false,
      errorCode: -221,
      message: 'Display revision conflict',
    },
  })
  const model = await ready(api)
  context.after(() => model.stop())
  const original = structuredClone(model.status.value!.display)
  api.current.display.revision = '22'
  await model.refresh()
  const command: DisplayCommand = {
    action: 'traceMove',
    traceName: 'DefaultA',
    diagram: 1,
    position: 'right',
  }
  const result = await model.display(command, original.revision)
  assert.equal(result, null)
  assert.match(model.notice.value?.text ?? '', /Display revision conflict/)
  assert.deepEqual(calls, [{ command, revision: original.revision }])
  assert.deepEqual(model.status.value!.display.diagrams, original.diagrams)
  assert.equal(model.status.value!.display.revision, '22')
})

test('calibration workspace polls status without repeatedly fetching the hidden full DUT matrix', async (context) => {
  const api = new FakeGateway()
  const calibration: CalibrationSnapshot = {
    revision: '5', collecting: true, capturePending: false, sessionId: '6',
    method: 'onePort', port1: 1, port2: 0, selectedSetId: '', enabled: false,
    compatible: false, reason: '', sets: [], steps: [
      { id: 'open-1', kind: 'open', port1: 1, port2: 0, captured: true, runId: '2', frameSeq: '1' },
    ],
  }
  api.current.calibration = calibration
  let frameReads = 0
  const retained = acquiredFrame()
  api.frame = async () => { frameReads++; return structuredClone(retained) }
  const gateway = Object.assign(api, { calibration: async () => ({ operationId: 'op-1', state: 'pending' as const }) })
  const model = await ready(gateway)
  context.after(() => model.stop())
  const before = frameReads
  const firstFrame = model.frame.value
  for (let poll = 0; poll < 3; poll++) await model.refresh()
  assert.equal(frameReads, before, 'collecting repeatedly fetched all hidden DUT traces')
  assert.equal(model.frame.value, firstFrame, 'collecting must retain the previous DUT snapshot')
  const capture = model.calibrate({ action: 'capture', sessionId: '6', stepId: 'open-1' })
  await microtasks()
  api.operationLoader = async () => ({ operationId: 'op-1', state: 'completed', result: { ok: true, errorCode: 0, message: '' } })
  await model.refresh()
  assert.equal((await capture)?.calibration?.collecting, true)
  assert.equal(frameReads, before, 'standard operation readback must not fetch the hidden DUT matrix')
  api.current.calibration = { ...calibration, collecting: false, sessionId: '', steps: [] }
  await model.refresh()
  await model.refresh()
  assert.ok(frameReads > before, 'normal frame polling must resume after calibration ends')
})

test('continuous sweep accepts measurement and sweep settings through the same backend write gate', async (context) => {
  const api = new FakeGateway()
  api.current.state = 'Running'
  api.current.config.sweepMode = 'CONT'
  const model = await ready(api)
  context.after(() => model.stop())
  assert.equal(model.canConfigure.value, true)
  assert.equal(model.canChangeMeasurements.value, true)
  const changing = model.updateMeasurement(
    {
      name: 'DefaultA',
      kind: 'sParameter',
      receiverKind: 'b',
      receiverPort: 1,
      sourcePort: 1,
    },
    '1',
  )
  await microtasks()
  assert.equal(model.pendingOperation.value?.kind, 'measurement')
  model.stop()
  assert.equal(await changing, null)
})

test('measurement edits respect control ownership and pending operations in continuous mode', async (context) => {
  const api = new FakeGateway()
  api.current.state = 'Running'
  api.current.config.sweepMode = 'CONT'
  const model = await ready(api)
  context.after(() => model.stop())
  api.current.control.hasControl = false
  await model.refresh()
  assert.equal(model.canChangeMeasurements.value, false)
  assert.equal(await model.deleteMeasurement('DefaultA', '1'), null)
  api.current.control.hasControl = true
  await model.refresh()
  const adding = model.addMeasurement(
    { ...api.current.measurements[0]!, name: 'Copy' },
    '1',
  )
  await microtasks()
  assert.equal(model.pendingOperation.value?.kind, 'measurement')
  assert.equal(model.canChangeMeasurements.value, false)
  assert.equal(await model.deleteMeasurement('DefaultA', '1'), null)
  model.stop()
  assert.equal(await adding, null)
})

test('frame freshness pairs acquisition and measurement revisions separately across late responses', async (context) => {
  const api = new FakeGateway()
  api.current = snapshot('9')
  api.current.acquisitionRevision = '3'
  const model = await ready(api)
  context.after(() => model.stop())
  const acquired: FrameResponse = {
    available: true,
    current: true,
    channelId: 1,
    runId: '1',
    revision: '3',
    displayRevision: '1',
    sampleIndexes: [0, 1],
    measurementRevision: '9',
    frameSeq: '12',
    originalPointCount: 2,
    displayPointCount: 2,
    decimated: false,
    frequencyHz: [10e6, 20e6],
    traces: [
      {
        ...api.current.measurements[0]!,
        available: true,
        real: [1, 1],
        imag: [0, 0],
        valid: [true, true],
        projection: projection([0, 0]),
      },
    ],
  }
  api.frame = async () => acquired
  await model.refresh()
  assert.equal(model.frameCondition.value.kind, 'current')
  api.frame = async () => ({ ...acquired, measurementRevision: '8' })
  await model.refresh()
  assert.equal(model.frameCondition.value.kind, 'stale')
  api.current.acquisitionRevision = '4'
  api.frame = async () => acquired
  await model.refresh()
  assert.equal(model.frameCondition.value.kind, 'stale')
  api.frame = async () => ({ ...acquired, revision: '4' })
  await model.refresh()
  assert.equal(model.frameCondition.value.kind, 'current')
})

test('initial successful connection clears connecting notice and lease renewal preserves later notices', async (context) => {
  const api = new FakeGateway()
  let time = 1000
  const model = await ready(api, { now: () => time })
  context.after(() => model.stop())
  assert.equal(model.connected.value, true)
  assert.equal(model.hasControl.value, true)
  assert.equal(model.notice.value?.text, 'RAW 接收波与 S 参数 · 未校准')
  model.notice.value = {
    text: 'Preserve this operation warning',
    tone: 'warning',
  }
  time += 12000
  await model.refresh()
  assert.equal(api.claims, 2)
  assert.equal(model.notice.value?.text, 'Preserve this operation warning')
})

test('a pending background renewal keeps valid lease write and calibration capabilities stable', async (context) => {
  const api = Object.assign(new FakeGateway(), {
    calibration: async () => ({ operationId: 'cal-op', state: 'pending' as const }),
  })
  api.current.calibration = {
    revision: '5', collecting: false, capturePending: false, sessionId: '', method: 'onePort',
    port1: 1, port2: 0, selectedSetId: '', enabled: false, compatible: false, reason: '', sets: [], steps: [],
  }
  let time = 1000
  const model = await ready(api, { now: () => time })
  context.after(() => model.stop())
  const renewal = deferred<ClaimResponse>()
  const entered = deferred<void>()
  api.claim = () => { api.claims++; entered.resolve(); return renewal.promise }
  time += 12000
  const polling = model.refresh()
  await entered.promise
  try {
    assert.equal(model.canWrite.value, true, 'same-token renewal must not temporarily revoke a valid lease')
    assert.equal(model.canConfigure.value, true)
    assert.equal(model.canCalibrate.value, true)
    assert.equal(model.canEditDisplay.value, true)
    assert.equal(model.canContinueDisplayGesture.value, true)
    assert.equal(model.controlBusy.value, false, 'background renewal is not a foreground control transition')
    assert.equal(await model.claim(), false, 'manual claim must not duplicate an in-flight renewal')
    assert.equal(api.claims, 2)
  } finally {
    renewal.resolve({ sessionId: api.sessionId, hasControl: true, leaseSeconds: 30 })
    await polling
  }
  assert.equal(model.canWrite.value, true)
})

test('an expired real API renewal closes permissions without anonymously reclaiming control', async (context) => {
  const requests: { sessionId?: string }[] = []
  const entered = deferred<void>()
  const rejected = deferred<Response>()
  const fallback = deferred<void>()
  const recovery = deferred<Response>()
  const granted = (sessionId: string) => new Response(JSON.stringify({ sessionId, hasControl: true, leaseSeconds: 30 }))
  const expired = () => new Response(JSON.stringify({ error: { code: 'invalid_session', message: 'expired' } }), { status: 403 })
  const api = new InstrumentApi({
    storage: { getItem: () => 'original-token', setItem() {}, removeItem() {} },
    fetch: async (_input, init) => {
      requests.push(init?.body ? JSON.parse(String(init.body)) : {})
      if (requests.length === 1) return granted('original-token')
      if (requests.length === 2) { entered.resolve(); return rejected.promise }
      if (requests.at(-1)?.sessionId) return expired()
      fallback.resolve()
      return recovery.promise
    },
  })
  api.status = async () => snapshot()
  api.frame = async () => ({ available: false, current: false, reason: 'No completed frame' })
  let time = 1000
  const model = useInstrument({ api, now: () => time, pollMs: 1e6 })
  context.after(() => model.stop())
  model.start()
  await model.refresh()
  time += 12000
  const polling = model.refresh()
  await entered.promise
  assert.equal(model.canWrite.value, true, 'the existing lease stays usable while its renewal is undecided')
  rejected.resolve(expired())
  try {
    await Promise.race([polling, fallback.promise])
    assert.equal(requests.length, 2, 'background renewal must report expiry instead of starting anonymous recovery')
    assert.equal(model.hasControl.value, false)
    assert.equal(model.canWrite.value, false)
    assert.equal(model.canConfigure.value, false)
    assert.equal(model.canContinueDisplayGesture.value, false)
    assert.equal(await model.configure(snapshot().config, '1'), null)
    assert.equal(requests.length, 2, 'the closed gate cannot send a write using the expired token')
    // 主动获取仍允许用服务端新令牌恢复；它属于有前台忙碌门禁的显式操作。
    recovery.resolve(granted('replacement-token'))
    assert.equal(await model.claim(), true)
    assert.equal(api.sessionId, 'replacement-token')
    assert.equal(requests.length, 4)
    assert.deepEqual(requests[3], {})
  } finally {
    recovery.resolve(granted('replacement-token'))
    await polling
  }
})

test('a rejected renewal revokes permissions despite an earlier owned-control snapshot', async (context) => {
  for (const code of [403, 409, 0]) {
    await context.test(`renewal failure ${code}`, async (caseContext) => {
      const api = new FakeGateway()
      let time = 1000
      const model = await ready(api, { now: () => time })
      caseContext.after(() => model.stop())
      api.claim = async () => { api.claims++; throw new ApiError('renewal rejected', code) }
      time += 12000
      await model.refresh()
      assert.equal(model.status.value?.control.hasControl, true, 'the status read preceded the failed renewal')
      assert.equal(model.hasControl.value, false)
      assert.equal(model.canWrite.value, false)
      assert.equal(model.canContinueDisplayGesture.value, false)
      assert.equal(model.controlBusy.value, false)
      assert.equal(model.connected.value, code !== 0)
      await model.refresh()
      assert.equal(api.claims, 2, 'a failed renewal must not repeat automatically from stale ownership')
      assert.equal(model.canWrite.value, false)
    })
  }
})

test('release during a pending renewal closes permissions immediately and reaches the server after renewal', async (context) => {
  const api = new FakeGateway()
  let time = 1000
  const model = await ready(api, { now: () => time })
  context.after(() => model.stop())
  const renewal = deferred<ClaimResponse>()
  const entered = deferred<void>()
  api.claim = () => { api.claims++; entered.resolve(); return renewal.promise }
  let releases = 0
  api.release = async () => { releases++ }
  time += 12000
  const polling = model.refresh()
  await entered.promise
  const releasing = model.release()
  assert.equal(model.hasControl.value, false)
  assert.equal(model.canWrite.value, false)
  assert.equal(model.canContinueDisplayGesture.value, false)
  assert.equal(model.controlBusy.value, true)
  assert.equal(releases, 0, 'release must not be overtaken by a previously sent renewal')
  assert.equal(await model.claim(), false)
  renewal.resolve({ sessionId: api.sessionId, hasControl: true, leaseSeconds: 30 })
  assert.equal(await releasing, true)
  await polling
  assert.equal(releases, 1)
  assert.equal(model.controlBusy.value, false)
  assert.equal(model.hasControl.value, false, 'late renewal success cannot restore released control')
  time += 12000
  await model.refresh()
  assert.equal(api.claims, 2)
  assert.equal(model.canWrite.value, false)
})

test('a configuration write can be accepted while the same valid lease is renewing', async (context) => {
  const api = new FakeGateway()
  let time = 1000
  const model = await ready(api, { now: () => time })
  context.after(() => model.stop())
  const renewal = deferred<ClaimResponse>()
  const entered = deferred<void>()
  api.claim = () => { api.claims++; entered.resolve(); return renewal.promise }
  let writes = 0
  api.submitter = async () => { writes++; return { operationId: 'op-1', state: 'pending' } }
  time += 12000
  const polling = model.refresh()
  await entered.promise
  const configured = model.configure(api.current.config, '1')
  await microtasks()
  assert.equal(writes, 1)
  assert.equal(model.pendingOperation.value?.kind, 'config')
  assert.equal(model.pendingOperation.value?.ownerSessionId, 'original-token')
  assert.equal(model.canWrite.value, false, 'the accepted write still closes the ordinary operation gate')
  renewal.resolve({ sessionId: api.sessionId, hasControl: true, leaseSeconds: 30 })
  await polling
  api.operationLoader = async () => ({ operationId: 'op-1', state: 'completed', result: { ok: true, errorCode: 0, message: '' } })
  await model.refresh()
  assert.equal((await configured)?.revision, '1')
  assert.equal(model.canWrite.value, true)
})

test('disposing during a background renewal ignores the late control acknowledgement', async (context) => {
  const api = new FakeGateway()
  let time = 1000
  const model = await ready(api, { now: () => time })
  context.after(() => model.stop())
  const renewal = deferred<ClaimResponse>()
  const entered = deferred<void>()
  api.claim = () => { api.claims++; entered.resolve(); return renewal.promise }
  time += 12000
  const polling = model.refresh()
  await entered.promise
  model.stop()
  renewal.resolve({ sessionId: api.sessionId, hasControl: true, leaseSeconds: 30 })
  await polling
  assert.equal(api.aborts, 1)
  assert.equal(model.connected.value, false)
  assert.equal(model.hasControl.value, false)
  assert.equal(model.canWrite.value, false)
  assert.equal(model.canContinueDisplayGesture.value, false)
  assert.equal(model.controlBusy.value, false)
})

test('transport keeps uint64 identities as strings and rejects malformed receiver data', () => {
  const status = snapshot('18446744073709551615')
  status.runId = '9007199254740993'
  assert.equal(decodeStatus(status).revision, '18446744073709551615')
  assert.equal(decodeStatus(status).runId, '9007199254740993')
  assert.equal(decodeStatus(status).acquisitionRevision, '18446744073709551615')
  assert.throws(() =>
    decodeStatus({ ...status, acquisitionRevision: undefined }),
  )
  assert.throws(() =>
    decodeStatus({ ...status, acquisitionRevision: 9007199254740992 }),
  )
  assert.throws(() => decodeStatus({ ...status, revision: 9007199254740992 }))
  const frame = {
    available: true,
    current: true,
    channelId: 1,
    runId: '9007199254740993',
    revision: '1',
    displayRevision: '1',
    sampleIndexes: [0, 1],
    measurementRevision: '1',
    frameSeq: '18446744073709551615',
    originalPointCount: 2,
    displayPointCount: 2,
    decimated: false,
    frequencyHz: [1e6, 2e6],
    traces: [
      {
        ...status.measurements[0],
        available: true,
        real: [1, null],
        imag: [0, 0],
        valid: [true, false],
        projection: projection([0, null]),
      },
    ],
  }
  assert.equal(decodeFrame(frame).available, true)
  assert.throws(() => decodeFrame({ ...frame, measurementRevision: undefined }))
  assert.throws(() =>
    decodeFrame({ ...frame, measurementRevision: 9007199254740992 }),
  )
  assert.throws(() =>
    decodeFrame({
      ...frame,
      traces: [{ ...frame.traces[0], valid: [true, true] }],
    }),
  )
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
    return {
      operationId: 'op-1',
      state: 'completed',
      result: { ok: true, errorCode: 0, message: '' },
    }
  }
  api.statusLoader = () =>
    operationFinished ? readback.promise : Promise.resolve(snapshot())
  let settled = false
  const configured = model.configure(snapshot().config, '1').then((value) => {
    settled = true
    return value
  })
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
  api.submitter = async () => {
    throw new ApiError('network unavailable', 0, '', 'transport')
  }
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
    getItem: () => 'expired-token',
    setItem: (_key: string, _value: string) => {},
    removeItem: (_key: string) => {},
  }
  const fetcher: typeof fetch = async (input, init) => {
    const path = String(input)
    const body: unknown = init?.body ? JSON.parse(String(init.body)) : undefined
    const token = new Headers(init?.headers).get('X-Vna-Session')
    requests.push({ path, body, token })
    if (requests.length === 1)
      return new Response(
        JSON.stringify({
          error: { code: 'invalid_session', message: 'expired' },
        }),
        { status: 403 },
      )
    if (path.endsWith('/control/claim'))
      return new Response(
        JSON.stringify({
          sessionId: 'server-token',
          hasControl: true,
          leaseSeconds: 30,
        }),
      )
    return new Response(
      JSON.stringify({
        error: { code: 'revision_conflict', message: 'changed' },
      }),
      { status: 409 },
    )
  }
  const api = new InstrumentApi({ fetch: fetcher, storage })
  await api.claim()
  assert.equal(api.sessionId, 'server-token')
  assert.deepEqual(requests[1]?.body, {})
  assert.equal(requests[1]?.token, null)
  await assert.rejects(
    api.configure(snapshot().config, '18446744073709551615'),
    (error: unknown) => error instanceof ApiError && error.status === 409,
  )
  assert.equal(requests.length, 3)
  assert.equal(
    (requests[2]?.body as Record<string, unknown>).expectedRevision,
    '18446744073709551615',
  )
  assert.equal(
    Object.hasOwn(
      requests[2]?.body as Record<string, unknown>,
      'triggerSource',
    ),
    false,
  )
  assert.equal(requests[2]?.token, 'server-token')
})

test('frame requests encode an explicit display set and an empty set separately from defaults', async () => {
  const paths: string[] = []
  const api = new InstrumentApi({
    storage: null,
    fetch: async (input) => {
      paths.push(String(input))
      return new Response(
        JSON.stringify({
          available: false,
          current: false,
          reason: 'No frame',
        }),
      )
    },
  })
  await api.frame(['反射 A', 'S22'])
  await api.frame([])
  await api.frame()
  assert.deepEqual(paths, [
    '/api/v1/frame?trace=%E5%8F%8D%E5%B0%84%20A&trace=S22&encoding=packed',
    '/api/v1/frame?display=none&encoding=packed',
    '/api/v1/frame?encoding=packed',
  ])
})

test('late frame projection is stale until its display revision matches backend status', async (context) => {
  const api = new FakeGateway()
  const model = await ready(api)
  context.after(() => model.stop())
  const { acquiredFrame, receiverTrace } = await import('./displayFixtures')
  const frame = acquiredFrame(receiverTrace(api.current.measurements[0]!))
  api.frame = async () => frame
  await model.refresh()
  assert.equal(model.frameCondition.value.kind, 'current')
  api.current.display.revision = '2'
  api.current.display.presentations.DefaultA!.format = 'phase'
  await model.refresh()
  assert.equal(model.frameCondition.value.kind, 'stale')
  api.frame = async () => ({ ...frame, displayRevision: '2' })
  await model.refresh()
  assert.equal(model.frameCondition.value.kind, 'current')
})

test('display operations require control and wait for successful authoritative readback', async (context) => {
  const api = new FakeGateway()
  api.current.state = 'Running'
  api.current.config.sweepMode = 'CONT'
  const model = await ready(api)
  context.after(() => model.stop())
  const before = structuredClone(model.status.value!.display)
  const changing = model.display({
    action: 'format',
    traceName: 'DefaultA',
    format: 'phase',
  })
  await microtasks()
  assert.equal(model.pendingOperation.value?.kind, 'display')
  assert.deepEqual(model.status.value!.display, before)
  api.current.display.revision = '2'
  api.current.display.presentations.DefaultA!.format = 'phase'
  api.operationLoader = async () => ({
    operationId: 'op-1',
    state: 'completed',
    result: { ok: true, errorCode: 0, message: '' },
  })
  await model.refresh()
  assert.equal(
    (await changing)!.display.presentations.DefaultA!.format,
    'phase',
  )
  assert.equal(model.status.value!.display.revision, '2')
  api.current.control.hasControl = false
  await model.refresh()
  assert.equal(model.canEditDisplay.value, false)
  assert.equal(await model.display({ action: 'layout', layout: 'grid' }), null)
})

test('completed marker operations keep drag feedback pending until the matching frame projection has been read back', async (context) => {
  const api = new FakeGateway()
  const initial = acquiredFrame(receiverTrace(api.current.measurements[0]!))
  api.frame = async () => initial
  const model = await ready(api)
  context.after(() => model.stop())
  const freshFrame = deferred<FrameResponse>()
  const changed = structuredClone(initial)
  changed.displayRevision = '2'
  changed.traces[0]!.projection.marker = markerProjection({
    available: true,
    valid: true,
    frequencyHz: 20e6,
    sampleIndex: 1,
    value: -13.5,
    valueState: 'finite',
  })
  let frameRequests = 0
  api.frame = () =>
    ++frameRequests === 1 ? Promise.resolve(initial) : freshFrame.promise
  api.current.display.revision = '2'
  api.operationLoader = async () => ({
    operationId: 'op-1',
    state: 'completed',
    result: { ok: true, errorCode: 0, message: '' },
  })
  let settled = false
  const changing = model
    .display({ action: 'marker', traceName: 'DefaultA', frequencyHz: 20e6 })
    .then((value) => {
      settled = true
      return value
    })
  await microtasks()
  const polling = model.refresh()
  await microtasks()
  await microtasks()
  await microtasks()
  assert.equal(
    settled,
    false,
    'Marker commit must not remove drag feedback while the rendered projection is still old',
  )
  assert.equal(model.pendingOperation.value?.readingBack, true)
  assert.equal(frameRequests, 2)
  freshFrame.resolve(changed)
  await polling
  await changing
  assert.equal(
    model.frame.value?.displayRevision,
    model.status.value?.display.revision,
  )
  assert.equal(
    model.frame.value!.traces[0]!.projection.marker.frequencyHz,
    20e6,
  )
  assert.equal(settled, true)
})

function completedOperation(api: FakeGateway) {
  api.operationLoader = async () => ({
    operationId: 'op-1',
    state: 'completed',
    result: { ok: true, errorCode: 0, message: '' },
  })
}

test('display readback keeps mismatched projections pending and retries without resending the command', async (context) => {
  const api = new FakeGateway()
  const initial = acquiredFrame(receiverTrace(api.current.measurements[0]!))
  api.frame = async () => initial
  const model = await ready(api)
  context.after(() => model.stop())
  let writes = 0
  api.submitter = async () => {
    writes += 1
    return { operationId: 'op-1', state: 'pending' }
  }
  let settled = false
  const changing = model
    .display({ action: 'marker', traceName: 'DefaultA', frequencyHz: 20e6 })
    .then((value) => {
      settled = true
      return value
    })
  await microtasks()
  api.current.display.revision = '2'
  completedOperation(api)
  await model.refresh()
  assert.equal(settled, false)
  assert.equal(model.pendingOperation.value?.readingBack, true)
  assert.equal(model.frame.value?.displayRevision, '1')
  api.frame = async () => ({ ...initial, displayRevision: '2' })
  await model.refresh()
  assert.equal((await changing)?.display.revision, '2')
  assert.equal(model.frame.value?.displayRevision, '2')
  assert.equal(writes, 1)
})

for (const statusCode of [0, 404]) {
  test(`display frame readback failure (${statusCode}) preserves operation credentials for a later retry`, async (context) => {
    const api = new FakeGateway()
    const initial = acquiredFrame(receiverTrace(api.current.measurements[0]!))
    api.frame = async () => initial
    const model = await ready(api)
    context.after(() => model.stop())
    const changing = model.display({
      action: 'marker',
      traceName: 'DefaultA',
      frequencyHz: 20e6,
    })
    await microtasks()
    api.current.display.revision = '2'
    completedOperation(api)
    let reads = 0
    api.frame = async () => {
      if (++reads === 1) return initial
      throw new ApiError('Frame readback temporarily unavailable', statusCode)
    }
    await model.refresh()
    assert.equal(model.pendingOperation.value?.id, 'op-1')
    assert.equal(model.pendingOperation.value?.readingBack, true)
    api.frame = async () => ({ ...initial, displayRevision: '2' })
    await model.refresh()
    assert.equal((await changing)?.display.revision, '2')
    assert.equal(model.frame.value?.displayRevision, '2')
  })
}

test('display readback can complete on a retained HOLD projection from an earlier acquisition and run', async (context) => {
  const api = new FakeGateway()
  const initial = acquiredFrame(receiverTrace(api.current.measurements[0]!))
  api.frame = async () => initial
  const model = await ready(api)
  context.after(() => model.stop())
  const changing = model.display({
    action: 'marker',
    traceName: 'DefaultA',
    frequencyHz: 20e6,
  })
  await microtasks()
  api.current.revision = '5'
  api.current.acquisitionRevision = '3'
  api.current.runId = '4'
  api.current.display.revision = '2'
  completedOperation(api)
  api.frame = async () => ({ ...initial, displayRevision: '2', current: false })
  await model.refresh()
  assert.equal((await changing)?.display.revision, '2')
  assert.equal(model.frame.value?.revision, '1')
  assert.equal(model.frame.value?.runId, '1')
  assert.equal(model.frameCondition.value.kind, 'stale')
})

for (const emptyDisplay of [false, true]) {
  test(`display readback without an acquired frame completes with an authoritative status (empty display ${emptyDisplay})`, async (context) => {
    const api = new FakeGateway()
    const model = await ready(api)
    context.after(() => model.stop())
    const changing = model.display({ action: 'layout', layout: 'grid' })
    await microtasks()
    api.current.display.revision = '2'
    if (emptyDisplay) {
      api.current.display.displayedNames = []
      api.current.display.diagrams = []
    }
    completedOperation(api)
    await model.refresh()
    assert.equal((await changing)?.display.revision, '2')
    assert.equal(model.frame.value, null)
    assert.equal(model.pendingOperation.value, null)
  })
}

test('configuration operation completion retains its status-only readback behavior', async (context) => {
  const api = new FakeGateway()
  const model = await ready(api)
  context.after(() => model.stop())
  const changing = model.configure(api.current.config, '1')
  await microtasks()
  let frames = 0
  api.frame = async () => {
    frames += 1
    return { available: false, current: false, reason: 'No completed frame' }
  }
  completedOperation(api)
  api.current = snapshot('2')
  await model.refresh()
  assert.equal((await changing)?.revision, '2')
  assert.equal(frames, 1)
})

test('sweep selection waits for completed-operation readback and shares the write gate', async (context) => {
  const api = new FakeGateway()
  const model = await ready(api)
  context.after(() => model.stop())
  const selecting = model.sweepControl({ action: 'source', source: 'MAN' })
  await microtasks()
  assert.equal(model.status.value!.config.triggerSource, 'IMM')
  assert.equal(await model.sweepControl({ action: 'start' }), null)
  assert.deepEqual(api.sweepCommands, [{ action: 'source', source: 'MAN' }])
  completedOperation(api)
  api.current.config.triggerSource = 'MAN'
  await model.refresh()
  assert.equal((await selecting)?.config.triggerSource, 'MAN')
  assert.equal(model.pendingOperation.value, null)
  delete api.current.config.selectedSweepMode
  await model.refresh()
  assert.equal(await model.sweepControl({ action: 'start' }), null)
  assert.equal(api.sweepCommands.length, 1)
})

test('stimulus zoom waits for authoritative readback and can restore history with another active format', async (context) => {
  const calls: { command: StimulusZoomCommand; revision: string; displayRevision: string }[] = []
  const api = Object.assign(new FakeGateway(), {
    stimulusZoom(command: StimulusZoomCommand, revision: string, displayRevision: string) {
      calls.push({ command, revision, displayRevision })
      return Promise.resolve({ operationId: 'op-1', state: 'pending' as const })
    },
  })
  api.current.stimulusZoom = { available: true, canZoomOut: false, reason: '', depth: 0 }
  const model = await ready(api)
  context.after(() => model.stop())
  const command: StimulusZoomCommand = {
    action: 'apply', traceName: 'DefaultA', startHz: 20e6, stopHz: 80e6, min: -40, max: -10,
  }
  const zooming = model.stimulusZoom(command, '1', '4')
  await microtasks()
  assert.equal(model.status.value!.config.startHz, 1e6)
  assert.equal(await model.stimulusZoom({ action: 'out' }, '1', '4'), null)
  assert.deepEqual(calls, [{ command, revision: '1', displayRevision: '4' }])
  completedOperation(api)
  api.current.config.startHz = 20e6
  api.current.config.stopHz = 80e6
  api.current.revision = '2'
  api.current.display.revision = '5'
  api.current.stimulusZoom = { available: false, canZoomOut: true, reason: 'Circular trace selected', depth: 1 }
  await model.refresh()
  assert.equal((await zooming)?.config.startHz, 20e6)
  const restoring = model.stimulusZoom({ action: 'out' }, '2', '5')
  await microtasks()
  assert.equal(calls.length, 2)
  assert.deepEqual(calls[1], { command: { action: 'out' }, revision: '2', displayRevision: '5' })
  api.current.config.startHz = 1e6
  await model.refresh()
  assert.equal((await restoring)?.config.startHz, 1e6)
  delete api.current.stimulusZoom
  await model.refresh()
  assert.equal(await model.stimulusZoom(command, '2', '5'), null)
  assert.equal(calls.length, 2)
})

test('removing the last displayed trace completes from status without waiting for an unusable frame endpoint', async (context) => {
  const api = new FakeGateway()
  api.frame = async () =>
    acquiredFrame(receiverTrace(api.current.measurements[0]!))
  const model = await ready(api)
  context.after(() => model.stop())
  let settled = false
  const changing = model
    .display({ action: 'displayed', traceName: 'DefaultA', enabled: false })
    .then((value) => {
      settled = true
      return value
    })
  await microtasks()
  api.current.display.revision = '2'
  api.current.display.displayedNames = []
  api.current.display.diagrams = []
  completedOperation(api)
  let frames = 0
  api.frame = async () => {
    frames += 1
    throw new ApiError('Frame endpoint temporarily unavailable', 404)
  }
  await model.refresh()
  assert.equal(
    settled,
    true,
    'Empty display has no marker projection to wait for',
  )
  assert.equal((await changing)?.display.revision, '2')
  assert.equal(model.pendingOperation.value, null)
  assert.equal(frames, 1)
})

test('an explicit no-frame display readback clears the previously cached marker projection', async (context) => {
  const api = new FakeGateway()
  const initial = acquiredFrame(receiverTrace(api.current.measurements[0]!))
  api.frame = async () => initial
  const model = await ready(api)
  context.after(() => model.stop())
  const changing = model.display({
    action: 'format',
    traceName: 'DefaultA',
    format: 'phase',
  })
  await microtasks()
  api.current.display.revision = '2'
  completedOperation(api)
  let frames = 0
  api.frame = async () =>
    ++frames === 1
      ? initial
      : { available: false, current: false, reason: 'No completed frame' }
  await model.refresh()
  assert.equal((await changing)?.display.revision, '2')
  assert.equal(
    model.frame.value,
    null,
    'Confirmed no acquired frame must not reveal a cached old marker',
  )
  assert.equal(model.frameCondition.value.kind, 'empty')
})

test('operation lookup expiration still ends observation without retrying the write', async (context) => {
  const api = new FakeGateway()
  const model = await ready(api)
  context.after(() => model.stop())
  const changing = model.display({ action: 'layout', layout: 'grid' })
  await microtasks()
  api.operationLoader = async () => {
    throw new ApiError('Operation expired', 404)
  }
  await model.refresh()
  assert.equal(await changing, null)
  assert.equal(model.pendingOperation.value, null)
  assert.match(model.notice.value?.text ?? '', /操作结果已过期/)
})

test('display decoder rejects missing projection provenance and accepts an invalid marker without losing the frame', async () => {
  const { acquiredFrame, markerProjection, receiverTrace } =
    await import('./displayFixtures')
  const current = acquiredFrame(receiverTrace(snapshot().measurements[0]!))
  const marker = markerProjection({
    available: true,
    valid: false,
    reason: 'Invalid reference',
    frequencyHz: 15e6,
    sampleIndex: 1,
  })
  current.traces[0]!.projection.marker = marker
  assert.deepEqual(legacyProtocolFields(decodeFrame(current)), current)
  assert.throws(() => decodeFrame({ ...current, displayRevision: undefined }))
  assert.throws(() => decodeFrame({ ...current, sampleIndexes: [0, 0] }))
  assert.throws(() =>
    decodeFrame({
      ...current,
      traces: [{ ...current.traces[0], projection: undefined }],
    }),
  )
  assert.throws(() => decodeStatus({ ...snapshot(), display: undefined }))
  const status = snapshot()
  assert.throws(() =>
    decodeStatus({
      ...status,
      display: { ...status.display, diagrams: undefined },
    }),
  )
  assert.throws(() =>
    decodeStatus({
      ...status,
      display: {
        ...status.display,
        diagrams: [
          {
            number: 1,
            measurementNames: ['DefaultA'],
            primaryMeasurement: 'Missing',
          },
        ],
      },
    }),
  )
  assert.throws(() =>
    decodeStatus({
      ...status,
      display: {
        ...status.display,
        diagrams: [
          {
            number: 1,
            measurementNames: ['Missing'],
            primaryMeasurement: 'Missing',
          },
        ],
      },
    }),
  )
})

test('scale and center/span requests preserve the raw control parameters for backend execution', async () => {
  const bodies: Record<string, unknown>[] = []
  const api = new InstrumentApi({
    storage: null,
    fetch: async (_input, init) => {
      bodies.push(JSON.parse(String(init?.body)))
      return new Response(
        JSON.stringify({ operationId: 'op-1', state: 'pending' }),
        { status: 202 },
      )
    },
  })
  await api.configure({ centerHz: 4e9, spanHz: 2e9 }, '3')
  await api.display(
    {
      action: 'scale',
      traceName: 'DefaultA',
      perDivision: 2,
      referenceLevel: -5,
      referencePosition: 3,
    },
    '7',
  )
  assert.equal(bodies[0]!.centerHz, 4e9)
  assert.equal(bodies[0]!.spanHz, 2e9)
  assert.equal(bodies[0]!.startHz, undefined)
  assert.equal(bodies[0]!.stopHz, undefined)
  assert.equal(bodies[1]!.perDivision, 2)
  assert.equal(bodies[1]!.referenceLevel, -5)
  assert.equal(bodies[1]!.min, undefined)
  assert.equal(bodies[1]!.max, undefined)
  assert.equal(bodies[1]!.expectedDisplayRevision, '7')
})
