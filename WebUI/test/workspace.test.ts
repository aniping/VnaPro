import assert from 'node:assert/strict'
import test from 'node:test'
import { effectScope, nextTick, ref } from 'vue'
import type { AcquiredFrame, Measurement } from '../src/api/types'
import { useWorkspace } from '../src/composables/useWorkspace'
import { getTraceScale } from '../src/plot'
import { displayValue, referenceLevel, usesRelativeAxis } from '../src/plot/traceMath'
import { FORMAT_INFO } from '../src/plot/formatting'

const reflect: Measurement = {
  name: 'Reflect', kind: 'sParameter', receiverKind: 'b', receiverPort: 1, sourcePort: 1,
  parameter: 'S11', unit: '1', calibration: 'uncalibrated',
}
const transmit: Measurement = { ...reflect, name: 'Transmit', receiverPort: 2, parameter: 'S21' }

function acquired(real: number[], imaginary = real.map(() => 0)): AcquiredFrame {
  return {
    available: true, current: true, channelId: 1, runId: '1', revision: '1', measurementRevision: '1', frameSeq: '1',
    originalPointCount: real.length, displayPointCount: real.length, decimated: false,
    frequencyHz: real.map((_, index) => (index + 1) * 1e6),
    traces: [
      { ...reflect, available: true, real, imag: imaginary, valid: real.map(() => true) },
      { ...transmit, available: true, real: real.map(() => 0.05), imag: real.map(() => 0), valid: real.map(() => true) },
    ],
  }
}

test('Autoscale fixes the range, division and reference while continuous frames change', async context => {
  const scope = effectScope()
  context.after(() => scope.stop())
  const currentFrame = ref(acquired([0.2, 0.2001, 0.1999]))
  const catalog = ref([reflect, transmit])
  const workspace = scope.run(() => useWorkspace(() => catalog.value, () => currentFrame.value))!
  workspace.autoscale()
  const fitted = getTraceScale(currentFrame.value, reflect, workspace.activePresentation.value)
  currentFrame.value = acquired([0.1, 0.3, 0.2])
  catalog.value = [{ ...reflect }, { ...transmit }]
  await nextTick()
  const later = getTraceScale(currentFrame.value, reflect, workspace.activePresentation.value)
  assert.deepEqual(later, fitted)
  assert.equal((later.max - later.min) / 10, (fitted.max - fitted.min) / 10)
  assert.equal(referenceLevel(later), referenceLevel(fitted))
})

test('another Autoscale click refits only the active trace using the latest displayed frame', context => {
  const scope = effectScope()
  context.after(() => scope.stop())
  const currentFrame = ref(acquired([0.2, 0.2001, 0.1999]))
  const workspace = scope.run(() => useWorkspace(() => [reflect, transmit], () => currentFrame.value))!
  const otherScale = { ...workspace.presentations[transmit.name]!.scale }
  workspace.autoscale()
  const firstScale = { ...workspace.activePresentation.value!.scale }
  currentFrame.value = acquired([0.1, 0.3, 0.2])
  workspace.autoscale()
  const refitted = workspace.activePresentation.value!.scale
  assert.notDeepEqual(refitted, firstScale)
  assert.ok(refitted.min <= displayValue(0.1, 0, 'db'))
  assert.ok(refitted.max >= displayValue(0.3, 0, 'db'))
  assert.deepEqual(workspace.presentations[transmit.name]!.scale, otherScale)
  workspace.selectTrace(transmit.name)
  workspace.autoscale()
  assert.deepEqual(workspace.presentations[reflect.name]!.scale, refitted)
})

test('manual scale and format changes replace the fixed range without following later frames', context => {
  const scope = effectScope()
  context.after(() => scope.stop())
  const currentFrame = ref(acquired([0.2, 0.2001, 0.1999]))
  const workspace = scope.run(() => useWorkspace(() => [reflect], () => currentFrame.value))!
  workspace.autoscale()
  const manual = { min: -30, max: -10, referencePosition: 9 }
  workspace.setScale(manual)
  manual.min = -100
  currentFrame.value = acquired([0.01, 0.3, 0.4])
  assert.deepEqual(getTraceScale(currentFrame.value, reflect, workspace.activePresentation.value),
    { min: -30, max: -10, referencePosition: 9 })
  workspace.setFormat('phase')
  assert.deepEqual(workspace.activePresentation.value!.scale, FORMAT_INFO.phase.fallback)
  currentFrame.value = acquired([0.2, 0.2, 0.2], [0.1, 0.2, 0.3])
  assert.deepEqual(getTraceScale(currentFrame.value, reflect, workspace.activePresentation.value), FORMAT_INFO.phase.fallback)
})

