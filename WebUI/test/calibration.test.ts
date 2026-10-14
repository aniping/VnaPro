import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive } from 'vue'
import { decodeCalibration } from '../src/api/calibrationDecoders'
import { decodeFrame, decodeStatus } from '../src/api/decoders'
import { InstrumentApi } from '../src/api/instrumentApi'
import type { CalibrationCommand, CalibrationSnapshot } from '../src/api/calibrationTypes'
import { calibrationConnection } from '../src/domain/calibration'
import { useInstrument } from '../src/composables/useInstrument'
import { useWorkspace } from '../src/composables/useWorkspace'
import { exportVisibleFrameCsv } from '../src/plot/exportCsv'
import { acquiredFrame, instrumentStatus, legacyProtocolFields, receiverTrace, reflection, transmission } from './displayFixtures'
import { findElements, sfcHarness } from './sfcHarness'

function calibration(collecting = true): CalibrationSnapshot {
  return { revision: '5', collecting, capturePending: false, sessionId: collecting ? '6' : '0',
    method: 'onePort', port1: 1, port2: 0, selectedSetId: '0', enabled: false,
    compatible: false, reason: '', sets: [], steps: collecting ? ['open', 'short', 'load'].map((kind, i) => ({
      id: `step-${i}`, kind: kind as 'open' | 'short' | 'load', port1: 1, port2: 0,
      captured: false, runId: '0', frameSeq: '0' })) : [] }
}

test('calibration protocol preserves uint64 revisions and rejects invalid sessions, duplicate steps and unsupported ports', () => {
  const value = calibration()
  value.revision = '18446744073709551615'
  assert.equal(decodeCalibration(value).revision, value.revision)
  assert.throws(() => decodeCalibration({ ...value, sessionId: '0' }), /session/)
  assert.equal(decodeCalibration(calibration(false)).selectedSetId, '')
  assert.throws(() => decodeCalibration({ ...value, steps: [value.steps[0], value.steps[0]] }), /duplicate/)
  assert.throws(() => decodeCalibration({ ...value, port1: 5 }), /port1/)
  assert.throws(() => decodeCalibration({ ...calibration(false), capturePending: true }), /capturePending/)
  assert.throws(() => decodeCalibration({ ...value, method: 'twoPort', port2: 1 }), /ports/)
  const stored = { ...calibration(false), selectedSetId: '1', sets: [{ id: '1', name: 'P2/P4', method: 'twoPort',
    port1: 2, port2: 4, compatible: true, includeIsolation: true,
    startHz: 10e6, stopHz: 8e9, points: 201, ifBandwidthHz: 1000 }] }
  const decoded = decodeCalibration(stored)
  assert.equal(decoded.sessionId, '')
  assert.equal(decoded.selectedSetId, '1')
  assert.equal(decoded.sets[0]!.includeIsolation, true)
  assert.equal(decoded.sets[0]!.startHz, 10e6)
  assert.equal(decoded.sets[0]!.points, 201)
  assert.equal(decoded.sets[0]!.ifBandwidthHz, 1000)
  assert.throws(() => decodeCalibration({ ...stored, sets: [{ ...stored.sets[0], startHz: 9e9 }] }), /frequency range/)
})

test('calibrated S parameters coexist with raw receivers and uncovered uncalibrated traces', () => {
  const calibrated = { ...reflection, calibration: 'calibrated' as const }
  const raw = { ...reflection, name: 'RawA', kind: 'receiver' as const, receiverKind: 'a' as const,
    parameter: 'a1,1', unit: 'a.u.' as const, calibration: 'raw' as const }
  const status = instrumentStatus([calibrated, raw, transmission])
  status.calibration = calibration(false)
  assert.equal(decodeStatus(legacyProtocolFields(status)).measurements[0]!.calibration, 'calibrated')
  assert.throws(() => decodeStatus(legacyProtocolFields(instrumentStatus([{ ...raw, calibration: 'calibrated' }]))), /receiver metadata/)
  const frame = acquiredFrame(receiverTrace(calibrated))
  frame.calibrationRevision = '5'
  frame.traces.push(receiverTrace(raw), receiverTrace(transmission))
  const decoded = decodeFrame(legacyProtocolFields(frame))
  assert.equal(decoded.available && decoded.calibrationRevision, '5')
  assert.equal(decoded.available && decoded.traces[2]!.calibration, 'uncalibrated')
  const csv = exportVisibleFrameCsv(frame, status.display.presentations)
  assert.match(csv, /calibrationRevision=5/)
  assert.match(csv, /,calibrated,/)
  assert.match(csv, /,uncalibrated,/)
  assert.match(csv, /,raw,/)
})

