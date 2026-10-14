import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive } from 'vue'
import {
  instrumentStatus,
  markerDefinitions,
  reflection,
} from './displayFixtures'
import { findElements, sfcHarness } from './sfcHarness'
import { enableMarkerOnEntry } from '../src/composables/markerMenuEntry'

function menuProps() {
  const presentation = instrumentStatus().display.presentations.Reflect!
  presentation.markers = markerDefinitions()
  presentation.activeMarkerId = 2
  presentation.markers[0] = {
    id: 1,
    enabled: true,
    frequencyHz: 10e6,
    sampleIndex: 0,
    discrete: true,
  }
  presentation.markers[1] = {
    id: 2,
    enabled: true,
    frequencyHz: 20e6,
    sampleIndex: 1,
    discrete: false,
  }
  const { id: _id, ...state } = presentation.markers[1]!
  presentation.marker = state
  return reactive({
    measurement: reflection,
    presentation,
    frame: null,
    index: 0,
    canEditDisplay: true,
  })
}

function isDisabled(value: unknown): boolean {
  return value !== undefined && value !== false
}

test('Marker menu entry enables backend M1 when all saved markers are off without changing the local selection', () => {
  const props = menuProps()
  props.presentation.markers!.forEach((marker) => {
    marker.enabled = false
  })
  props.presentation.marker.enabled = false
  const before = JSON.parse(JSON.stringify(props.presentation))
  const selected: number[] = []
  const completion = Promise.resolve(true)
  assert.equal(
    enableMarkerOnEntry(props.presentation, true, (id) => {
      selected.push(id)
      return completion
    }),
    completion,
  )
  assert.deepEqual(selected, [1])
  assert.deepEqual(props.presentation, before)
})

test('Marker menu entry preserves any enabled marker and readonly observers never issue a write', () => {
  const props = menuProps()
  const unexpected = () =>
    assert.fail(
      'Opening the menu must not toggle an enabled marker or write as an observer',
    )
  enableMarkerOnEntry(props.presentation, true, unexpected)
  props.presentation.markers!.forEach((marker) => {
    marker.enabled = false
  })
  props.presentation.marker.enabled = false
  enableMarkerOnEntry(props.presentation, false, unexpected)
  enableMarkerOnEntry(undefined, true, unexpected)
})

test('Marker menu entry preserves the old M1 behavior when a legacy response has no collection', () => {
  const props = menuProps()
  delete props.presentation.markers
  delete props.presentation.activeMarkerId
  props.presentation.marker.enabled = false
  const selected: number[] = []
  enableMarkerOnEntry(props.presentation, true, (id) => selected.push(id))
  assert.deepEqual(selected, [1])
  props.presentation.marker.enabled = true
  enableMarkerOnEntry(props.presentation, true, () =>
    assert.fail('Existing M1 must stay on'),
  )
})

test('real Marker menu renders ten usable number choices, commands select M10 without changing M2 before readback', async () => {
  const props = menuProps()
  const menu = await sfcHarness(
    'src/components/softtools/MarkerSofttool.vue',
    props,
    {},
  )
  try {
    const choices = findElements(
      menu.render(),
      (node) =>
        node.type === 'button' && node.props?.['data-marker-id'] !== undefined,
    )
    assert.equal(choices.length, 10)
    assert.deepEqual(
      choices.map((choice) => choice.props!['data-marker-id']),
      [1, 2, 3, 4, 5, 6, 7, 8, 9, 10],
    )
    const state = findElements(
      menu.render(),
      (node) => node.props?.['data-testid'] === 'marker-enabled',
    )[0]!
    assert.ok(
      findElements(
        state,
        (node) =>
          typeof node.children === 'string' &&
          node.children.trim() === 'Marker',
      ).length,
    )
    assert.equal(
      findElements(state, (node) => node.type === 'strong')[0]!.children,
      'On',
    )
    assert.ok(choices.every((choice) => !isDisabled(choice.props!.disabled)))
    assert.equal(choices[1]!.props!['aria-pressed'], true)
    choices[9]!.props!.onClick()
    assert.deepEqual(JSON.parse(JSON.stringify(menu.emitted)), [
      ['marker', { markerId: 10, markerAction: 'select' }],
    ])
    assert.equal(props.presentation.activeMarkerId, 2)
    assert.equal(props.presentation.markers![9]!.enabled, false)
  } finally {
    menu.dispose()
  }
})

