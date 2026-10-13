import assert from 'node:assert/strict'
import test from 'node:test'
import { reactive, type VNode } from 'vue'
import { transmission } from './displayFixtures'
import { findElements, sfcHarness } from './sfcHarness'

function textContent(value: unknown): string {
  if (typeof value === 'string') return value
  if (Array.isArray(value)) return value.map(textContent).join('')
  if (value && typeof value === 'object')
    return textContent(Reflect.get(value, 'children'))
  return ''
}

function item(name: string, markerId: number, color: string) {
  return {
    name,
    markerId,
    color,
    format: 'db' as const,
    measurement: { ...transmission, name },
    result: {
      index: 5,
      frequencyHz: 2.5e9,
      valid: true,
      value: -28.1234,
      secondaryValue: 0,
      real: 0.02,
      imaginary: 0.03,
      impedanceReal: null,
      impedanceImag: null,
      impedanceState: 'invalid' as const,
      interpolated: false,
      reason: '',
    },
    active: false,
  }
}

function hasClass(node: VNode, name: string): boolean {
  return String(node.props?.class).split(' ').includes(name)
}

test('multi-trace marker readouts keep local marker numbers and make trace ownership and color visible', async () => {
  const markers = [
    item('Trc17', 1, '#ffff26'),
    item('Trc17', 10, '#ffff26'),
    item('Trc1', 1, '#60e600'),
  ]
  markers[1]!.active = true
  const props = reactive({ markers, canEditDisplay: true })
  const readout = await sfcHarness(
    'src/components/diagrams/MarkerReadout.vue',
    props,
    {},
  )
  try {
    const root = readout.render()
    const rows = findElements(root, (node) => hasClass(node, 'marker-row'))
    assert.equal(rows.length, 3)
    for (let index = 0; index < rows.length; index++) {
      const row = rows[index]!
      const marker = markers[index]!
      assert.equal(row.props!.style.color, marker.color)
      assert.ok(textContent(row).includes(`M${marker.markerId}`))
      assert.ok(
        textContent(row).includes(marker.name),
        `The repeated M1 must visibly identify its owner ${marker.name}`,
      )
      row.props!.onClick({ stopPropagation() {} })
    }
    assert.deepEqual(JSON.parse(JSON.stringify(readout.emitted)), [
      ['select', { name: 'Trc17', markerId: 1 }],
      ['select', { name: 'Trc17', markerId: 10 }],
      ['select', { name: 'Trc1', markerId: 1 }],
    ])
    assert.equal(rows[1]!.props!['aria-current'], 'true')
    assert.deepEqual(
      props.markers.map((marker) => [marker.name, marker.markerId]),
      [
        ['Trc17', 1],
        ['Trc17', 10],
        ['Trc1', 1],
      ],
      'Readouts must neither globally renumber nor reorder backend markers',
    )
  } finally {
    readout.dispose()
  }
})

test('a single-trace marker field stays compact and readonly rows retain the trace color', async () => {
  const props = reactive({
    markers: [item('Trc17', 1, '#ffff26')],
    canEditDisplay: false,
  })
  const readout = await sfcHarness(
    'src/components/diagrams/MarkerReadout.vue',
    props,
    {},
  )
  try {
    const root = readout.render()
    const row = findElements(root, (node) => hasClass(node, 'marker-row'))[0]!
    assert.equal(row.props!.disabled, true)
    assert.equal(row.props!.style.color, '#ffff26')
    assert.equal(
      findElements(root, (node) => hasClass(node, 'marker-trace')).length,
      0,
    )
    assert.match(textContent(row), /M1/)
    assert.equal(props.markers[0]!.markerId, 1)
  } finally {
    readout.dispose()
  }
})