test('calibration API uses the shared accepted-operation protocol and sends the expected calibration revision', async () => {
  const requests: { path: string; body: Record<string, unknown> }[] = []
  const api = new InstrumentApi({ storage: null, fetch: async (input, init) => {
    requests.push({ path: String(input), body: JSON.parse(String(init?.body)) as Record<string, unknown> })
    return new Response(JSON.stringify({ operationId: 'cal-op', state: 'pending' }), { status: 202 })
  } })
  await api.calibration({ action: 'capture', sessionId: 'session-1', stepId: 'open1' }, '18446744073709551615')
  assert.equal(requests[0]!.path, '/api/v1/calibration')
  assert.equal(requests[0]!.body.expectedRevision, '18446744073709551615')
  assert.equal(requests[0]!.body.stepId, 'open1')
  assert.equal(typeof requests[0]!.body.requestId, 'string')
})

test('standard capture stays unconfirmed until backend readback; readonly and stale sessions cannot capture', async (context) => {
  let resolve!: (result: ReturnType<typeof instrumentStatus> | null) => void
  const calls: CalibrationCommand[] = []
  const props = reactive({ status: { ...instrumentStatus(), calibration: calibration() }, disabled: false,
    loadPreview: async () => ({ available: false as const, sessionId: '6', stepId: 'step-0', calibrationRevision: '5', reason: 'No preview' }),
    command: (command: CalibrationCommand) => { calls.push(command); return new Promise<ReturnType<typeof instrumentStatus> | null>((accept) => { resolve = accept }) } })
  const harness = await sfcHarness('src/components/softtools/CalibrationAcquisition.vue', props, {})
  context.after(harness.dispose)
  const button = () => findElements(harness.render(), (node) => node.props?.['data-cal-step'] === 'step-0')[0]!
  const pending = (button().props!.onClick as () => Promise<void>)()
  await nextTick()
  assert.equal(props.status.calibration.steps[0]!.captured, false)
  assert.equal(button().props!.disabled, true)
  props.status.calibration.steps[0]!.captured = true
  props.status.calibration.steps[0]!.runId = '6'
  props.status.calibration.steps[0]!.frameSeq = '7'
  resolve(props.status)
  await pending
  await nextTick()
  assert.match(String(button().props!.class), /\bcaptured\b/)
  assert.deepEqual(JSON.parse(JSON.stringify(calls)), [{ action: 'capture', sessionId: '6', stepId: 'step-0' }])
  props.disabled = true
  await (button().props!.onClick as () => Promise<void>)()
  assert.equal(calls.length, 1)
  props.disabled = false
  props.status.calibration.sessionId = 'other-session'
  await (button().props!.onClick as () => Promise<void>)()
  assert.equal(calls.length, 1)
})

test('manual setup cannot start after the frozen acquisition revision changes', async (context) => {
  let calls = 0
  const props = reactive({ status: { ...instrumentStatus(), calibration: calibration(false) }, disabled: false,
    command: async () => { calls++; return null } })
  const harness = await sfcHarness('src/components/softtools/CalibrationWorkflow.vue', props, {})
  context.after(harness.dispose)
  ;(harness.bindings.next as () => void)()
  props.status.acquisitionRevision = '2'
  await nextTick()
  await (harness.bindings.begin as () => Promise<void>)()
  assert.equal(calls, 0)
})

test('HOLD projection with an older calibration revision cannot drive autoscale', () => {
  const status = instrumentStatus()
  status.calibration = calibration(false)
  const frame = acquiredFrame()
  frame.calibrationRevision = '4'
  const workspace = useWorkspace(() => status, () => frame, () => undefined)
  assert.equal(workspace.canAutoscale.value, false)
})

test('two-port connection instructions use physical ports and an explicit isolated load connection', () => {
  const step = { ...calibration().steps[0]!, kind: 'isolation' as const, port1: 2, port2: 4 }
  assert.match(calibrationConnection(step), /P2.*P4.*50 Ω/)
})

