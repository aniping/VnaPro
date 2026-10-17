import assert from 'node:assert/strict'
import test from 'node:test'
import { sweepCursorPoint } from '../src/plot/sweepCursor'
import type { SelectableCurve } from '../src/plot/traceHitTest'

test('Smith/Polar扫描符号补偿CSS缩放，未采点与窗外点不制造位置', () => {
  const geometry = { width: 800, height: 400, scaleX: 0.5, scaleY: 1,
    rect: { left: 0, top: 0, width: 800, height: 400 } }
  for (const format of ['smith', 'polar', 'invSmith'] as const) {
    const curve: SelectableCurve = { kind: 'circular', format, radius: 1,
      color: '#8f0', active: true, name: 'T1',
      points: [{ real: 0, imaginary: 0 }, { real: 0.5, imaginary: -0.5 }, null] }
    assert.deepEqual(sweepCursorPoint(curve, 2, geometry), { x: 582, y: 291, index: 1 })
    assert.deepEqual(sweepCursorPoint(curve, 3, geometry), { x: 582, y: 291, index: 1 })
    assert.equal(sweepCursorPoint({ ...curve, points: [{ real: 4, imaginary: 0 }] }, 1, geometry), null)
    assert.equal(sweepCursorPoint({ ...curve, points: [null] }, 1, geometry), null)
  }
})