test('Autoscale without finite display values preserves the applied range and becomes unavailable', context => {
  const scope = effectScope()
  context.after(() => scope.stop())
  const currentFrame = ref<AcquiredFrame | null>(null)
  const workspace = scope.run(() => useWorkspace(() => [reflect], () => currentFrame.value))!
  workspace.setScale({ min: -20, max: -10, referencePosition: 9 })
  const applied = { ...workspace.activePresentation.value!.scale }
  const missing = acquired([0.2, 0.3, 0.4])
  missing.traces = []
  const unavailable = acquired([0.2, 0.3, 0.4])
  unavailable.traces[0]!.available = false
  const invalid = acquired([0.2, 0.3, 0.4])
  invalid.traces[0]!.valid.fill(false)
  for (const snapshot of [null, missing, unavailable, invalid, acquired([0, 0, 0])]) {
    currentFrame.value = snapshot
    assert.equal(workspace.canAutoscale.value, false)
    workspace.autoscale()
    assert.deepEqual(workspace.activePresentation.value!.scale, applied)
  }
  currentFrame.value = acquired([0.2, 0.3, 0.4])
  assert.equal(workspace.canAutoscale.value, true)
  workspace.setFormat('phase')
  currentFrame.value = acquired([0, 0, 0])
  assert.equal(workspace.canAutoscale.value, false)
  workspace.autoscale()
  assert.deepEqual(workspace.activePresentation.value!.scale, FORMAT_INFO.phase.fallback)
})

test('Real / Imag Autoscale fits both components once and holds their common range', context => {
  const scope = effectScope()
  context.after(() => scope.stop())
  const currentFrame = ref(acquired([1, 2, 3], [-5, 6, 1]))
  const workspace = scope.run(() => useWorkspace(() => [reflect], () => currentFrame.value))!
  workspace.setFormat('realimag')
  workspace.autoscale()
  const fitted = { ...workspace.activePresentation.value!.scale }
  assert.ok(fitted.min <= -5 && fitted.max >= 6)
  currentFrame.value = acquired([10, 20, 30], [-50, 60, 10])
  assert.deepEqual(getTraceScale(currentFrame.value, reflect, workspace.activePresentation.value), fitted)
})

test('fixed tiny ranges retain relative ticks independently of how their scale was applied', () => {
  assert.equal(usesRelativeAxis({ min: 0.9064, max: 0.90645 }), true)
  assert.equal(usesRelativeAxis({ min: -1000.1, max: -1000 }), true)
  assert.equal(usesRelativeAxis({ min: -0.00001, max: 0.00001 }), true)
  assert.equal(usesRelativeAxis({ min: -14.01, max: -13.91 }), false)
  assert.equal(usesRelativeAxis(FORMAT_INFO.db.fallback), false)
})

test('unrepresentable Autoscale ranges cannot replace a fixed scale or reactivate per-frame fitting', context => {
  const scope = effectScope()
  context.after(() => scope.stop())
  const currentFrame = ref(acquired([1e20, 1e20]))
  const workspace = scope.run(() => useWorkspace(() => [reflect], () => currentFrame.value))!
  workspace.setFormat('real')
  workspace.setScale({ min: -10, max: 10, referencePosition: 8 })
  const applied = { ...workspace.activePresentation.value!.scale }
  for (const real of [[1e20, 1e20], [-1e308, 1e308]]) {
    currentFrame.value = acquired(real)
    workspace.autoscale()
    assert.deepEqual(workspace.activePresentation.value!.scale, applied)
    assert.deepEqual(getTraceScale(currentFrame.value, reflect, workspace.activePresentation.value), applied)
    assert.equal(workspace.canAutoscale.value, false)
  }
})

test('display selection keeps a larger catalog, replaces only the active slot and permits an empty display', async context => {
  const scope = effectScope()
  context.after(() => scope.stop())
  const catalog = ref(Array.from({ length: 5 }, (_, index) => ({ ...reflect, name: `Trace ${index + 1}` })))
  const workspace = scope.run(() => useWorkspace(() => catalog.value, () => null))!
  assert.deepEqual(workspace.displayedNames.value, ['Trace 1', 'Trace 2', 'Trace 3', 'Trace 4'])
  workspace.selectTrace('Trace 2')
  workspace.selectTrace('Trace 5')
  assert.deepEqual(workspace.displayedNames.value, ['Trace 1', 'Trace 5', 'Trace 3', 'Trace 4'])
  assert.equal(catalog.value.length, 5)
  assert.ok(workspace.presentations['Trace 2'])
  for (const name of [...workspace.displayedNames.value]) workspace.setDisplayed(name, false)
  catalog.value = catalog.value.map(item => ({ ...item }))
  await nextTick()
  assert.deepEqual(workspace.displayedNames.value, [])
  assert.equal(workspace.activeTraceName.value, '')
  workspace.setDisplayed('Trace 5', true)
  assert.deepEqual(workspace.displayedMeasurements.value.map(item => item.name), ['Trace 5'])
  catalog.value = catalog.value.filter(item => item.name !== 'Trace 5')
  await nextTick()
  assert.deepEqual(workspace.displayedNames.value, [])
})
