import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive, type Ref, type VNode } from 'vue'
import { decodeCalibration, decodeCalibrationExport } from '../src/api/calibrationDecoders'
import type { CalibrationCommand, CalibrationSnapshot, CalibrationStep } from '../src/api/calibrationTypes'
import { calibrationConnection, calibrationMethodLabel, calibrationPorts } from '../src/domain/calibration'
import { instrumentStatus } from './displayFixtures'
import { findElements, sfcHarness } from './sfcHarness'

function fourPort(includeIsolation = false): CalibrationSnapshot {
  const steps: CalibrationStep[] = []
  function add(kind: CalibrationStep['kind'], port1: number, port2 = 0) {
    steps.push({ id: kind + '-' + port1 + '-' + port2, kind, port1, port2,
      captured: false, runId: '0', frameSeq: '0' })
  }
  for (const port of [1, 2, 3, 4])
    for (const kind of ['open', 'short', 'load'] as const) add(kind, port)
  for (const [first, second] of [[1, 2], [1, 3], [1, 4], [2, 3], [2, 4], [3, 4]] as const) {
    add('thru', first, second)
    if (includeIsolation) add('isolation', first, second)
  }
  return { revision: '5', collecting: true, capturePending: false, sessionId: '6',
    method: 'fourPort', port1: 1, port2: 4, steps, includeIsolation,
    selectedSetId: '0', enabled: false, compatible: false, reason: '', sets: [] }
}

function document() {
  return { schema: 3, id: '7', name: 'Four-port TOSM', method: 'fourPort', port1: 1, port2: 4,
    frequencyHz: [10e6, 20e6], powerDbm: [-12.375, -12.375],
    onePortTerms: [], twoPortTerms: [], fourPortTerms: [Array(96).fill(0.125), Array(96).fill(-0.25)] }
}

function text(node: VNode): string {
  if (typeof node.children === 'string') return node.children
  return Array.isArray(node.children)
    ? node.children.map((child) => typeof child === 'object' && child !== null ? text(child as VNode) : String(child ?? '')).join('')
    : ''
}

test('four-port snapshots accept all 18/24 steps and reject pair-only, missing or duplicate physical coverage', () => {
  assert.throws(() => decodeCalibration({ ...fourPort(), method: 'twoPort', port2: 2, steps: fourPort().steps.slice(0, 9) }), /calibration.steps/)
  for (const isolation of [false, true]) {
    const snapshot = fourPort(isolation)
    assert.equal(decodeCalibration(snapshot).steps.length, isolation ? 24 : 18)
    assert.throws(() => decodeCalibration({ ...snapshot, port2: 2 }), /fourPort ports/)
    assert.throws(() => decodeCalibration({ ...snapshot, steps: snapshot.steps.slice(1) }), /coverage/)
    assert.throws(() => decodeCalibration({ ...snapshot, steps: [...snapshot.steps.slice(1), { ...snapshot.steps[1]!, id: 'duplicate-physical-step' }] }), /coverage/)
    assert.throws(() => decodeCalibration(snapshot, 2), /port2|port1/)
  }
  const snapshot = fourPort()
  snapshot.collecting = false
  snapshot.sessionId = '0'
  snapshot.steps = []
  snapshot.selectedSetId = '7'
  snapshot.sets = [{ id: '7', name: 'Complete', method: 'fourPort', port1: 1, port2: 4, compatible: true }]
  assert.equal(decodeCalibration(snapshot).sets[0]!.method, 'fourPort')
  assert.equal(calibrationMethodLabel('fourPort'), 'TOSM · 4-Port')
  assert.equal(calibrationPorts('fourPort', 1, 4), 'P1, P2, P3, P4')
})

test('schema 3 preserves bounded 96-number rows and rejects malformed or mixed one/two/four-port documents', () => {
  const value = document()
  assert.equal(decodeCalibrationExport(value), value)
  for (const fields of [
    { method: 'twoPort' }, { schema: 2 }, { port2: 2 },
    { frequencyHz: Array(10002).fill(10e6) }, { powerDbm: [-12.375] },
    { onePortTerms: [[1]] }, { twoPortTerms: [[1]] },
    { fourPortTerms: [Array(96).fill(0)] },
    { fourPortTerms: [Array(95).fill(0), Array(96).fill(0)] },
    { fourPortTerms: [Array(97).fill(0), Array(96).fill(0)] },
    { fourPortTerms: [Array(96).fill(Infinity), Array(96).fill(0)] },
  ]) assert.throws(() => decodeCalibrationExport({ ...value, ...fields }), /calibration data/)
})

test('four-port setup selects all physical ports and submits a distinct full method with optional six-pair isolation', async (context) => {
  const calls: CalibrationCommand[] = []
  const status = instrumentStatus()
  status.calibration = { ...fourPort(), collecting: false, sessionId: '0', steps: [] }
  const props = reactive({ status, disabled: false, command: async (command: CalibrationCommand) => { calls.push(command); return null } })
  const harness = await sfcHarness('src/components/softtools/CalibrationWorkflow.vue', props, {})
  context.after(harness.dispose)
  const entry = findElements(harness.render(), (node) => node.props?.['data-cal-type'] === 'tosm-four-port')[0]!
  assert.equal(entry.props!.disabled, false)
  ;(entry.props!.onClick as () => void)()
  await nextTick()
  const ports = findElements(harness.render(), (node) => node.props?.['data-cal-port'] !== undefined)
  assert.deepEqual(ports.map((node) => node.props!['aria-pressed']), [true, true, true, true])
  assert.ok(ports.every((node) => node.props!.disabled))
  ;(harness.bindings.includeIsolation as Ref<boolean>).value = true
  ;(harness.bindings.next as () => void)()
  await nextTick()
  assert.match(text(harness.render()), /12 项 OSM \+ 6 对 Through \+ 6 对 Isolation/)
  assert.match(text(harness.render()), /未参与连接.*50 Ω/)
  await (harness.bindings.begin as () => Promise<void>)()
  assert.deepEqual(JSON.parse(JSON.stringify(calls)), [{ action: 'begin', method: 'fourPort', port1: 1, port2: 4, includeIsolation: true }])
})

