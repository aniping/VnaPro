import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive, type VNode } from 'vue'
import { decodeCalibration } from '../src/api/calibrationDecoders'
import type { CalibrationApplicability, CalibrationCommand, CalibrationSnapshot } from '../src/api/calibrationTypes'
import type { InstrumentStatus, PendingOperation } from '../src/api/types'
import { applicabilityLabel, applicabilityMapping, calibrationEffective, calibrationStateLabel } from '../src/domain/calibrationApplicability'
import { instrumentStatus } from './displayFixtures'
import { findComponent, findElements, sfcHarness } from './sfcHarness'

function applicability(overrides: Partial<CalibrationApplicability> = {}): CalibrationApplicability {
  return {
    applicable: true, prepared: true, frequencyMode: 'exact', reasonCode: 'none', reason: '',
    sourcePoints: 800, targetPoints: 800, exactPoints: 800, interpolatedPoints: 0,
    targetStartHz: 10e6, targetStopHz: 40e9, targetIfBandwidthHz: 1000,
    targetMinPowerDbm: -10, targetMaxPowerDbm: -10, warnings: [], ...overrides,
  }
}

function calibration(app = applicability()): CalibrationSnapshot {
  return {
    revision: '5', collecting: false, capturePending: false, sessionId: '0', method: 'fourPort',
    port1: 1, port2: 4, steps: [], selectedSetId: '1', enabled: true, compatible: app.applicable,
    effective: app.applicable && app.prepared, applicability: app, reason: app.reason,
    sets: ['1', '2'].map((id) => ({
      id, name: `Cable ${id}`, method: 'fourPort', port1: 1, port2: 4,
      compatible: app.applicable, startHz: 10e6, stopHz: 40e9, points: 800, ifBandwidthHz: 1000,
      powerMinDbm: -10, powerMaxDbm: -10, deviceIdentity: 'simulator:four-port', includeIsolation: true,
      applicability: { ...app, prepared: id === '1' && app.prepared },
    })),
  }
}

function text(node: VNode): string {
  if (typeof node.children === 'string') return node.children
  return Array.isArray(node.children)
    ? node.children.map((child) => typeof child === 'object' && child !== null ? text(child as VNode) : String(child ?? '')).join('')
    : ''
}

type Harness = Awaited<ReturnType<typeof sfcHarness>>
function elements(harness: Harness, id: string) {
  return findElements(harness.render(), (node) => node.props?.['data-testid'] === id)
}
function element(harness: Harness, id: string) {
  const node = elements(harness, id)[0]
  assert.ok(node, `Expected ${id}`)
  return node
}

test('applicability protocol preserves backend mappings and rejects malformed or inconsistent supplied states', () => {
  const original = calibration()
  const decoded = decodeCalibration(original)
  assert.deepEqual(decoded.applicability, original.applicability)
  assert.equal(decoded.effective, true)
  assert.equal(decoded.sets[0]!.powerMinDbm, -10)
  assert.equal(decoded.sets[0]!.deviceIdentity, 'simulator:four-port')
  for (const change of [
    { frequencyMode: 'nearest' }, { reasonCode: 'unknown' }, { sourcePoints: 10002 },
    { exactPoints: 799 }, { targetStartHz: Number.NaN }, { targetMinPowerDbm: 10, targetMaxPowerDbm: -10 },
    { frequencyMode: 'subset', interpolatedPoints: 1 }, { frequencyMode: 'interpolated' },
    { prepared: true, applicable: false }, { warnings: ['ifbwChanged', 'ifbwChanged'] },
    { warnings: ['unverified'] }, { targetIfBandwidthHz: undefined },
  ]) assert.throws(() => decodeCalibration({ ...original, applicability: { ...original.applicability, ...change } }), /calibration/)
  assert.throws(() => decodeCalibration({ ...original, enabled: false, effective: true }), /effective/)
  assert.throws(() => decodeCalibration({ ...original, sets: [{ ...original.sets[0], powerMinDbm: 0, powerMaxDbm: -1 }] }), /power range/)
  const rejectedAxis = applicability({ applicable: false, prepared: false, frequencyMode: 'none',
    reasonCode: 'invalidAxis', reason: 'Target frequencies are not increasing', targetStartHz: 20e9, targetStopHz: 10e9,
    exactPoints: 0 })
  assert.equal(decodeCalibration(calibration(rejectedAxis)).applicability?.reasonCode, 'invalidAxis',
    'a rejected target axis remains displayable instead of turning a valid error report into a connection failure')
})

test('legacy fixtures keep the existing enabled-compatible fallback without inventing a mapping', () => {
  const original = calibration()
  delete original.effective
  delete original.applicability
  original.sets.forEach((set) => { delete set.applicability })
  const decoded = decodeCalibration(original)
  assert.equal(calibrationEffective(decoded), true)
  assert.equal(decoded.applicability, undefined)
  assert.equal(calibrationStateLabel(decoded), 'User Cal On')
  assert.equal(applicabilityMapping(decoded.applicability), '')
  assert.equal(calibrationEffective({ ...decoded, effective: false }), false, 'an explicit backend Off cannot fall back to legacy booleans')
})