test('real Add Marker asks the backend to choose a slot and All Off retains all frontend saved positions', async () => {
  const props = menuProps()
  const menu = await sfcHarness(
    'src/components/softtools/MarkerSofttool.vue',
    props,
    {},
  )
  try {
    const before = JSON.parse(JSON.stringify(props.presentation))
    for (const id of ['add-marker', 'markers-all-off']) {
      findElements(
        menu.render(),
        (node) => node.props?.['data-testid'] === id,
      )[0]!.props!.onClick()
    }
    assert.deepEqual(JSON.parse(JSON.stringify(menu.emitted)), [
      ['marker', { markerAction: 'add' }],
      ['marker', { markerAction: 'allOff' }],
    ])
    assert.deepEqual(props.presentation, before)
    props.presentation.markers!.forEach((marker) => {
      marker.enabled = true
    })
    await nextTick()
    const add = findElements(
      menu.render(),
      (node) => node.props?.['data-testid'] === 'add-marker',
    )[0]!
    assert.equal(isDisabled(add.props!.disabled), true)
  } finally {
    menu.dispose()
  }
})

test('real Marker menu identifies the active trace scope when selection changes', async () => {
  const props = menuProps()
  const menu = await sfcHarness(
    'src/components/softtools/MarkerSofttool.vue',
    props,
    {},
  )
  try {
    for (const name of ['Reflect', 'Second Trace']) {
      props.measurement = { ...reflection, name }
      await nextTick()
      const scope = findElements(
        menu.render(),
        (node) => node.props?.['data-testid'] === 'marker-scope',
      )[0]!
      assert.equal(
        findElements(scope, (node) => node.type === 'span')[0]!.children,
        'Active Trace · Ch1',
      )
      assert.equal(
        findElements(scope, (node) => node.type === 'strong')[0]!.children,
        name,
      )
      const choices = findElements(
        menu.render(),
        (node) => node.props?.['data-marker-id'] !== undefined,
      )
      assert.deepEqual(
        choices.map((choice) => choice.props!.title),
        Array.from({ length: 10 }, (_, index) => `${name} · M${index + 1}`),
      )
      const allOff = findElements(
        menu.render(),
        (node) => node.props?.['data-testid'] === 'markers-all-off',
      )[0]!
      assert.equal(allOff.props!.title, `Turn off all markers on ${name}`)
    }
    assert.deepEqual(JSON.parse(JSON.stringify(menu.emitted)), [])
  } finally {
    menu.dispose()
  }
})

test('readonly marker menu disables all writes, including direct stale event handlers', async () => {
  const props = menuProps()
  props.canEditDisplay = false
  const menu = await sfcHarness(
    'src/components/softtools/MarkerSofttool.vue',
    props,
    {},
  )
  try {
    const buttons = findElements(
      menu.render(),
      (node) => node.type === 'button',
    )
    assert.ok(buttons.every((button) => isDisabled(button.props!.disabled)))
    findElements(
      menu.render(),
      (node) => node.props?.['data-marker-id'] === 10,
    )[0]!.props!.onClick()
    assert.deepEqual(JSON.parse(JSON.stringify(menu.emitted)), [])
  } finally {
    menu.dispose()
  }
})

test('real trace catalog enables the fifth display and clones the backend-selected trace without assigning a local name', async () => {
  const catalog = Array.from({ length: 5 }, (_, index) => ({
    ...reflection,
    name: `Trc${index + 1}`,
  }))
  const status = instrumentStatus(catalog)
  const calls: string[] = []
  const props = {
    status,
    activeTraceName: 'Trc5',
    displayedNames: catalog.slice(0, 4).map((trace) => trace.name),
    canChangeMeasurements: true,
    busy: false,
    canEditDisplay: true,
    clone: async (name: string) => {
      calls.push(name)
      return null
    },
    remove: async () => null,
  }
  const menu = await sfcHarness(
    'src/components/softtools/TraceCatalogSofttool.vue',
    props,
    {},
  )
  try {
    const fifth = findElements(
      menu.render(),
      (node) =>
        node.type === 'input' && node.props?.['aria-label'] === 'Display Trc5',
    )[0]!
    assert.equal(
      isDisabled(fifth.props!.disabled),
      false,
      'The old four-trace frontend cap must be gone',
    )
    fifth.props!.onChange()
    assert.deepEqual(JSON.parse(JSON.stringify(menu.emitted[0])), [
      'display',
      { name: 'Trc5', enabled: true },
    ])
    const add = findElements(
      menu.render(),
      (node) =>
        node.type === 'button' && String(node.children).trim() === 'Add Trace',
    )[0]!
    await add.props!.onClick()
    assert.deepEqual(calls, ['Trc5'])
    assert.equal(status.measurements.length, 5)
  } finally {
    menu.dispose()
  }
})
