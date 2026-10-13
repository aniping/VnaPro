import assert from 'node:assert/strict'
import test from 'node:test'
import { reactive } from 'vue'
import { decodeDisplay } from '../src/api/displayDecoders'
import {
  displaySnapshot,
  instrumentStatus,
  markerDefinitions,
  reflection,
  transmission,
} from './displayFixtures'
import { findElements, sfcHarness } from './sfcHarness'

function textContent(value: unknown): string {
  if (typeof value === 'string') return value
  if (Array.isArray(value)) return value.map(textContent).join('')
  if (value && typeof value === 'object')
    return textContent(Reflect.get(value, 'children'))
  return ''
}

test('native diagram menu provides independent arrangement and single-transaction trace/diagram actions', async () => {
  const props = reactive({
    number: 3,
    active: false,
    layout: 'single',
    hasTrace: true,
    canEditDisplay: true,
    canAddTrace: true,
    canAddDiagram: true,
    canDeleteDiagram: true,
  })
  const menu = await sfcHarness(
    'src/components/diagrams/DiagramMenu.vue',
    props,
    {},
  )
  try {
    const open = menu.bindings.open as { value: boolean }
    const commands = [
      'Add Trace',
      'Add Trace + Diagram',
      'Delete Diagram',
      'Overlay All',
      'Split All',
    ]
    for (const label of commands) {
      open.value = true
      const button = findElements(
        menu.render(),
        (node) =>
          node.type === 'button' &&
          textContent(node)
            .replace(/^[+×✓]/, '')
            .trim() === label,
      )[0]!
      assert.ok(button, `Missing native action ${label}`)
      button.props!.onClick({ stopPropagation() {} })
    }
    assert.deepEqual(JSON.parse(JSON.stringify(menu.emitted)), [
      ['addTrace', false],
      ['addTrace', true],
      ['deleteDiagram'],
      ['arrange', 'overlay'],
      ['arrange', 'split'],
    ])
    assert.equal(props.number, 3)
    assert.equal(props.layout, 'single')
    props.canDeleteDiagram = false
    open.value = true
    const last = findElements(
      menu.render(),
      (node) =>
        node.type === 'button' && textContent(node).includes('Delete Diagram'),
    )[0]!
    assert.equal(last.props!.disabled, true)
    last.props!.onClick({ stopPropagation() {} })
    assert.equal(menu.emitted.length, 5)
    props.canAddDiagram = false
    const newDiagram = findElements(
      menu.render(),
      (node) =>
        node.type === 'button' &&
        textContent(node).includes('Add Trace + Diagram'),
    )[0]!
    assert.equal(newDiagram.props!.disabled, true)
    newDiagram.props!.onClick({ stopPropagation() {} })
    assert.equal(menu.emitted.length, 5)
    const addInExisting = findElements(
      menu.render(),
      (node) =>
        node.type === 'button' &&
        textContent(node)
          .replace(/^[+×✓]/, '')
          .trim() === 'Add Trace',
    )[0]!
    assert.equal(addInExisting.props!.disabled, false)
  } finally {
    menu.dispose()
  }
})

test('all explicit diagrams render without a four-window limit and maximization preserves the complete backend catalog', async () => {
  const catalog = Array.from({ length: 6 }, (_, index) => ({
    ...reflection,
    name: `Trc${index + 1}`,
  }))
  const display = displaySnapshot(catalog)
  display.displayedNames = catalog.map((trace) => trace.name)
  display.selectedDiagram = 5
  display.diagrams = catalog.map((trace, index) => ({
    number: index + 1,
    measurementNames: [trace.name],
    primaryMeasurement: trace.name,
  }))
  assert.equal(decodeDisplay(display, catalog).diagrams.length, 6)
  const props = reactive({
    diagrams: display.diagrams,
    selectedDiagram: 5,
    frame: null,
    measurements: catalog,
    presentations: display.presentations,
    activeTraceName: 'Trc5',
    layout: 'grid',
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
    const panes = () =>
      findElements(
        workspace.render(),
        (node) =>
          typeof node.type === 'object' &&
          Reflect.get(node.type, 'name') === 'DiagramPane',
      )
    assert.equal(panes().length, 6)
    props.layout = 'maximized'
    assert.equal(panes().length, 1)
    assert.equal(panes()[0]!.props!.number, 5)
    assert.equal(props.diagrams.length, 6)
    props.layout = 'single'
    assert.equal(panes().length, 6)
  } finally {
    workspace.dispose()
  }
})

test('the format menu presents the thirteen native formats and permits Smith for transmission measurements', async () => {
  const view = instrumentStatus().display.presentations.Reflect!
  const menu = await sfcHarness(
    'src/components/softtools/TraceSofttool.vue',
    {
      mode: 'format',
      measurement: transmission,
      presentation: view,
      frame: null,
      index: 0,
      layout: 'single',
      canAutoscale: false,
      canEditDisplay: true,
    },
    {},
  )
  try {
    const buttons = findElements(
      menu.render(),
      (node) => node.props?.['data-format'],
    )
    assert.equal(buttons.length, 13)
    assert.ok(buttons.every((button) => button.props!.disabled === false))
    assert.ok(
      !buttons.some((button) => button.props!['data-format'] === 'realimag'),
    )
    const smith = buttons.find(
      (button) => button.props!['data-format'] === 'smith',
    )!
    smith.props!.onClick()
    assert.deepEqual(JSON.parse(JSON.stringify(menu.emitted)), [
      ['format', 'smith'],
    ])
  } finally {
    menu.dispose()
  }
})

test('Remove Marker delegates disabling only the selected slot and preserves all saved positions until readback', async () => {
  const view = instrumentStatus().display.presentations.Reflect!
  view.markers = markerDefinitions()
  view.activeMarkerId = 3
  view.markers[2] = {
    id: 3,
    enabled: true,
    frequencyHz: 123e6,
    sampleIndex: 7,
    discrete: true,
  }
  const { id: _id, ...alias } = view.markers[2]!
  view.marker = alias
  const before = structuredClone(view)
  const menu = await sfcHarness(
    'src/components/softtools/MarkerSofttool.vue',
    {
      measurement: reflection,
      presentation: view,
      frame: null,
      index: 0,
      canEditDisplay: true,
    },
    {},
  )
  try {
    findElements(
      menu.render(),
      (node) => node.props?.['data-testid'] === 'remove-marker',
    )[0]!.props!.onClick()
    assert.deepEqual(JSON.parse(JSON.stringify(menu.emitted)), [
      ['marker', { markerId: 3, enabled: false }],
    ])
    assert.deepEqual(view, before)
  } finally {
    menu.dispose()
  }
})