test('instrument treats an old calibration projection as stale even with matching acquisition and display revisions', async (context) => {
  const status = instrumentStatus()
  status.calibration = calibration(false)
  const frame = acquiredFrame()
  frame.calibrationRevision = '4'
  const api = new InstrumentApi({ storage: null, fetch: async (input) => {
    const path = String(input)
    if (path.endsWith('/control/claim')) return new Response(JSON.stringify({ sessionId: 'token', hasControl: true, leaseSeconds: 30 }))
    return new Response(JSON.stringify(legacyProtocolFields(path.endsWith('/frame') ? frame : status)))
  } })
  const model = useInstrument({ api, pollMs: 100000 })
  context.after(model.stop)
  model.start()
  await model.refresh()
  assert.equal(model.frameCondition.value.label, 'PREVIOUS CAL')
  frame.calibrationRevision = '5'
  await model.refresh()
  assert.equal(model.frameCondition.value.kind, 'current')
  status.calibration.collecting = true
  status.calibration.sessionId = '6'
  status.calibration.steps = calibration().steps
  await model.refresh()
  assert.equal(model.canConfigure.value, false)
  assert.equal(model.canRun.value, false)
  assert.equal(model.canCalibrate.value, true)
})

test('faulted instruments can cancel an owned calibration session without admitting capture or other writes', async (context) => {
  const status = instrumentStatus()
  status.state = 'Faulted'
  status.calibration = calibration()
  const commands: CalibrationCommand[] = []
  let completed = false
  const api = new InstrumentApi({ storage: null, fetch: async (input, init) => {
    const path = String(input)
    if (path.endsWith('/control/claim')) return new Response(JSON.stringify({ sessionId: 'token', hasControl: true, leaseSeconds: 30 }))
    if (path.endsWith('/frame')) return new Response(JSON.stringify({ available: false, current: false, reason: 'Faulted' }))
    if (path.endsWith('/calibration')) {
      commands.push(JSON.parse(String(init?.body)) as CalibrationCommand)
      return new Response(JSON.stringify({ operationId: 'cancel-op', state: 'pending' }), { status: 202 })
    }
    if (path.endsWith('/operations/cancel-op')) return new Response(JSON.stringify({ operationId: 'cancel-op', state: completed ? 'completed' : 'pending', result: completed ? { ok: true, errorCode: 0, message: '' } : null }))
    return new Response(JSON.stringify(legacyProtocolFields(status)))
  } })
  const model = useInstrument({ api, pollMs: 100000 })
  context.after(model.stop)
  model.start()
  await model.refresh()
  assert.equal(model.canCalibrate.value, false)
  assert.equal(model.canCancelCalibration.value, true)
  assert.equal(await model.calibrate({ action: 'capture', sessionId: '6', stepId: 'step-0' }), null)
  const completion = model.calibrate({ action: 'cancel', sessionId: '6' })
  for (let i = 0; i < 12; i++) await Promise.resolve()
  assert.equal(commands[0]!.action, 'cancel')
  completed = true
  status.calibration = calibration(false)
  await model.refresh()
  assert.equal((await completion)?.calibration?.collecting, false)
  assert.equal(model.canCalibrate.value, false)
})

test('Cal Manager distinguishes a requested enable from an incompatible inactive correction', async (context) => {
  const cal = calibration(false)
  cal.selectedSetId = '1'
  cal.enabled = true
  cal.compatible = false
  cal.sets = [{ id: '1', name: 'Prior calibration', method: 'twoPort', port1: 2, port2: 4, compatible: false, includeIsolation: false }]
  const calls: CalibrationCommand[] = []
  const props = reactive({ mode: 'cal-use', status: { ...instrumentStatus(), calibration: cal }, canCalibrate: true, busy: false,
    command: async (command: CalibrationCommand) => { calls.push(command); return null }, exportSet: async () => ({ schema: 1 }) })
  const harness = await sfcHarness('src/components/softtools/CalibrationSofttool.vue', props, {})
  context.after(harness.dispose)
  const toggle = findElements(harness.render(), (node) => node.type === 'button' && node.props?.['aria-pressed'] !== undefined)[0]!
  assert.equal(toggle.props!['aria-pressed'], false)
  assert.equal(toggle.props!.disabled, false)
  await (toggle.props!.onClick as () => Promise<void>)()
  assert.equal(calls[0]!.action, 'enable')
  assert.equal('enabled' in calls[0]! && calls[0]!.enabled, false)
})

