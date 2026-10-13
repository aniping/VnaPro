import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive, type VNode } from 'vue'
import { decodeFrame, decodeStatus } from '../src/api/decoders'
import type {
  InstrumentStatus,
  Measurement,
  MeasurementCandidate,
} from '../src/api/types'
import {
  acquiredFrame,
  instrumentStatus,
  legacyProtocolFields,
  receiverTrace,
  reflection,
} from './displayFixtures'
import { findComponent, findElements, sfcHarness } from './sfcHarness'

const matrix = [
  'S11',
  'S12',
  'S13',
  'S14',
  'S21',
  'S22',
  'S23',
  'S24',
  'S31',
  'S32',
  'S33',
  'S34',
  'S41',
  'S42',
  'S43',
  'S44',
]

function parameter(receiverPort: number, sourcePort: number): Measurement {
  return {
    ...reflection,
    name: `Trc${receiverPort}${sourcePort}`,
    receiverPort,
    sourcePort,
    parameter: `S${receiverPort}${sourcePort}`,
  }
}

function textContent(node: unknown): string {
  if (typeof node === 'string') return node
  if (Array.isArray(node)) return node.map(textContent).join('')
  return node && typeof node === 'object'
    ? textContent(Reflect.get(node, 'children'))
    : ''
}

function parameterButtons(render: () => VNode) {
  return findElements(
    render(),
    (node) =>
      node.type === 'button' && node.props?.['aria-pressed'] !== undefined,
  )
}

function menuProps(portCount = 4) {
  const status = instrumentStatus([reflection])
  status.capabilities.portCount = portCount
  status.config.sweepMode = 'CONT'
  status.state = 'Running'
  status.revision = '27'
  return reactive({
    status,
    activeTraceName: reflection.name,
    mode: 'meas',
    canChangeMeasurements: true,
    busy: false,
    update: async (
      _candidate: MeasurementCandidate,
      _revision: string,
    ): Promise<InstrumentStatus | null> => null,
  })
}

function openParameterDialog(menu: Awaited<ReturnType<typeof sfcHarness>>) {
  const opener = findElements(
    menu.render(),
    (node) => node.props?.['aria-label'] === 'Select S parameter',
  )[0]
  assert.ok(opener, 'Meas must open the instrument parameter dialog')
  opener.props!.onClick()
  const dialog = findComponent(menu.render(), 'SParameterDialog')
  assert.ok(dialog)
  return dialog
}

test('四端口 Meas 保留原机四项快捷键，完整参数通过仪器弹框选择', async () => {
  const props = menuProps()
  const menu = await sfcHarness(
    'src/components/softtools/MeasurementSofttool.vue',
    props,
    {},
  )
  try {
    const before = JSON.stringify(props.status)
    assert.deepEqual(
      parameterButtons(menu.render).map((button) => textContent(button).trim()),
      ['S11', 'S12', 'S21', 'S22'],
    )
    assert.equal(
      findElements(menu.render(), (node) => node.type === 'select').length,
      0,
    )
    const dialog = openParameterDialog(menu)
    assert.equal(dialog.props!['port-count'], 4)
    assert.equal(dialog.props!.parameter, 'S11')
    assert.equal(JSON.stringify(props.status), before)
  } finally {
    menu.dispose()
  }
})

test('两端口弹框及过期四端口事件不能提交跨端口和任意非法参数', async () => {
  const props = menuProps()
  const writes: MeasurementCandidate[] = []
  props.update = async (candidate) => {
    writes.push({ ...candidate })
    return null
  }
  const menu = await sfcHarness(
    'src/components/softtools/MeasurementSofttool.vue',
    props,
    {},
  )
  try {
    const old = openParameterDialog(menu)
    props.status.capabilities.portCount = 2
    await nextTick()
    assert.equal(findComponent(menu.render(), 'SParameterDialog'), undefined)
    const current = openParameterDialog(menu)
    assert.equal(current.props!['port-count'], 2)
    for (const value of ['S44', 'S33', 'S99', 'S1', 'RAW'])
      await current.props!.onSelect(value)
    await old.props!.onSelect('S44')
    assert.deepEqual(writes, [])
  } finally {
    menu.dispose()
  }
})

