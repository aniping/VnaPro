import assert from 'node:assert/strict'
import test from 'node:test'
import { reactive } from 'vue'
import { InstrumentApi } from '../src/api/instrumentApi'
import type { InstrumentStatus, MeasurementCandidate } from '../src/api/types'
import { instrumentStatus } from './displayFixtures'
import { findComponent, findElements, sfcHarness } from './sfcHarness'

function menuProps(portCount = 4) {
  const status = instrumentStatus()
  status.capabilities.portCount = portCount
  status.revision = '31'
  status.display.revision = '71'
  status.config.sweepMode = 'CONT'
  return reactive({
    status,
    activeTraceName: 'Reflect',
    mode: 'meas',
    canChangeMeasurements: true,
    busy: false,
    update: async (_candidate: MeasurementCandidate, _revision: string) => null,
    allSParameters: async (
      _revision: string,
      _displayRevision: string,
    ): Promise<InstrumentStatus | null> => null,
  })
}

function workflowProps(portCount = 4) {
  const props = menuProps(portCount)
  return reactive({
    status: props.status,
    activeTraceName: props.activeTraceName,
    disabled: false,
    create: props.allSParameters,
  })
}

function confirmation(view: Awaited<ReturnType<typeof sfcHarness>>) {
  const child = findComponent(view.render(), 'ConfirmationDialog')
  assert.ok(child)
  return child.props!
}

test('All S-Params 传输只发送一次已确认双 CAS 请求，不传前端定义列表', async () => {
  const calls: { url: string; body: Record<string, unknown> }[] = []
  const api = new InstrumentApi({
    storage: null,
    fetch: async (url, init) => {
      calls.push({ url: String(url), body: JSON.parse(String(init?.body)) })
      return new Response(
        JSON.stringify({ operationId: 'all-1', state: 'pending' }),
        { status: 202 },
      )
    },
  })
  const create = Reflect.get(api, 'createAllSParameters')
  assert.equal(typeof create, 'function')
  await create.call(api, '31', '71')
  assert.equal(calls.length, 1)
  assert.equal(calls[0]!.url, '/api/v1/measurement-actions/all-sparameters')
  const { requestId, ...body } = calls[0]!.body
  assert.equal(typeof requestId, 'string')
  assert.deepEqual(body, {
    expectedRevision: '31',
    expectedDisplayRevision: '71',
    confirmed: true,
  })
})

test('Meas All S-Params 先打开确认，取消没有写请求或本地目录更改', async () => {
  const props = menuProps()
  let writes = 0
  props.allSParameters = async () => {
    writes += 1
    return null
  }
  const menu = await sfcHarness(
    'src/components/softtools/MeasurementSofttool.vue',
    props,
    {},
  )
  try {
    const before = JSON.stringify(props.status)
    const button = findElements(
      menu.render(),
      (node) => node.props?.['data-testid'] === 'all-s-parameters',
    )[0]
    assert.ok(button)
    assert.equal(button.props!.disabled, false)
    button.props!.onClick()
    const flow = findComponent(menu.render(), 'AllSParametersWorkflow')
    assert.ok(flow)
    assert.equal(writes, 0)
    flow.props!.onClose()
    assert.equal(
      findComponent(menu.render(), 'AllSParametersWorkflow'),
      undefined,
    )
    assert.equal(writes, 0)
    assert.equal(JSON.stringify(props.status), before)
  } finally {
    menu.dispose()
  }
})

test('整组创建不依赖活动测量，空图窗和空活动名称仍可打开确认', async () => {
  const props = menuProps()
  props.activeTraceName = ''
  props.status.display.selectedMeasurement = ''
  props.status.display.selectedDiagram = 2
  props.status.display.gridColumns = 2
  props.status.display.diagrams.push({
    number: 2,
    measurementNames: [],
    primaryMeasurement: '',
  })
  const menu = await sfcHarness(
    'src/components/softtools/MeasurementSofttool.vue',
    props,
    {},
  )
  try {
    const button = findElements(
      menu.render(),
      (node) => node.props?.['data-testid'] === 'all-s-parameters',
    )[0]!
    assert.equal(button.props!.disabled, false)
    button.props!.onClick()
    assert.ok(findComponent(menu.render(), 'AllSParametersWorkflow'))
  } finally {
    menu.dispose()
  }
})

test('取消或销毁整组确认会撤销意图，旧 Yes 事件不能提交', async () => {
  for (const action of ['cancel', 'dispose']) {
    const props = workflowProps()
    let writes = 0
    props.create = async () => {
      writes += 1
      return null
    }
    const view = await sfcHarness(
      'src/components/softtools/AllSParametersWorkflow.vue',
      props,
      {},
    )
    try {
      const opened = confirmation(view)
      if (action === 'cancel') opened.onCancel()
      else view.dispose()
      await opened.onConfirm()
      opened.onCancel()
      assert.equal(writes, 0)
      assert.deepEqual(view.emitted, action === 'cancel' ? [['close']] : [])
    } finally {
      view.dispose()
    }
  }
})