test('imported JSON is submitted once for backend validation while readonly and malformed input stay local', async (context) => {
  const commands: CalibrationCommand[] = []
  const props = reactive({ mode: 'cal-use', status: { ...instrumentStatus(), calibration: calibration(false) }, canCalibrate: true, busy: false,
    command: async (command: CalibrationCommand) => { commands.push(command); return null }, exportSet: async () => ({ schema: 1 }) })
  const harness = await sfcHarness('src/components/softtools/CalibrationSofttool.vue', props, {})
  context.after(harness.dispose)
  const importer = harness.bindings.importSelected as (event: Event) => Promise<void>
  function input(contents: string): Event {
    return { target: { value: 'file.json', files: [{ size: contents.length, text: async () => contents }] } } as unknown as Event
  }
  await importer(input('{"schema":1,"id":"1"}'))
  assert.equal(commands.length, 1)
  assert.equal(commands[0]!.action, 'import')
  props.canCalibrate = false
  await importer(input('{"schema":1}'))
  assert.equal(commands.length, 1)
  props.canCalibrate = true
  await importer(input('[1,2]'))
  assert.equal(commands.length, 1)
  await importer(input('{broken'))
  assert.equal(commands.length, 1)
})

test('an existing faulted or readonly calibration session can be reopened without enabling acquisition writes', async (context) => {
  const props = reactive({ mode: 'cal', status: { ...instrumentStatus(), state: 'Faulted', calibration: calibration() },
    canCalibrate: false, busy: false, command: async () => { assert.fail('View-only entry must not write'); return null }, exportSet: async () => ({ schema: 1 }) })
  const harness = await sfcHarness('src/components/softtools/CalibrationSofttool.vue', props, {})
  context.after(harness.dispose)
  const entry = findElements(harness.render(), (node) => node.type === 'button' && String(node.props?.class).includes('softtool-primary'))[0]!
  assert.equal(entry.props!.disabled, false)
  ;(entry.props!.onClick as () => void)()
  assert.equal(harness.emitted[0]![0], 'start')
})

test('begin and a partial standard capture report their action rather than completion of the whole calibration', async (context) => {
  const status = instrumentStatus()
  status.calibration = calibration(false)
  let operationId = 0
  const api = new InstrumentApi({ storage: null, fetch: async (input, init) => {
    const path = String(input)
    if (path.endsWith('/control/claim')) return new Response(JSON.stringify({ sessionId: 'token', hasControl: true, leaseSeconds: 30 }))
    if (path.endsWith('/calibration')) {
      const command = JSON.parse(String(init?.body)) as CalibrationCommand
      if (command.action === 'begin') status.calibration = calibration()
      else if (command.action === 'capture') status.calibration!.steps[0]!.captured = true
      return new Response(JSON.stringify({ operationId: String(++operationId), state: 'pending' }), { status: 202 })
    }
    if (path.includes('/operations/')) return new Response(JSON.stringify({ operationId: String(operationId), state: 'completed', result: { ok: true, errorCode: 0, message: '' } }))
    const frame = acquiredFrame()
    frame.calibrationRevision = status.calibration!.revision
    return new Response(JSON.stringify(legacyProtocolFields(path.endsWith('/frame') ? frame : status)))
  } })
  const model = useInstrument({ api, pollMs: 100000 })
  context.after(model.stop)
  model.start()
  await model.refresh()
  await model.calibrate({ action: 'begin', method: 'onePort', port1: 1 })
  assert.match(model.notice.value!.text, /创建校准会话/)
  await model.calibrate({ action: 'capture', sessionId: '6', stepId: 'step-0' })
  assert.match(model.notice.value!.text, /采集标准 Open（P1）/)
  assert.doesNotMatch(model.notice.value!.text, /Calibration 完成|应用校准|step-0/)
  assert.equal(model.status.value!.calibration!.collecting, true)
  assert.equal(model.status.value!.calibration!.steps.filter((step) => step.captured).length, 1)
})