test('CONT 弹框选择 S34 等待后端更新及读回，完成前保持原参数并阻止取消', async () => {
  const props = menuProps()
  const requests: { candidate: MeasurementCandidate; revision: string }[] = []
  let finish: (value: InstrumentStatus | null) => void = () => {}
  props.update = (candidate, revision) => {
    requests.push({ candidate: { ...candidate }, revision })
    return new Promise((resolve) => {
      finish = resolve
    })
  }
  const menu = await sfcHarness(
    'src/components/softtools/MeasurementSofttool.vue',
    props,
    {},
  )
  try {
    const before = JSON.stringify(props.status)
    const dialog = openParameterDialog(menu)
    const pending = dialog.props!.onSelect('S34')
    assert.deepEqual(requests, [
      { candidate: { name: 'Reflect', parameter: 'S34' }, revision: '27' },
    ])
    assert.equal(JSON.stringify(props.status), before)
    const waiting = findComponent(menu.render(), 'SParameterDialog')!
    assert.equal(waiting.props!.disabled, true)
    waiting.props!.onClose()
    assert.ok(findComponent(menu.render(), 'SParameterDialog'))
    await waiting.props!.onSelect('S44')
    assert.equal(requests.length, 1)
    const readback = instrumentStatus([{ ...parameter(3, 4), name: 'Reflect' }])
    props.status = readback
    finish(readback)
    await pending
    assert.equal(findComponent(menu.render(), 'SParameterDialog'), undefined)
    const opener = findElements(
      menu.render(),
      (node) => node.props?.['aria-label'] === 'Select S parameter',
    )[0]!
    assert.equal(textContent(opener).includes('34'), true)
  } finally {
    finish(null)
    menu.dispose()
  }
})

test('弹框失败保留错误，活动迹线变更关闭草稿且只读或忙碌不能发起更新', async () => {
  const props = menuProps()
  let writes = 0
  props.update = async () => {
    writes += 1
    return null
  }
  const menu = await sfcHarness(
    'src/components/softtools/MeasurementSofttool.vue',
    props,
    {},
  )
  try {
    const dialog = openParameterDialog(menu)
    await dialog.props!.onSelect('S44')
    const failed = findComponent(menu.render(), 'SParameterDialog')!
    assert.match(failed.props!.error, /测量未更新/)
    assert.equal(failed.props!.disabled, false)
    props.activeTraceName = 'OtherTrace'
    await nextTick()
    assert.equal(findComponent(menu.render(), 'SParameterDialog'), undefined)
    await failed.props!.onSelect('S33')
    assert.equal(writes, 1)
    props.status.measurements.push({ ...reflection, name: 'OtherTrace' })
    openParameterDialog(menu)
    await failed.props!.onSelect('S22')
    assert.equal(writes, 1, '旧弹框事件不能修改重新打开后的另一条迹线')
    props.activeTraceName = reflection.name
    await nextTick()
    for (const state of ['readonly', 'busy']) {
      props.canChangeMeasurements = state !== 'readonly'
      props.busy = state === 'busy'
      const opener = findElements(
        menu.render(),
        (node) => node.props?.['aria-label'] === 'Select S parameter',
      )[0]!
      assert.equal(opener.props!.disabled, true)
      opener.props!.onClick()
      assert.equal(findComponent(menu.render(), 'SParameterDialog'), undefined)
      parameterButtons(menu.render).forEach((button) => button.props!.onClick())
      assert.equal(writes, 1)
    }
  } finally {
    menu.dispose()
  }
})
test('四端口状态和采集帧严格接受全部 Sij，仍由后端投影提供显示数据', () => {
  const measurements = matrix.map((name) =>
    parameter(Number(name[1]), Number(name[2])),
  )
  const status = instrumentStatus(measurements)
  assert.deepEqual(decodeStatus(status).measurements, measurements)
  for (const measurement of measurements) {
    const trace = receiverTrace(measurement)
    const frame = decodeFrame(acquiredFrame(trace))
    assert.equal(frame.available, true)
    if (frame.available)
      assert.deepEqual(legacyProtocolFields(frame.traces[0]), trace)
  }
})

test('二端口状态拒绝端口三和四，四端口解码仍拒绝越界及不一致的参数元数据', () => {
  for (const measurement of [
    parameter(3, 1),
    parameter(1, 3),
    parameter(4, 4),
  ]) {
    const status = instrumentStatus([measurement])
    status.capabilities.portCount = 2
    assert.throws(() => decodeStatus(status))
  }
  for (const override of [
    { receiverPort: 5, parameter: 'S54' },
    { sourcePort: 0, parameter: 'S30' },
    { parameter: 'S43' },
    { receiverKind: 'a' },
    { unit: 'a.u.' },
    { calibration: 'raw' },
  ]) {
    const malformed = { ...parameter(3, 4), ...override }
    assert.throws(() =>
      decodeStatus(instrumentStatus([malformed as Measurement])),
    )
    assert.throws(() =>
      decodeFrame(acquiredFrame(receiverTrace(malformed as Measurement))),
    )
  }
})
