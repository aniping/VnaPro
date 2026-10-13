import assert from 'node:assert/strict'
import test from 'node:test'
import type { Ref, VNode } from 'vue'
import type { Measurement } from '../src/api/types'
import {
  measuredTraceTitleLayout,
  type MeasuredTraceTitleLayout,
  type TraceTitleField,
} from '../src/plot/titleLayout'
import {
  instrumentStatus,
  presentation,
  reflection,
  transmission,
} from './displayFixtures'
import { findElements, sfcHarness } from './sfcHarness'

interface TitleBindings {
  titleLayout: Ref<MeasuredTraceTitleLayout>
}

interface TraceListStyle {
  gridTemplateColumns: string
  '--trace-name-width': string
  '--trace-chip-width': string
  '--trace-format-width': string
}

function hasClass(node: VNode, className: string): boolean {
  return String(node.props?.class ?? '')
    .split(/\s+/)
    .includes(className)
}

/** 固定字体测量输入，标题槽位算法仍调用生产实现。 */
function titleFields(measurement: Measurement): TraceTitleField[] {
  const values = [
    ['trace-number', measurement.name, 4, 4],
    ['measurement-chip', measurement.parameter, 4, 4],
    ['trace-format', 'dB Mag', 4, 4],
    ['trace-scale', '10 dB/', 0, 4],
    ['trace-reference', 'Ref 0 dB', 0, 4],
  ] as const
  return values.map(([kind, text, paddingLeft, paddingRight]) => ({
    kind,
    text,
    font: '11px Tahoma',
    paddingLeft,
    paddingRight,
  }))
}

async function diagram(count: 1 | 2) {
  const measurements = [
    { ...reflection, name: 'Trc1' },
    { ...transmission, name: 'Trc2' },
  ].slice(0, count)
  const status = instrumentStatus(measurements)
  const props = {
    number: 1,
    layout: 'single',
    frame: null,
    measurements,
    allMeasurements: measurements,
    presentations: Object.fromEntries(
      measurements.map((measurement) => [measurement.name, presentation()]),
    ),
    activeTraceName: measurements[count - 1]!.name,
    primaryTraceName: measurements[count - 1]!.name,
    selectedDiagram: 1,
    config: status.config,
    frameCondition: { kind: 'waiting', label: 'Waiting', detail: '' },
    canEditDisplay: true,
    commitMarker: async () => true,
  }
  const before = JSON.stringify(props)
  const layout = measuredTraceTitleLayout(
    895,
    measurements.map(titleFields),
    (text) => text.length * 6,
  )
  const harness = await sfcHarness(
    'src/components/diagrams/DiagramPane.vue',
    props,
    {
      window: { devicePixelRatio: 1 },
      requestAnimationFrame: () => 1,
      cancelAnimationFrame() {},
    },
  )
  const bindings = harness.bindings as unknown as TitleBindings
  bindings.titleLayout.value = layout
  return { harness, props, before, layout }
}

for (const count of [1, 2] as const) {
  test(`${count} 条迹线的真实图窗模板应用像素槽宽，剩余区域留空`, async () => {
    const { harness, props, before, layout } = await diagram(count)
    try {
      const list = findElements(harness.render(), (node) =>
        hasClass(node, 'trace-list'),
      )[0]
      assert.ok(list, '生产模板必须渲染迹线列表')
      const style = list.props!.style as TraceListStyle
      assert.equal(
        style.gridTemplateColumns,
        `repeat(${layout.columns}, ${layout.slotWidth}px)`,
        '标题背景应使用生产布局的像素槽宽，不能用 1fr 拉满图窗',
      )
      assert.equal(style['--trace-name-width'], `${layout.nameWidth}px`)
      assert.equal(style['--trace-chip-width'], `${layout.chipWidth}px`)
      assert.equal(
        style['--trace-format-width'],
        `${layout.formatWidth}px`,
        '格式列宽需要传入子组件，保证不同格式的字段对齐',
      )
      assert.equal(JSON.stringify(props), before, '布局不能修改后端显示 DTO')
      assert.deepEqual(harness.emitted, [])
    } finally {
      harness.dispose()
    }
  })
}

test('自然槽宽下的真实标题按钮仍只提交选择事件，不修改正式显示状态', async () => {
  const { harness, props, before } = await diagram(2)
  let child: Awaited<ReturnType<typeof sfcHarness>> | undefined
  try {
    const strips = findElements(
      harness.render(),
      (node) =>
        typeof node.type === 'object' &&
        'name' in node.type &&
        node.type.name === 'TraceStrip',
    )
    assert.equal(strips.length, 2)
    const strip = strips[1]!
    assert.equal(strip.props?.active, true)
    // 夹具直接执行 setup，补齐真实 Vue 挂载时执行的属性名称转换。
    const childProps = {
      ...strip.props,
      canEditDisplay: strip.props?.['can-edit-display'],
      smithRadius: strip.props?.['smith-radius'],
    }
    child = await sfcHarness(
      'src/components/diagrams/TraceStrip.vue',
      childProps,
      {},
    )
    const button = findElements(child.render(), (node) =>
      hasClass(node, 'trace-select'),
    )[0]
    assert.ok(button)
    assert.equal(button.props?.disabled, false)
    assert.equal(button.props?.['aria-pressed'], true)
    const select = button.props!.onClick as () => void
    select()
    assert.deepEqual(child.emitted, [['select', 'Trc2']])
    const relay = strip.props!.onSelect as (name: string) => void
    relay('Trc2')
    assert.deepEqual(harness.emitted, [['selectTrace', 'Trc2']])
    assert.equal(JSON.stringify(props), before, '前端事件不能代替 C++ 更新状态')
  } finally {
    child?.dispose()
    harness.dispose()
  }
})