test('a backend exact subset uses original values and is never labeled interpolated from changed point count or range', () => {
  const app = applicability({ frequencyMode: 'subset', targetPoints: 201, exactPoints: 201, targetStartHz: 1e9, targetStopHz: 10e9 })
  assert.equal(applicabilityLabel(app, true), '精确子集')
  assert.match(applicabilityMapping(app), /201.*直接取值，无插值/)
  assert.match(calibrationStateLabel(calibration(app)), /精确子集/)
  assert.doesNotMatch(calibrationStateLabel(calibration(app)), /插值校正/)
  assert.equal(applicabilityLabel(undefined, true), '适用', 'point-count differences alone never create a subset verdict')
})

test('selected conditions distinguish preflight from prepared coefficients and show IFBW and interpolation warnings', async (context) => {
  const app = applicability({ prepared: false, frequencyMode: 'interpolated', targetPoints: 4000,
    exactPoints: 2, interpolatedPoints: 3998, targetIfBandwidthHz: 100, warnings: ['ifbwChanged', 'interpolation'] })
  assert.equal(applicabilityLabel(app, true), '需插值')
  assert.equal(applicabilityLabel({ ...app, prepared: true }, true), '插值校正')
  const props = reactive({ set: calibration(app).sets[1]! })
  const harness = await sfcHarness('src/components/softtools/CalibrationApplicabilityDetails.vue', props, {})
  context.after(harness.dispose)
  const content = text(harness.render())
  assert.match(content, /校准时.*当前扫频/)
  assert.match(content, /800.*4000/)
  assert.match(content, /1 kHz.*100 Hz/)
  assert.match(content, /-10 dBm/)
  assert.match(content, /适用性预检通过.*应用时才准备并校验/)
  assert.match(content, /2 个点直接取值，3998 个点.*插值/)
  assert.equal(elements(harness, 'calibration-condition-warning').length, 2)
  assert.match(content, /更窄的测量带宽不能消除校准时的噪声/)
  assert.match(content, /不能保证与原校准点相同的校正精度/)
  assert.match(content, /幅度与相位.*线性插值.*建议按目标扫频重新校准/)
  assert.doesNotMatch(content, /实部、虚部|厂商等效|厂家等效/)
  props.set.applicability = { ...app, prepared: true }
  await nextTick()
  assert.match(text(element(harness, 'calibration-preparation')), /已准备完成/)
})

test('out-of-range candidates explain full correction rejection and cannot invoke activation', async (context) => {
  const app = applicability({ applicable: false, prepared: false, frequencyMode: 'none', reasonCode: 'outOfRange',
    reason: 'The requested axis exceeds the calibrated range', targetStopHz: 50e9, exactPoints: 0 })
  const props = reactive({ calibration: calibration(app), channelId: 1, selectedId: '2', disabled: false, busy: false, error: '' })
  const manager = await sfcHarness('src/components/softtools/CalibrationSetManager.vue', props, {})
  context.after(manager.dispose)
  assert.match(text(manager.render()), /频率越界/)
  assert.match(text(element(manager, 'calibration-current-badge')), /通道所选/)
  assert.equal(element(manager, 'calibration-apply').props!.disabled, true)
  ;(element(manager, 'calibration-apply').props!.onClick as () => void)()
  assert.deepEqual(manager.emitted, [])
  const details = await sfcHarness('src/components/softtools/CalibrationApplicabilityDetails.vue', { set: props.calibration.sets[1] }, {})
  context.after(details.dispose)
  assert.match(text(element(details, 'calibration-applicability-blocked')), /exceeds.*不进行外推或局部校正/)
})

test('effective calibration keeps IFBW warnings visible and follows changed SCPI snapshots', async (context) => {
  const app = applicability({ frequencyMode: 'subset', targetPoints: 201, exactPoints: 201, targetIfBandwidthHz: 100, warnings: ['ifbwChanged'] })
  const props = reactive({ mode: 'cal-use', status: { ...instrumentStatus(), calibration: calibration(app) },
    canCalibrate: false, busy: false, command: async () => null, exportSet: async () => ({}) })
  const harness = await sfcHarness('src/components/softtools/CalibrationSofttool.vue', props, {})
  context.after(harness.dispose)
  assert.match(text(element(harness, 'calibration-correction-state')), /User Cal On.*精确子集/)
  assert.match(text(element(harness, 'calibration-active-warning')), /IFBW.*更窄/)
  assert.match(text(element(harness, 'calibration-active-mapping')), /无插值/)
  props.status.calibration = calibration({ ...app, applicable: false, prepared: false, frequencyMode: 'none',
    reasonCode: 'powerMismatch', reason: 'Stimulus power differs from calibration', exactPoints: 0, warnings: [] })
  await nextTick()
  assert.match(text(element(harness, 'calibration-correction-state')), /Cal Off.*不适用/)
  assert.match(text(harness.render()), /恢复与校准时一致的激励功率/)
  assert.equal(elements(harness, 'calibration-active-warning').length, 0)
})

