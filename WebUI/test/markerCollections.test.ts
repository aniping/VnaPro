import assert from 'node:assert/strict'
import test from 'node:test'
import { decodeDisplay, decodeProjection } from '../src/api/displayDecoders'
import { decodeFrame } from '../src/api/decoders'
import { activeMarker, traceMarkers } from '../src/domain/markers'
import { diagramTraceColor, MAX_TRACES } from '../src/domain/presentation'
import {
  acquiredFrame,
  displaySnapshot,
  markerDefinitions,
  markerProjection,
  markerProjections,
  projection,
  receiverTrace,
  reflection,
} from './displayFixtures'

function display() {
  const value = displaySnapshot([reflection])
  const view = value.presentations.Reflect!
  view.activeMarkerId = 2
  view.markers = markerDefinitions()
  view.markers[0] = {
    id: 1,
    enabled: true,
    frequencyHz: 10e6,
    sampleIndex: 0,
    discrete: true,
  }
  view.markers[1] = {
    id: 2,
    enabled: true,
    frequencyHz: 20e6,
    sampleIndex: 1,
    discrete: false,
  }
  const { id: _id, ...state } = view.markers[1]!
  view.marker = state
  return value
}

function multiProjection() {
  const value = projection([-12, -6])
  value.activeMarkerId = 2
  value.markers = markerProjections()
  value.markers[1] = {
    id: 2,
    ...markerProjection({
      available: true,
      valid: true,
      frequencyHz: 20e6,
      sampleIndex: 1,
      real: 0.5,
      imag: 0,
      value: -6,
      valueState: 'finite',
      reason: '',
    }),
  }
  const { id: _id, ...alias } = value.markers[1]!
  value.marker = alias
  return value
}

test('new display DTO keeps all ten independent saved states and the backend active M2 alias', () => {
  const value = display()
  const decoded = decodeDisplay(value, [reflection])
  assert.deepEqual(decoded, value)
  assert.equal(activeMarker(decoded.presentations.Reflect)!.id, 2)
  assert.equal(
    traceMarkers(decoded.presentations.Reflect).filter(
      (marker) => marker.enabled,
    ).length,
    2,
  )
})

test('new marker projection collection preserves each independent backend result', () => {
  const value = multiProjection()
  assert.deepEqual(decodeProjection(value, 2), value)
  assert.equal(decodeProjection(value, 2).markers![1]!.value, -6)
})

test('old protocol collections wholly absent normalize to M1 without enabling M2 through M10', () => {
  const old = displaySnapshot([reflection])
  old.presentations.Reflect!.marker.enabled = true
  const decoded = decodeDisplay(old, [reflection]).presentations.Reflect!
  assert.equal(decoded.activeMarkerId, 1)
  assert.equal(decoded.markers!.length, 10)
  assert.equal(decoded.markers![0]!.enabled, true)
  assert.ok(decoded.markers!.slice(1).every((marker) => !marker.enabled))
  const result = decodeProjection(projection([-12, -6]), 2)
  assert.equal(result.activeMarkerId, 1)
  assert.equal(result.markers!.length, 10)
  assert.ok(result.markers!.every((marker) => !marker.available))
})

for (const defect of [
  'missingCollection',
  'missingActiveId',
  'short',
  'duplicateId',
  'outOfRangeId',
  'aliasMismatch',
  'badActiveId',
  'badDiscrete',
] as const) {
  test(`marker state collection rejects ${defect} instead of patching partial new data`, () => {
    const value = display()
    const view = value.presentations.Reflect!
    switch (defect) {
      case 'missingCollection':
        delete view.markers
        break
      case 'missingActiveId':
        delete view.activeMarkerId
        break
      case 'short':
        view.markers!.pop()
        break
      case 'duplicateId':
        view.markers![9]!.id = 1
        break
      case 'outOfRangeId':
        view.markers![9]!.id = 11
        break
      case 'aliasMismatch':
        view.marker.frequencyHz = 21e6
        break
      case 'badActiveId':
        view.activeMarkerId = 0
        break
      case 'badDiscrete':
        Reflect.set(view.markers![2]!, 'discrete', 'true')
        break
    }
    assert.throws(() => decodeDisplay(value, [reflection]))
  })
}

for (const defect of [
  'missingCollection',
  'missingActiveId',
  'short',
  'duplicateId',
  'badActiveId',
  'aliasMismatch',
] as const) {
  test(`marker projection collection rejects ${defect}`, () => {
    const value = multiProjection()
    switch (defect) {
      case 'missingCollection':
        delete value.markers
        break
      case 'missingActiveId':
        delete value.activeMarkerId
        break
      case 'short':
        value.markers!.pop()
        break
      case 'duplicateId':
        value.markers![9]!.id = 1
        break
      case 'badActiveId':
        value.activeMarkerId = 11
        break
      case 'aliasMismatch':
        value.marker.value = -7
        break
    }
    assert.throws(() => decodeProjection(value, 2))
  })
}

for (const count of [32, MAX_TRACES]) {
  test(`all ${count} catalog traces can be displayed, grouped and decoded without a four-trace frame cap`, () => {
    const catalog = Array.from({ length: count }, (_, index) => ({
      ...reflection,
      name: `Trc${index + 1}`,
    }))
    const display = displaySnapshot(catalog)
    display.displayedNames = catalog.map((trace) => trace.name)
    display.diagrams[0]!.measurementNames = [...display.displayedNames]
    assert.equal(decodeDisplay(display, catalog).displayedNames.length, count)
    const frame = acquiredFrame()
    frame.traces = catalog.map((measurement) => receiverTrace(measurement))
    assert.equal((decodeFrame(frame) as typeof frame).traces.length, count)
    if (count === MAX_TRACES) {
      frame.traces.push(
        receiverTrace({ ...reflection, name: `Trc${MAX_TRACES + 1}` }),
      )
      assert.throws(() => decodeFrame(frame))
    }
  })
}

test('each diagram uses its own trace order for colors, with mixed overlays retaining distinct colors', () => {
  assert.notEqual(diagramTraceColor(0, 'grid'), diagramTraceColor(1, 'grid'))
  assert.equal(diagramTraceColor(0, 'grid'), diagramTraceColor(0, 'single'))
  assert.notEqual(
    diagramTraceColor(2, 'maximized'),
    diagramTraceColor(3, 'maximized'),
  )
})
