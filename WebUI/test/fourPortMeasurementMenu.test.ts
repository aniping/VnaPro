import assert from 'node:assert/strict'
import test from 'node:test'
import { reactive, type VNode } from 'vue'
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
import { findElements, sfcHarness } from './sfcHarness'

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

class SelectInput {
  constructor(public value: string) {}
}

test('四端口 Meas 保留原机四项快捷键，完整选择器提供十六项', async () => {
  const props = menuProps()
  const menu = await sfcHarness(
    'src/components/softtools/MeasurementSofttool.vue',
    props,
    {},
  )
  try {
    const before = JSON.stringify(props.status)
    const buttons = parameterButtons(menu.render)
    assert.deepEqual(
      buttons.map((button) => textContent(button).trim()),
      ['S11', 'S12', 'S21', 'S22'],
    )
    assert.ok(buttons.every((button) => button.props!.disabled === false))
    const options = findElements(
      menu.render(),
      (node) => node.type === 'option' && node.props?.value !== '',
    )
    assert.deepEqual(
      options.map((option) => option.props!.value),
      matrix,
    )
    assert.equal(JSON.stringify(props.status), before)
  } finally {
    menu.dispose()
  }
})

test('两端口选择器仅提供四项，旧四端口事件和任意非法值不能发出命令', async () => {
  const props = menuProps()
  const requests: MeasurementCandidate[] = []
  props.update = async (candidate) => {
    requests.push({ ...candidate })
    return null
  }
  const menu = await sfcHarness(
    'src/components/softtools/MeasurementSofttool.vue',
    props,
    { HTMLSelectElement: SelectInput },
  )
  try {
    const oldSelect = findElements(
      menu.render(),
      (node) => node.type === 'select',
    )[0]!
    props.status.capabilities.portCount = 2
    assert.deepEqual(
      parameterButtons(menu.render).map((button) => textContent(button).trim()),
      ['S11', 'S12', 'S21', 'S22'],
    )
    const options = findElements(
      menu.render(),
      (node) => node.type === 'option' && node.props?.value !== '',
    )
    assert.deepEqual(
      options.map((option) => option.props!.value),
      ['S11', 'S12', 'S21', 'S22'],
    )
    for (const value of ['S44', 'S33', 'S99', 'S1', 'RAW'])
      oldSelect.props!.onChange({ target: new SelectInput(value) })
    assert.deepEqual(requests, [])
  } finally {
    menu.dispose()
  }
})

test('连续扫描期间选择 S34 只提交规范名称和目录版本，标题等待读回才变化', async () => {
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
    { HTMLSelectElement: SelectInput },
  )
  try {
    const before = JSON.stringify(props.status)
    const select = findElements(
      menu.render(),
      (node) => node.type === 'select',
    )[0]!
    select.props!.onChange({ target: new SelectInput('S34') })
    assert.deepEqual(requests, [
      { candidate: { name: 'Reflect', parameter: 'S34' }, revision: '27' },
    ])
    assert.equal(JSON.stringify(props.status), before)
    assert.ok(
      parameterButtons(menu.render).every((node) => node.props!.disabled),
    )
    const readback = instrumentStatus([{ ...parameter(3, 4), name: 'Reflect' }])
    finish(readback)
    await new Promise((resolve) => setImmediate(resolve))
    assert.equal(props.status.measurements[0]!.parameter, 'S11')
    props.status = readback
    assert.equal(
      findElements(menu.render(), (node) => node.type === 'select')[0]!.props!
        .value,
      'S34',
    )
  } finally {
    finish(null)
    menu.dispose()
  }
})

test('测量下拉在提交前恢复设备值，只读和忙碌状态阻止旧按钮事件', async () => {
  const props = menuProps()
  let writes = 0
  props.update = async () => {
    writes += 1
    return null
  }
  const menu = await sfcHarness(
    'src/components/softtools/MeasurementSofttool.vue',
    props,
    { HTMLSelectElement: SelectInput },
  )
  try {
    const select = findElements(
      menu.render(),
      (node) => node.type === 'select',
    )[0]!
    const input = new SelectInput('S44')
    select.props!.onChange({ target: input })
    assert.equal(input.value, 'S11')
    assert.equal(writes, 1)
    await new Promise((resolve) => setImmediate(resolve))
    for (const mode of ['readonly', 'busy']) {
      props.canChangeMeasurements = mode !== 'readonly'
      props.busy = mode === 'busy'
      const buttons = parameterButtons(menu.render)
      assert.ok(buttons.every((button) => button.props!.disabled))
      buttons.forEach((button) => button.props!.onClick())
      select.props!.onChange({ target: new SelectInput('S44') })
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
