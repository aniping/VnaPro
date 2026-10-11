import assert from 'node:assert/strict'
import test from 'node:test'
import { effectScope, ref } from 'vue'
import type { AcquiredFrame, Measurement } from '../src/api/types'
import { useWorkspace } from '../src/composables/useWorkspace'
import { defaultTracePresentation } from '../src/domain/presentation'
import { getMarkerReadout } from '../src/plot'
import { exportVisibleFrameCsv } from '../src/plot/exportCsv'
import { MIN_SMITH_RADIUS, nearestSmithIndex, normalizedImpedance, smithAutoscale, smithPoint, smithSeries, smithViewport, supportsSmith, validSmithRadius } from '../src/plot/smithMath'
import { paintSmithDiagram } from '../src/plot/smithRenderer'
import { markerResult } from '../src/plot/traceMath'

const reflection: Measurement = {
  name: 'Reflect', kind: 'sParameter', receiverKind: 'b', receiverPort: 1, sourcePort: 1,
  parameter: 'S11', unit: '1', calibration: 'uncalibrated',
}

function acquired(real: (number | null)[], imag = real.map(() => 0)): AcquiredFrame {
  return {
    available: true, current: true, channelId: 1, runId: '1', revision: '1', measurementRevision: '1', frameSeq: '1',
    originalPointCount: real.length, displayPointCount: real.length, decimated: false,
    frequencyHz: real.map((_, index) => (index + 1) * 10e6),
    traces: [{ ...reflection, available: true, real, imag, valid: real.map(value => value !== null) }],
  }
}

function canvasProbe(context: { after: (callback: () => void) => void }) {
  const oldWindow = Object.getOwnPropertyDescriptor(globalThis, 'window')
  Object.defineProperty(globalThis, 'window', { configurable: true, value: { devicePixelRatio: 1.5 } })
  context.after(() => {
    if (oldWindow) Object.defineProperty(globalThis, 'window', oldWindow)
    else Reflect.deleteProperty(globalThis, 'window')
  })
  const lines: number[][] = []
  const isolated: number[][] = []
  const moves: { color: string; x: number; y: number }[] = []
  const drawing = {
    strokeStyle: '', setTransform() {}, clearRect() {}, setLineDash() {}, fillRect() {},
    save() {}, restore() {}, beginPath() {}, clip() {}, stroke() {}, rect() {}, fill() {}, fillText() {},
    arc(x: number, y: number) { if (this.strokeStyle === '#60e600') isolated.push([x, y]) },
    moveTo(x: number, y: number) {
      if (['#60e600', '#ffff26'].includes(this.strokeStyle)) moves.push({ color: this.strokeStyle, x, y })
    },
    lineTo(x: number, y: number) { if (this.strokeStyle === '#60e600') lines.push([x, y]) },
  }
  const canvas = { style: {}, width: 0, height: 0, getContext: () => drawing }
  return { canvas, lines, isolated, moves }
}

test('Smith is a reflection-only format and preserves Cartesian scale across format switches', context => {
  const scope = effectScope()
  context.after(() => scope.stop())
  const current = ref(acquired([0.1, 0.2]))
  const workspace = scope.run(() => useWorkspace(() => [reflection], () => current.value))!
  workspace.setScale({ min: -30, max: -10, referencePosition: 9 })
  workspace.setFormat('smith')
  assert.equal(workspace.activePresentation.value!.format, 'smith')
  workspace.setFormat('db')
  assert.deepEqual(workspace.activePresentation.value!.scale, { min: -30, max: -10, referencePosition: 9 })
  const transmission = { ...reflection, name: 'Transmit', receiverPort: 2, parameter: 'S21' }
  const other = scope.run(() => useWorkspace(() => [transmission], () => current.value))!
  other.setFormat('smith')
  assert.equal(other.activePresentation.value!.format, 'db')
})