test('Meas 关闭再打开确认后，旧实例不能提交或关闭新的确认', async () => {
  const props = menuProps()
  const requests: string[][] = []
  props.allSParameters = async (revision, displayRevision) => {
    requests.push([revision, displayRevision])
    return props.status
  }
  const menu = await sfcHarness(
    'src/components/softtools/MeasurementSofttool.vue',
    props,
    {},
  )
  try {
    const before = JSON.stringify(props.status)
    const open = findElements(
      menu.render(),
      (node) => node.props?.['data-testid'] === 'all-s-parameters',
    )[0]!.props!.onClick
    open()
    const old = findComponent(menu.render(), 'AllSParametersWorkflow')!.props!
    open()
    assert.equal(
      findComponent(menu.render(), 'AllSParametersWorkflow')!.props!.create,
      old.create,
    )
    old.onClose()
    open()
    const current = findComponent(
      menu.render(),
      'AllSParametersWorkflow',
    )!.props!
    assert.notEqual(current.create, old.create)
    assert.equal(await old.create('31', '71'), null)
    old.onClose()
    assert.ok(findComponent(menu.render(), 'AllSParametersWorkflow'))
    assert.deepEqual(requests, [])
    assert.equal(await current.create('31', '71'), props.status)
    assert.deepEqual(requests, [['31', '71']])
    assert.equal(JSON.stringify(props.status), before)
    current.onClose()
    assert.equal(
      findComponent(menu.render(), 'AllSParametersWorkflow'),
      undefined,
    )
    menu.dispose()
    assert.equal(await current.create('31', '71'), null)
    assert.equal(requests.length, 1)
  } finally {
    menu.dispose()
  }
})

test('整组确认冻结打开时上下文，等待后端读回期间禁止重入和关闭', async () => {
  const props = workflowProps()
  const requests: string[][] = []
  let finish: (status: InstrumentStatus | null) => void = () => {}
  props.create = (revision, displayRevision) => {
    requests.push([revision, displayRevision])
    return new Promise((resolve) => {
      finish = resolve
    })
  }
  const view = await sfcHarness(
    'src/components/softtools/AllSParametersWorkflow.vue',
    props,
    {},
  )
  try {
    const before = JSON.stringify(props.status)
    const opened = confirmation(view)
    assert.match(opened.message, /删除.*测量定义.*图窗/)
    assert.match(opened.message, /16.*4×4/)
    assert.match(opened.message, /扫描设置保持/)
    const pending = opened.onConfirm()
    assert.deepEqual(requests, [['31', '71']])
    assert.equal(confirmation(view).busy, true)
    await opened.onConfirm()
    opened.onCancel()
    assert.deepEqual(view.emitted, [])
    assert.equal(requests.length, 1)
    assert.equal(JSON.stringify(props.status), before)
    finish(instrumentStatus())
    await pending
    assert.deepEqual(view.emitted, [['close']])
    assert.equal(JSON.stringify(props.status), before)
  } finally {
    finish(null)
    view.dispose()
  }
})

test('确认版本、端口或活动上下文变化后不提交，失败提示保留且可取消', async () => {
  for (const changed of ['revision', 'display', 'port', 'name', 'readonly']) {
    const props = workflowProps(2)
    let writes = 0
    props.create = async () => {
      writes += 1
      return null
    }
    const view = await sfcHarness(
      'src/components/softtools/AllSParametersWorkflow.vue',
      props,
      {},
    )
    try {
      const opened = confirmation(view)
      assert.match(opened.message, /4.*2×2/)
      if (changed === 'revision') props.status.revision = '32'
      if (changed === 'display') props.status.display.revision = '72'
      if (changed === 'port') props.status.capabilities.portCount = 4
      if (changed === 'name') props.activeTraceName = 'Transmit'
      if (changed === 'readonly') props.disabled = true
      await opened.onConfirm()
      assert.equal(writes, 0)
      assert.equal(confirmation(view).disabled, true)
      opened.onCancel()
      assert.deepEqual(view.emitted, [['close']])
    } finally {
      view.dispose()
    }
  }
  const props = workflowProps()
  const view = await sfcHarness(
    'src/components/softtools/AllSParametersWorkflow.vue',
    props,
    {},
  )
  try {
    await confirmation(view).onConfirm()
    assert.ok(confirmation(view).error)
    assert.equal(confirmation(view).busy, false)
    assert.deepEqual(view.emitted, [])
  } finally {
    view.dispose()
  }
})

test('确认框默认 No，拒绝期间可取消，执行期间 Yes、No、X、Esc 均不可重入', async () => {
  const props = reactive({
    title: 'Confirm All S-Params',
    message: '删除旧定义和图窗，扫描设置保持。',
    busy: false,
    disabled: false,
    error: '',
  })
  const dialog = await sfcHarness(
    'src/components/softtools/ConfirmationDialog.vue',
    props,
    {},
  )
  try {
    const no = findElements(
      dialog.render(),
      (node) => node.props?.['data-testid'] === 'confirmation-no',
    )[0]!
    assert.equal(no.props!.autofocus, true)
    no.props!.onClick()
    assert.deepEqual(dialog.emitted, [['cancel']])
    props.disabled = true
    findElements(
      dialog.render(),
      (node) => node.props?.['data-testid'] === 'confirmation-yes',
    )[0]!.props!.onClick()
    assert.deepEqual(dialog.emitted, [['cancel']])
    props.busy = true
    for (const button of findElements(
      dialog.render(),
      (node) => node.type === 'button',
    )) {
      assert.equal(button.props!.disabled, true)
      button.props!.onClick()
    }
    dialog.render().props!.onCancel({ preventDefault() {} })
    assert.deepEqual(dialog.emitted, [['cancel']])
  } finally {
    dialog.dispose()
  }
})
