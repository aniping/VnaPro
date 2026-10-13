import assert from 'node:assert/strict'
import test from 'node:test'
import { reactive } from 'vue'
import { decodeDisplay } from '../src/api/displayDecoders'
import {
  displaySnapshot,
  instrumentStatus,
  legacyProtocolFields,
  reflection,
} from './displayFixtures'
import { findElements, sfcHarness } from './sfcHarness'

function matrixDisplay(portCount: 2 | 4) {
  const measurements = Array.from({ length: portCount ** 2 }, (_, index) => ({
    ...reflection,
    name: `BackendTrace${index + 1}`,
  }))
  return {
    measurements,
    display: {
      ...displaySnapshot(measurements),
      gridColumns: portCount,
      gridRows: portCount,
      layout: 'grid',
      displayedNames: measurements.map((trace) => trace.name),
      diagrams: measurements.map((trace, index) => ({
        number: index + 1,
        measurementNames: [trace.name],
        primaryMeasurement: trace.name,
        markerInfoVisible: true,
      })),
    },
  }
}

for (const count of [2, 4] as const) {
  test(`${count} 端口矩阵图窗按 C++ 返回尺寸显示，Maximized 不改保存的矩阵`, async () => {
    const { measurements, display } = matrixDisplay(count)
    const props = reactive({
      ...display,
      gridColumns: count,
      gridRows: count,
      frame: null,
      measurements,
      activeTraceName: display.selectedMeasurement,
      config: instrumentStatus().config,
      frameCondition: { kind: 'empty', label: '', detail: '' },
      canEditDisplay: true,
      commitMarker: async () => true,
    })
    const workspace = await sfcHarness(
      'src/components/diagrams/DiagramWorkspace.vue',
      props,
      {},
    )
    try {
      const section = () =>
        findElements(
          workspace.render(),
          (node) => node.props?.['aria-label'] === 'Measurement diagrams',
        )[0]!
      const before = JSON.stringify(props.diagrams)
      assert.equal(
        section().props!.style.gridTemplateColumns,
        `repeat(${count}, minmax(0, 1fr))`,
      )
      assert.equal(
        section().props!.style.gridTemplateRows,
        `repeat(${count}, minmax(0, 1fr))`,
      )
      props.layout = 'maximized'
      assert.equal(
        section().props!.style.gridTemplateColumns,
        'repeat(1, minmax(0, 1fr))',
      )
      props.layout = 'grid'
      assert.equal(
        section().props!.style.gridTemplateColumns,
        `repeat(${count}, minmax(0, 1fr))`,
      )
      assert.equal(JSON.stringify(props.diagrams), before)
      assert.deepEqual(workspace.emitted, [])
    } finally {
      workspace.dispose()
    }
  })
}

test('显示 DTO 严格保留后端网格，拒绝缺省、半组、零、小数及无法容纳图窗的尺寸', () => {
  const { measurements, display } = matrixDisplay(4)
  const decoded = decodeDisplay(display, measurements)
  assert.equal(Reflect.get(decoded, 'gridColumns'), 4)
  assert.equal(Reflect.get(decoded, 'gridRows'), 4)
  assert.deepEqual(legacyProtocolFields(decoded), display)
  for (const invalid of [
    { gridColumns: undefined },
    { gridRows: undefined },
    { gridColumns: 0 },
    { gridRows: 1.2 },
    { gridColumns: 129 },
    { gridColumns: 2, gridRows: 2 },
  ])
    assert.throws(() => decodeDisplay({ ...display, ...invalid }, measurements))
})
