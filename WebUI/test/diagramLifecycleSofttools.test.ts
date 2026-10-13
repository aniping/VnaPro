import assert from 'node:assert/strict'
import test from 'node:test'
import { reactive, type VNode } from 'vue'
import type { InstrumentStatus, TraceDestination } from '../src/api/types'
import { MAX_DIAGRAMS, MAX_TRACES } from '../src/domain/presentation'
import { instrumentStatus } from './displayFixtures'
import { findElements, sfcHarness } from './sfcHarness'

function twoDiagrams(): InstrumentStatus {
  const status = instrumentStatus()
  status.display.diagrams = status.measurements.map((trace, index) => ({
    number: index + 1,
    measurementNames: [trace.name],
    primaryMeasurement: trace.name,
  }))
  return status
}

function control(render: () => VNode, id: string) {
  const node = findElements(
    render(),
    (node) => node.props?.['data-testid'] === id,
  )[0]
  assert.ok(node, `Missing control ${id}`)
  return node.props!
}

function traceProps(status = twoDiagrams()) {
  return reactive({
    status,
    activeTraceName: status.display.selectedMeasurement,
    displayedNames: status.display.displayedNames,
    canChangeMeasurements: true,
    canEditDisplay: true,
    busy: false,
    clone: async (_source: string, _destination?: TraceDestination) => null,
    remove: async (_name: string, _revision: string) => null,
  })
}

test('Display diagram controls request native add, select and delete without changing the authoritative snapshot', async () => {
  const props = reactive({
    status: twoDiagrams(),
    canEditDisplay: true,
    canChangeMeasurements: true,
    busy: false,
  })
  const view = await sfcHarness(
    'src/components/softtools/DisplaySofttool.vue',
    props,
    {},
  )
  try {
    const before = JSON.stringify(props.status)
    control(view.render, 'display-add-diagram').onClick()
    const input = { value: '2' }
    control(view.render, 'active-diagram').onChange({ target: input })
    control(view.render, 'display-delete-diagram').onClick()
    assert.deepEqual(view.emitted, [
      ['addTrace'],
      ['selectDiagram', 2],
      ['deleteDiagram', 1],
    ])
    assert.equal(
      input.value,
      '1',
      'Pending selection stays at the device readback',
    )
    assert.equal(JSON.stringify(props.status), before)
    props.status.display.selectedDiagram = 2
    assert.equal(control(view.render, 'active-diagram').value, 2)
    control(view.render, 'display-delete-diagram').onClick()
    assert.deepEqual(view.emitted.at(-1), ['deleteDiagram', 2])
  } finally {
    view.dispose()
  }
})

test('Display diagram actions reject read-only, pending and exhausted capacity states', async () => {
  const props = reactive({
    status: twoDiagrams(),
    canEditDisplay: true,
    canChangeMeasurements: true,
    busy: false,
  })
  const view = await sfcHarness(
    'src/components/softtools/DisplaySofttool.vue',
    props,
    {},
  )
  try {
    for (const mode of ['readOnly', 'busy'] as const) {
      props.canEditDisplay = mode !== 'readOnly'
      props.canChangeMeasurements = mode !== 'readOnly'
      props.busy = mode === 'busy'
      for (const id of ['display-add-diagram', 'display-delete-diagram']) {
        const button = control(view.render, id)
        assert.equal(button.disabled, true)
        button.onClick()
      }
      const select = control(view.render, 'active-diagram')
      assert.equal(select.disabled, true)
      select.onChange({ target: { value: '2' } })
    }
    assert.equal(view.emitted.length, 0)
    props.canEditDisplay = true
    props.canChangeMeasurements = true
    props.busy = false
    props.status.display.diagrams = Array.from(
      { length: MAX_DIAGRAMS },
      (_, index) => ({
        number: index + 1,
        measurementNames: [],
        primaryMeasurement: '',
      }),
    )
    const full = control(view.render, 'display-add-diagram')
    assert.equal(full.disabled, true)
    full.onClick()
    assert.equal(view.emitted.length, 0)
    props.status.display.diagrams = [props.status.display.diagrams[0]!]
    const last = control(view.render, 'display-delete-diagram')
    assert.equal(last.disabled, true)
    last.onClick()
    props.status.measurements = Array.from(
      { length: MAX_TRACES },
      () => props.status.measurements[0]!,
    )
    assert.equal(control(view.render, 'display-add-diagram').disabled, true)
    control(view.render, 'display-add-diagram').onClick()
    assert.equal(view.emitted.length, 0)
  } finally {
    view.dispose()
  }
})

