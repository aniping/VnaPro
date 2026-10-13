import assert from 'node:assert/strict'
import test from 'node:test'
import { reactive, type Ref, type VNode } from 'vue'
import { instrumentStatus, presentation, reflection } from './displayFixtures'
import { findElements, sfcHarness } from './sfcHarness'

function hasClass(node: VNode, name: string) {
  return String(node.props?.class).split(/\s+/).includes(name)
}

async function pane(height: number) {
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
  return {
    ...view,
    props,
    labels: () =>
      findElements(view.render(), (node) => hasClass(node, 'y-tick')),
  }
}

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
            ? height - 3 - 14
            : parseFloat(node.props!.style.top) - 7
        return { top, bottom: top + 14 }
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