test('two-port instruments disable four-port setup and cannot submit it through a synthetic click', async (context) => {
  const status = instrumentStatus()
  status.capabilities.portCount = 2
  status.calibration = { ...fourPort(), method: 'twoPort', port2: 2, collecting: false, sessionId: '0', steps: [] }
  const calls: CalibrationCommand[] = []
  const props = reactive({ status, disabled: false, command: async (command: CalibrationCommand) => { calls.push(command); return null } })
  const harness = await sfcHarness('src/components/softtools/CalibrationWorkflow.vue', props, {})
  context.after(harness.dispose)
  const entry = findElements(harness.render(), (node) => node.props?.['data-cal-type'] === 'tosm-four-port')[0]!
  assert.equal(entry.props!.disabled, true)
  ;(entry.props!.onClick as () => void)()
  await nextTick()
  assert.equal((harness.bindings.method as Ref<string>).value, 'twoPort')
  assert.equal(calls.length, 0)
})

for (const isolation of [false, true]) {
  test('four-port acquisition reaches every ' + (isolation ? 24 : 18) + ' standard and only applies after all backend captures', async (context) => {
    const status = instrumentStatus()
    status.calibration = fourPort(isolation)
    const calls: CalibrationCommand[] = []
    const props = reactive({ status, disabled: false,
      command: async (command: CalibrationCommand) => {
        calls.push(command)
        if (command.action === 'capture') {
          const step = props.status.calibration!.steps.find((entry) => entry.id === command.stepId)!
          step.captured = true
        }
        return props.status
      },
      loadPreview: async (_session: string, stepId: string) => ({ available: false as const,
        sessionId: '6', stepId, calibrationRevision: '5', reason: 'Preview not yet read' }),
    })
    const harness = await sfcHarness('src/components/softtools/CalibrationAcquisition.vue', props, {})
    context.after(harness.dispose)
    const groups = findElements(harness.render(), (node) => node.props?.['data-cal-group'] !== undefined)
    assert.deepEqual(groups.map((node) => node.props!['data-cal-group']),
      ['port-1', 'port-2', 'port-3', 'port-4', 'pair-1-2', 'pair-1-3', 'pair-1-4', 'pair-2-3', 'pair-2-4', 'pair-3-4'])
    const apply = () => findElements(harness.render(), (node) => node.type === 'button' && text(node).trim() === 'Apply')[0]!
    assert.equal(apply().props!.disabled, true)
    for (const step of props.status.calibration!.steps) {
      const button = findElements(harness.render(), (node) => node.props?.['data-cal-step'] === step.id)[0]!
      assert.equal(button.props!.disabled, false)
      assert.match(String(button.props!.title), /其余未参与.*50 Ω/)
      await (button.props!.onClick as () => Promise<void>)()
      await nextTick()
      assert.equal((calls.at(-1) as { stepId: string }).stepId, step.id)
    }
    assert.equal(calls.length, isolation ? 24 : 18)
    assert.equal(apply().props!.disabled, false)
    assert.match(text(harness.render()), isolation ? /24 \/ 24/ : /18 \/ 18/)
    assert.match(calibrationConnection(props.status.calibration!.steps.find((step) => step.port1 === 2 && step.port2 === 3)!, 'fourPort'), /P2.*P3/)
  })
}

test('schema 3 import rejects malformed rows before sending and keeps valid four-port coefficients intact', async (context) => {
  const commands: CalibrationCommand[] = []
  const props = reactive({ mode: 'cal-use', status: { ...instrumentStatus(), calibration: fourPort() }, canCalibrate: true, busy: false,
    command: async (command: CalibrationCommand) => { commands.push(command); return null }, exportSet: async () => ({ schema: 3 }) })
  props.status.calibration.collecting = false
  const harness = await sfcHarness('src/components/softtools/CalibrationSofttool.vue', props, {})
  context.after(harness.dispose)
  const importer = harness.bindings.importSelected as (event: Event) => Promise<void>
  function input(data: unknown, size?: number): Event {
    const contents = JSON.stringify(data)
    return { target: { value: 'four-port.json', files: [{ size: size ?? contents.length, text: async () => contents }] } } as unknown as Event
  }
  await importer(input({ ...document(), fourPortTerms: [[1]] }))
  assert.equal(commands.length, 0)
  await importer(input(document(), 32 * 1024 * 1024 + 1))
  assert.equal(commands.length, 0)
  await importer(input(document(), 20 * 1024 * 1024))
  assert.equal(commands.length, 1)
  assert.equal(commands[0]!.action, 'import')
  assert.deepEqual(JSON.parse(JSON.stringify('data' in commands[0]! && commands[0].data)), document())
})