test('Trace Config copies the active trace into the requested new diagram and moves the original by backend selection', async () => {
  const props = traceProps()
  const calls: { source: string; destination?: TraceDestination }[] = []
  props.clone = async (source, destination) => {
    calls.push({ source, destination: destination && { ...destination } })
    return null
  }
  const view = await sfcHarness(
    'src/components/softtools/TraceCatalogSofttool.vue',
    props,
    {},
  )
  try {
    const before = JSON.stringify(props.status)
    await control(view.render, 'trace-add').onClick()
    await control(view.render, 'trace-add-diagram').onClick()
    assert.deepEqual(calls, [
      { source: 'Reflect', destination: undefined },
      { source: 'Reflect', destination: { newDiagram: true } },
    ])
    const input = { value: '2' }
    control(view.render, 'trace-diagram').onChange({ target: input })
    assert.deepEqual(view.emitted, [['selectTrace', 'Reflect', 2]])
    assert.equal(input.value, '1')
    assert.equal(JSON.stringify(props.status), before)
    props.status.display.diagrams[0]!.measurementNames = []
    props.status.display.diagrams[0]!.primaryMeasurement = ''
    props.status.display.diagrams[1]!.measurementNames.push('Reflect')
    assert.equal(control(view.render, 'trace-diagram').value, 2)
  } finally {
    view.dispose()
  }
})

test('Trace Config rejects read-only or busy movement and does not clone beyond either project budget', async () => {
  const props = traceProps()
  let copies = 0
  props.clone = async () => {
    copies += 1
    return null
  }
  const view = await sfcHarness(
    'src/components/softtools/TraceCatalogSofttool.vue',
    props,
    {},
  )
  try {
    for (const mode of ['readOnly', 'busy'] as const) {
      props.canEditDisplay = mode !== 'readOnly'
      props.canChangeMeasurements = mode !== 'readOnly'
      props.busy = mode === 'busy'
      for (const id of ['trace-add', 'trace-add-diagram']) {
        const button = control(view.render, id)
        assert.equal(button.disabled, true)
        await button.onClick()
      }
      const select = control(view.render, 'trace-diagram')
      assert.equal(select.disabled, true)
      select.onChange({ target: { value: '2' } })
    }
    props.canEditDisplay = true
    props.canChangeMeasurements = true
    props.busy = false
    props.status.display.diagrams = Array.from(
      { length: MAX_DIAGRAMS },
      (_, index) => ({
        number: index + 1,
        measurementNames: index === 0 ? ['Reflect'] : [],
        primaryMeasurement: index === 0 ? 'Reflect' : '',
      }),
    )
    const full = control(view.render, 'trace-add-diagram')
    assert.equal(full.disabled, true)
    await full.onClick()
    assert.equal(control(view.render, 'trace-add').disabled, false)
    props.status.measurements = Array.from(
      { length: MAX_TRACES },
      () => props.status.measurements[0]!,
    )
    for (const id of ['trace-add', 'trace-add-diagram']) {
      const button = control(view.render, id)
      assert.equal(button.disabled, true)
      await button.onClick()
    }
    assert.equal(copies, 0)
    assert.equal(view.emitted.length, 0)
  } finally {
    view.dispose()
  }
})

test('Trace Config keeps clone actions and ownership disabled while the accepted request awaits readback', async () => {
  const props = traceProps()
  let finish: (value: null) => void = () => {}
  props.clone = () =>
    new Promise<null>((resolve) => {
      finish = resolve
    })
  const view = await sfcHarness(
    'src/components/softtools/TraceCatalogSofttool.vue',
    props,
    {},
  )
  try {
    const copying = control(view.render, 'trace-add-diagram').onClick()
    for (const id of ['trace-add', 'trace-add-diagram', 'trace-diagram'])
      assert.equal(control(view.render, id).disabled, true)
    control(view.render, 'trace-diagram').onChange({ target: { value: '2' } })
    assert.equal(view.emitted.length, 0)
    finish(null)
    await copying
    assert.equal(control(view.render, 'trace-add-diagram').disabled, false)
  } finally {
    finish(null)
    view.dispose()
  }
})