test('activation remains atomic and pending stages wait for readback while failures keep the previous correction', async (context) => {
  let complete!: (result: InstrumentStatus | null) => void
  const commands: CalibrationCommand[] = []
  const props = reactive({ mode: 'cal-use', status: { ...instrumentStatus(), calibration: calibration() },
    canCalibrate: true, busy: false, operation: null as PendingOperation | null, operationError: '',
    command: (command: CalibrationCommand) => { commands.push(command); return new Promise<InstrumentStatus | null>((resolve) => { complete = resolve }) },
    exportSet: async () => ({}) })
  const harness = await sfcHarness('src/components/softtools/CalibrationSofttool.vue', props, {})
  context.after(harness.dispose)
  ;(element(harness, 'calibration-manager-entry').props!.onClick as () => void)()
  await nextTick()
  const manager = () => findComponent(harness.render(), 'CalibrationSetManager')!
  const busyText = () => String(manager().props!.busyText ?? manager().props!['busy-text'])
  ;(manager().props!['onUpdate:selectedId'] as (id: string) => void)('2')
  const pending = (manager().props!.onApply as () => Promise<void>)()
  await nextTick()
  assert.deepEqual(JSON.parse(JSON.stringify(commands)), [{ action: 'activate', setId: '2' }])
  assert.equal(manager().props!.busy, true)
  assert.match(busyText(), /正在提交/)
  props.operation = { id: '20', label: 'Apply', kind: 'calibration', ownerSessionId: 'token', readingBack: false }
  await nextTick()
  assert.match(busyText(), /正在准备校准系数/)
  props.operation.readingBack = true
  await nextTick()
  assert.match(busyText(), /正在读回并确认/)
  assert.match(text(element(harness, 'current-calibration-set')), /Cable 1.*User Cal On/)
  props.operationError = 'Prepared calibration failed mathematical validation'
  complete(null)
  await pending
  assert.match(text(element(harness, 'current-calibration-set')), /Cable 1.*User Cal On/)
  assert.match(String(manager().props!.error), /mathematical validation/)
  assert.equal(manager().props!.selectedId ?? manager().props!['selected-id'], '2')
  assert.equal(commands.length, 1, 'no follow-up enable or select request can partially apply a calibration')
})

test('an activation result from the previous channel cannot replace the new channel candidate or error', async (context) => {
  let complete!: (result: InstrumentStatus | null) => void
  const props = reactive({ mode: 'cal-use', status: { ...instrumentStatus(), activeChannelId: 1, calibration: calibration() },
    canCalibrate: true, busy: false, command: () => new Promise<InstrumentStatus | null>((resolve) => { complete = resolve }), exportSet: async () => ({}) })
  const harness = await sfcHarness('src/components/softtools/CalibrationSofttool.vue', props, {})
  context.after(harness.dispose)
  const pending = (harness.bindings.act as (command: CalibrationCommand) => Promise<void>)({ action: 'activate', setId: '2' })
  props.status = { ...props.status, activeChannelId: 2, calibration: { ...calibration(), selectedSetId: '2' } }
  await nextTick()
  complete({ ...instrumentStatus(), activeChannelId: 1, calibration: calibration() })
  await pending
  assert.match(text(element(harness, 'current-calibration-set')), /Ch2.*Cable 2/)
  assert.equal((harness.bindings.selected as { value: string }).value, '2')
  assert.equal((harness.bindings.error as { value: string }).value, '')
})

test('persistent calibration is not hidden by operation notices or errors and becomes unconfirmed offline', async (context) => {
  const props = reactive({ connected: true, coreState: 'Ready', mode: 'HOLD', controller: 'scpi', hasControl: false,
    claimBusy: false, frameSeq: '1', runId: '1', revision: '1', notice: 'Old operation completed', noticeKind: 'success',
    error: '', calibrationLabel: 'Cable 1 · IFBW changed', calibrationState: 'User Cal On · 精确子集 · IFBW 变化', calibrationTone: 'warning' })
  const harness = await sfcHarness('src/components/instrument/InstrumentStatusBar.vue', props, {})
  context.after(harness.dispose)
  assert.match(text(harness.render()), /Old operation completed/)
  assert.match(text(element(harness, 'persistent-calibration-state')), /精确子集.*IFBW 变化/)
  props.error = 'Operation failed'
  props.calibrationState = 'Cal Off · 校准不适用'
  await nextTick()
  assert.match(text(harness.render()), /Operation failed/)
  assert.match(text(element(harness, 'persistent-calibration-state')), /Cal Off/)
  props.connected = false
  await nextTick()
  assert.equal(text(element(harness, 'persistent-calibration-state')), 'Cal · 状态未确认')
  assert.doesNotMatch(text(element(harness, 'persistent-calibration-state')), /User Cal On/)
})
