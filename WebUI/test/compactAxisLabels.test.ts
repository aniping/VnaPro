import assert from 'node:assert/strict'
import test from 'node:test'
import { reactive, type Ref, type VNode } from 'vue'
import { instrumentStatus, presentation, reflection } from './displayFixtures'
import { findElements, sfcHarness } from './sfcHarness'

function hasClass(node: VNode, name: string) {
  return String(node.props?.class).split(/\s+/).includes(name)
}

async function pane(height: number, width = 700) {
  const status = instrumentStatus([reflection])
  const props = reactive({
    number: 1,
    layout: 'grid',
    frame: null,
    measurements: [reflection],
    allMeasurements: [reflection],
    presentations: { Reflect: presentation() },
    activeTraceName: 'Reflect',
    primaryTraceName: 'Reflect',
    selectedDiagram: 1,
    config: status.config,
    frameCondition: { kind: 'empty', label: '', detail: '' },
    canEditDisplay: true,
    commitMarker: async () => true,
  })
  const view = await sfcHarness(
    'src/components/diagrams/DiagramPane.vue',
    props,
    {
      window: { devicePixelRatio: 1 },
      requestAnimationFrame: () => 1,
      cancelAnimationFrame() {},
    },
  )
  ;(view.bindings.height as Ref<number>).value = height
  ;(view.bindings.width as Ref<number>).value = width
  return {
    ...view,
    props,
    labels: () =>
      findElements(view.render(), (node) => hasClass(node, 'y-tick')),
  }
}

test('四乘四小窗的轴字号小于标题，大窗保持原字号，缩放不改变后端状态', async () => {
  const small = await pane(150, 230)
  const large = await pane(500, 700)
  try {
    const before = JSON.stringify(small.props)
    const font = (view: typeof small) =>
      parseFloat(view.render().props!.style['--diagram-axis-font-size'])
    assert.ok(font(small) >= 9 && font(small) < 10)
    assert.equal(font(large), 11)
    ;(small.bindings.width as Ref<number>).value = 700
    ;(small.bindings.height as Ref<number>).value = 500
    assert.equal(font(small), 11)
    assert.equal(JSON.stringify(small.props), before)
    assert.deepEqual(small.emitted, [])
  } finally {
    small.dispose()
    large.dispose()
  }
})

test('正常大图保持十一项 Y 标签及后端刻度范围', async () => {
  const view = await pane(500)
  try {
    const before = JSON.stringify(view.props)
    const labels = view.labels()
    assert.equal(labels.length, 11)
    assert.equal(labels[0]!.children, '10dB')
    assert.equal(labels.at(-1)!.children, '-90dB')
    assert.equal(JSON.stringify(view.props), before)
  } finally {
    view.dispose()
  }
})

test('短图窗仅减少标签密度，保留参考值而且不把它误认为顶部标签', async () => {
  const height = 110
  const view = await pane(height)
  try {
    const before = JSON.stringify(view.props)
    const labels = view.labels()
    assert.ok(labels.length < 11)
    const labelHeight = (view.bindings.axisLabelHeight as Ref<number>).value
    const reference = labels.find((node) => hasClass(node, 'reference-tick'))!
    assert.ok(reference)
    assert.equal(reference.children, '0dB')
    assert.equal(hasClass(reference, 'edge-top'), false)
    assert.equal(reference.props!.style.top, '11.8px')
    const bounds = labels
      .map((node) => {
        const top = hasClass(node, 'edge-top')
          ? 3
          : hasClass(node, 'edge-bottom')
            ? height - 3 - labelHeight
            : parseFloat(node.props!.style.top) - labelHeight / 2
        return { top, bottom: top + labelHeight }
      })
      .sort((a, b) => a.top - b.top)
    bounds
      .slice(1)
      .forEach((box, index) => assert.ok(box.top >= bounds[index]!.bottom + 2))
    assert.equal(JSON.stringify(view.props), before)
    assert.deepEqual(view.emitted, [])
  } finally {
    view.dispose()
  }
})