test('Smith marker retains valid open-circuit Gamma and exports complex rather than scalar values', () => {
  const current = acquired([1, 0, null], [0, 1, 0])
  const presentation = { ...defaultTracePresentation(), format: 'smith' as const,
    marker: { enabled: true, frequencyHz: 10e6 } }
  const readout = getMarkerReadout(current, reflection, presentation)
  assert.equal(readout?.valid, true)
  assert.match(readout?.valueText ?? '', /Γ.*1.*z.*∞/)
  const csv = exportVisibleFrameCsv(current, { [reflection.name]: presentation })
  assert.match(csv, /Smith 数据为无量纲 Γ 与归一化阻抗.*不假定物理参考阻抗/)
  assert.match(csv, /gamma_real,gamma_imaginary,normalized_r,normalized_x,impedance_state/)
  assert.match(csv, /uncalibrated,1,0,,,open/)
  assert.match(csv, /uncalibrated,0,1,0,1,finite/)
})

test('normalized impedance identifies matched, short, open and reactive samples without physical ohms', () => {
  assert.deepEqual(normalizedImpedance(0, 0), { state: 'finite', real: 1, imaginary: 0 })
  assert.deepEqual(normalizedImpedance(-1, 0), { state: 'finite', real: 0, imaginary: 0 })
  assert.equal(normalizedImpedance(1, 0).state, 'open')
  assert.deepEqual(normalizedImpedance(0, 1), { state: 'finite', real: 0, imaginary: 1 })
  assert.equal(normalizedImpedance(NaN, 0).state, 'invalid')
  assert.equal(normalizedImpedance(Number.MAX_VALUE, Number.MAX_VALUE).state, 'finite')
  assert.equal(normalizedImpedance(1, Number.MIN_VALUE).state, 'overflow')
})

test('Smith projection remains circular in wide and narrow viewports at unequal CSS scales and DPR', () => {
  for (const [width, height] of [[850, 650], [300, 650], [420, 300]]) {
    for (const scaleX of [0.75, 1, 1.3]) {
      for (const scaleY of [0.8, 1.2]) {
        const viewport = smithViewport(width!, height!, scaleX, scaleY)
        const real = smithPoint({ real: 1, imaginary: 0 }, viewport, 1)
        const imaginary = smithPoint({ real: 0, imaginary: 1 }, viewport, 1)
        for (const dpr of [1, 1.5, 2]) {
          assert.ok(Math.abs((real.x - viewport.centerX) * scaleX * dpr
            - (viewport.centerY - imaginary.y) * scaleY * dpr) < 1e-9)
        }
        assert.equal(smithPoint({ real: 0, imaginary: 0 }, viewport, 1).x, width! / 2)
      }
    }
  }
})

test('Smith actual-sample selection is two-dimensional and keeps out-of-unit-circle and invalid data semantics', () => {
  const trace = acquired([0.3, 0.3, null, 1.2], [-0.3, 0.3, 0, 0]).traces[0]!
  const viewport = smithViewport(400, 400)
  const selected = smithPoint({ real: 0.3, imaginary: 0.3 }, viewport, 1)
  assert.equal(nearestSmithIndex(trace, viewport, 1, selected.x, selected.y), 1)
  assert.equal(smithSeries(trace)[2], null)
  assert.equal(smithSeries(trace)[3]?.real, 1.2)
  assert.equal(smithAutoscale(trace), 1.44)
  assert.equal(supportsSmith({ ...reflection, receiverPort: 2, sourcePort: 2, parameter: 'S22' }), true)
  assert.equal(supportsSmith({ ...reflection, sourcePort: 2, parameter: 'S12' }), false)
})

