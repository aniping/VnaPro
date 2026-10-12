import assert from 'node:assert/strict'
import test from 'node:test'
import { ref } from 'vue'
import type { DisplayCommand } from '../src/api/types'
import { useWorkspace } from '../src/composables/useWorkspace'
import {
  instrumentStatus,
  acquiredFrame,
  reflection,
  transmission,
} from './displayFixtures'
import { referenceLevel, usesRelativeAxis } from '../src/plot/traceMath'

function workspaceFixture() {
  const status = ref(instrumentStatus())
  const frame = ref(acquiredFrame())
  const commands: DisplayCommand[] = []
  const workspace = useWorkspace(
    () => status.value,
    () => frame.value,
    (command) => commands.push(command),
  )
  return { status, frame, commands, workspace }
}

test('all display actions remain requests until authoritative backend readback', () => {
  const { status, workspace, commands } = workspaceFixture()
  const before = JSON.parse(JSON.stringify(status.value.display))
  workspace.selectTrace(transmission.name)
  workspace.setDisplayed(transmission.name, false)
  workspace.setFormat('phase')
  workspace.setScale({
    perDivision: 4,
    referenceLevel: 0,
    referencePosition: 5,
  })
  workspace.setSmithRadius(2)
  workspace.setMarker({ enabled: true, frequencyHz: 15e6 })
  workspace.toggleVisibility()
  workspace.setLayout('grid')
  workspace.autoscale()
  assert.deepEqual(status.value.display, before)
  assert.deepEqual(
    commands.map((command) => command.action),
    [
      'select',
      'displayed',
      'format',
      'scale',
      'smithRadius',
      'marker',
      'visible',
      'layout',
      'autoscale',
    ],
  )
  assert.equal(workspace.activeTraceName.value, reflection.name)
  assert.equal(workspace.activePresentation.value!.format, 'db')
})

test('backend snapshots synchronize independent workspaces and a recreated observer', () => {
  const { status, frame, workspace } = workspaceFixture()
  const observer = useWorkspace(
    () => status.value,
    () => frame.value,
    () => assert.fail('Observer must not write'),
  )
  const updated = JSON.parse(JSON.stringify(status.value))
  updated.display.revision = '2'
  updated.display.selectedMeasurement = transmission.name
  updated.display.presentations[transmission.name]!.format = 'phase'
  updated.display.presentations[transmission.name]!.scale = {
    min: -180,
    max: 180,
    referencePosition: 5,
  }
  updated.display.presentations[transmission.name]!.marker.enabled = true
  updated.display.layout = 'grid'
  status.value = updated
  const refreshed = useWorkspace(
    () => status.value,
    () => frame.value,
    () => undefined,
  )
  for (const current of [workspace, observer, refreshed]) {
    assert.equal(current.activeTraceName.value, transmission.name)
    assert.equal(current.activePresentation.value!.format, 'phase')
    assert.equal(current.activePresentation.value!.marker.enabled, true)
    assert.equal(current.layout.value, 'grid')
    assert.deepEqual(
      current.activePresentation.value!.scale,
      updated.display.presentations[transmission.name]!.scale,
    )
  }
})

test('Autoscale submits the acquisition identity without deriving any scale from local samples', () => {
  const { status, frame, workspace, commands } = workspaceFixture()
  const scale = JSON.parse(
    JSON.stringify(workspace.activePresentation.value!.scale),
  )
  frame.value.traces[0]!.real = [1e300, -1e300]
  status.value.acquisitionRevision = '9007199254740993'
  workspace.autoscale()
  assert.deepEqual(commands, [
    {
      action: 'autoscale',
      traceName: reflection.name,
      acquisitionRevision: '9007199254740993',
    },
  ])
  assert.deepEqual(workspace.activePresentation.value!.scale, scale)
})

test('marker keyboard commands use original source steps instead of display array indexes', () => {
  const { status, workspace, commands } = workspaceFixture()
  status.value.acquisitionRevision = '4'
  workspace.setMarker({ step: 1, discrete: true })
  workspace.setMarker({ sampleIndex: 10000, discrete: true })
  assert.deepEqual(commands, [
    {
      action: 'marker',
      traceName: reflection.name,
      step: 1,
      discrete: true,
      acquisitionRevision: '4',
    },
    {
      action: 'marker',
      traceName: reflection.name,
      sampleIndex: 10000,
      discrete: true,
      acquisitionRevision: '4',
    },
  ])
})

test('marker collection commands delegate slot allocation and selection to C++ without local saved state changes', () => {
  const { status, workspace, commands } = workspaceFixture()
  const before = JSON.parse(JSON.stringify(status.value.display))
  workspace.addMarker()
  workspace.selectMarker(reflection.name, 10)
  workspace.setMarker({ markerId: 10, frequencyHz: 20e6, discrete: true })
  workspace.setMarker({ markerAction: 'allOff' })
  assert.deepEqual(commands, [
    {
      action: 'marker',
      traceName: reflection.name,
      markerAction: 'add',
      acquisitionRevision: '1',
    },
    {
      action: 'marker',
      traceName: reflection.name,
      markerAction: 'select',
      markerId: 10,
      acquisitionRevision: '1',
    },
    {
      action: 'marker',
      traceName: reflection.name,
      markerId: 10,
      frequencyHz: 20e6,
      discrete: true,
      acquisitionRevision: '1',
    },
    {
      action: 'marker',
      traceName: reflection.name,
      markerAction: 'allOff',
      acquisitionRevision: '1',
    },
  ])
  assert.deepEqual(status.value.display, before)
})

test('empty display and catalog changes are rendered exactly as the backend reports', () => {
  const { status, workspace } = workspaceFixture()
  status.value.display.displayedNames = []
  status.value.display.selectedMeasurement = ''
  assert.equal(workspace.displayedMeasurements.value.length, 0)
  assert.equal(workspace.activeTraceName.value, '')
  status.value.measurements = status.value.measurements.map((trace) => ({
    ...trace,
  }))
  assert.deepEqual(workspace.displayedNames.value, [])
  status.value.display.displayedNames = [transmission.name]
  status.value.display.selectedMeasurement = transmission.name
  assert.deepEqual(
    workspace.displayedMeasurements.value.map((trace) => trace.name),
    [transmission.name],
  )
})

test('softtool trace colors use the active backend diagram order instead of a global catalog index', () => {
  const { status, workspace } = workspaceFixture()
  status.value.display.selectedMeasurement = transmission.name
  status.value.display.diagrams = [
    {
      number: 1,
      measurementNames: [transmission.name],
      primaryMeasurement: transmission.name,
    },
  ]
  assert.equal(workspace.activeIndex.value, 0)
  status.value.display.diagrams[0]!.measurementNames = [
    reflection.name,
    transmission.name,
  ]
  assert.equal(workspace.activeIndex.value, 1)
})

test('relative-axis labels depend only on the authoritative fixed scale', () => {
  assert.equal(usesRelativeAxis({ min: 0.9064, max: 0.90645 }), true)
  assert.equal(usesRelativeAxis({ min: -14.01, max: -13.91 }), false)
  assert.equal(referenceLevel({ min: -80, max: 20, referencePosition: 9 }), 10)
})
