import assert from 'node:assert/strict'
import test from 'node:test'
import { reactive } from 'vue'
import { findElements, sfcHarness } from './sfcHarness'

function contents(value: unknown): string {
  if (typeof value === 'string') return value
  if (Array.isArray(value)) return value.map(contents).join('')
  return value && typeof value === 'object'
    ? contents(Reflect.get(value, 'children'))
    : ''
}

for (const purpose of ['measurement', 'new'] as const) {
  test(`${purpose} 参数弹框直接选择十六项，保持原机标题且没有 OK 底栏`, async () => {
    const props = reactive({
      purpose,
      portCount: 4,
      parameter: 'S11',
      disabled: false,
      busy: false,
      error: '',
    })
    const dialog = await sfcHarness(
      'src/components/softtools/SParameterDialog.vue',
      props,
      {},
    )
    try {
      const buttons = findElements(
        dialog.render(),
        (node) => node.props?.['data-parameter'],
      )
      assert.equal(buttons.length, 16)
      assert.deepEqual(
        buttons.map((node) => node.props!['data-parameter']),
        [
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
        ],
      )
      const title = findElements(
        dialog.render(),
        (node) => node.type === 'h2',
      )[0]!
      assert.equal(
        contents(title),
        purpose === 'new' ? 'New Trace' : 'S-Parameters',
      )
      assert.equal(
        findElements(dialog.render(), (node) => node.type === 'footer').length,
        0,
      )
      assert.equal(buttons[0]!.props!['aria-pressed'], true)
      buttons[11]!.props!.onClick()
      assert.deepEqual(dialog.emitted, [['select', 'S34']])
      assert.equal(
        props.parameter,
        'S11',
        '参数只来自设备读回，点选不修改正式参数',
      )
    } finally {
      dialog.dispose()
    }
  })
}

test('只读参数弹框仍能关闭，只有已受理操作的 busy 状态阻止 X 和 Esc', async () => {
  const props = reactive({
    portCount: 2,
    parameter: 'S11',
    disabled: true,
    busy: false,
    error: '设备拒绝更新',
  })
  const dialog = await sfcHarness(
    'src/components/softtools/SParameterDialog.vue',
    props,
    {},
  )
  try {
    const parameters = findElements(
      dialog.render(),
      (node) => node.props?.['data-parameter'],
    )
    assert.deepEqual(
      parameters.map((node) => node.props!['data-parameter']),
      ['S11', 'S12', 'S21', 'S22'],
    )
    assert.ok(parameters.every((node) => node.props!.disabled))
    const grid = findElements(
      dialog.render(),
      (node) => node.props?.['aria-label'] === 'S-Parameter matrix',
    )[0]!
    assert.equal(
      grid.props!.style.gridTemplateColumns,
      'repeat(2, minmax(0, 1fr))',
    )
    parameters[0]!.props!.onClick()
    const close = findElements(
      dialog.render(),
      (node) => node.props?.['aria-label'] === 'Close S parameters',
    )[0]!
    assert.equal(close.props!.disabled, false)
    close.props!.onClick()
    assert.deepEqual(dialog.emitted, [['close']])
    props.busy = true
    const busyClose = findElements(
      dialog.render(),
      (node) => node.props?.['aria-label'] === 'Close S parameters',
    )[0]!
    assert.equal(busyClose.props!.disabled, true)
    busyClose.props!.onClick()
    dialog.render().props!.onCancel({ preventDefault() {} })
    assert.deepEqual(dialog.emitted, [['close']])
    assert.equal(
      findElements(dialog.render(), (node) => node.props?.role === 'alert')[0]!
        .children,
      '设备拒绝更新',
    )
  } finally {
    dialog.dispose()
  }
})

test('能力从四端口变两端口后，过期矩阵按钮不会提交 S44', async () => {
  const props = reactive({
    portCount: 4,
    disabled: false,
    busy: false,
    error: '',
  })
  const dialog = await sfcHarness(
    'src/components/softtools/SParameterDialog.vue',
    props,
    {},
  )
  try {
    const old = findElements(
      dialog.render(),
      (node) => node.props?.['data-parameter'] === 'S44',
    )[0]!
    props.portCount = 2
    old.props!.onClick()
    assert.deepEqual(dialog.emitted, [])
    const current = findElements(
      dialog.render(),
      (node) => node.props?.['data-parameter'] === 'S22',
    )[0]!
    current.props!.onClick()
    assert.deepEqual(dialog.emitted, [['select', 'S22']])
  } finally {
    dialog.dispose()
  }
})