test('Smith Autoscale fixes a per-trace radius until another click and retains it through Cartesian display', context => {
  const scope = effectScope()
  context.after(() => scope.stop())
  const current = ref(acquired([0.2, 0.3]))
  const workspace = scope.run(() => useWorkspace(() => [reflection], () => current.value))!
  workspace.setFormat('smith')
  workspace.autoscale()
  const first = workspace.activePresentation.value!.smithRadius
  assert.equal(first, 0.36)
  current.value = acquired([1, 2])
  assert.equal(workspace.activePresentation.value!.smithRadius, first)
  workspace.autoscale()
  assert.equal(workspace.activePresentation.value!.smithRadius, 2.4)
  workspace.setFormat('db')
  workspace.setFormat('smith')
  assert.equal(workspace.activePresentation.value!.smithRadius, 2.4)
  current.value.traces[0]!.valid.fill(false)
  assert.equal(workspace.canAutoscale.value, false)
  workspace.autoscale()
  assert.equal(workspace.activePresentation.value!.smithRadius, 2.4)
})

test('Smith renderer breaks the path at invalid points instead of connecting across the gap', context => {
  const { canvas, lines, isolated } = canvasProbe(context)
  paintSmithDiagram(canvas as unknown as HTMLCanvasElement, {
    width: 400, height: 300, radius: 1,
    curves: [{ color: '#60e600', active: true,
      points: [{ real: 0, imaginary: 0 }, null, { real: 0.2, imaginary: 0.2 }, { real: 0.3, imaginary: 0.3 }] }],
  }, { width: 400, height: 300, left: 0.3, top: 0.2 })
  assert.equal(lines.length, 1)
  assert.equal(isolated.length, 1)
  assert.ok(canvas.width >= 600 && canvas.height >= 450)
})

test('Smith overlay uses the active window radius for both traces and marker coordinates', context => {
  const { canvas, moves } = canvasProbe(context)
  const sample = { real: 0.3, imaginary: 0.4 }
  const curves = [
    { color: '#60e600', active: false, savedRadius: 0.2, points: [sample] },
    { color: '#ffff26', active: true, savedRadius: 2, points: [sample] },
  ]
  for (const radius of [1, 2]) {
    moves.length = 0
    paintSmithDiagram(canvas as unknown as HTMLCanvasElement, { width: 400, height: 300, radius, curves },
      { width: 400, height: 300, left: 0.3, top: 0.2 })
    const marker = smithPoint(sample, smithViewport(400, 300), radius)
    assert.equal(moves.length, 2)
    for (const move of moves) {
      assert.equal(move.x, marker.x)
      assert.equal(move.y, marker.y)
    }
  }
})

test('Smith radius input rejects subnormal, nonfinite and negative values while retaining the applied radius', context => {
  const scope = effectScope()
  context.after(() => scope.stop())
  const workspace = scope.run(() => useWorkspace(() => [reflection], () => acquired([0.2, 0.3])))!
  workspace.setSmithRadius(2)
  for (const radius of [Number.MIN_VALUE, MIN_SMITH_RADIUS / 2, 0, -1, NaN, Infinity]) {
    assert.equal(validSmithRadius(radius), false)
    workspace.setSmithRadius(radius)
    assert.equal(workspace.activePresentation.value!.smithRadius, 2)
  }
  workspace.setSmithRadius(MIN_SMITH_RADIUS)
  assert.equal(workspace.activePresentation.value!.smithRadius, MIN_SMITH_RADIUS)
  const { canvas } = canvasProbe(context)
  assert.doesNotThrow(() => paintSmithDiagram(canvas as unknown as HTMLCanvasElement,
    { width: 400, height: 300, radius: Number.MIN_VALUE, curves: [] }, { width: 400, height: 300, left: 0, top: 0 }))
  assert.equal(canvas.width, 0)
})

test('Smith CW markers preserve the selected actual sample among repeated frequencies', () => {
  const current = acquired([0.1, 0.2, 0.3], [0.1, 0.2, 0.3])
  current.frequencyHz.fill(10e6)
  const selected = markerResult(current, current.traces[0], 10e6, 'smith', 2)
  assert.equal(selected?.index, 2)
  assert.equal(selected?.real, 0.3)
  assert.equal(markerResult(current, current.traces[0], 10e6, 'smith', 1)?.index, 1)
  current.frequencyHz = [10e6, 20e6, 30e6]
  assert.equal(markerResult(current, current.traces[0], 10e6, 'smith', 2)?.index, 0)
})
