import assert from 'node:assert/strict'
import test from 'node:test'
import { decodeFrame } from '../src/api/decoders'
import {
  acquiredFrame,
  projection,
  receiverTrace,
  reflection,
} from './displayFixtures'
import type { DisplayFormat } from '../src/domain/presentation'

function completeFrame(
  points: number,
  traces = 4,
  format: DisplayFormat = 'db',
) {
  const values = Array.from({ length: points }, (_, index) => index / points)
  const trace = receiverTrace(reflection, values)
  trace.projection = projection(values, format)
  const frame = acquiredFrame(trace)
  frame.traces = Array.from({ length: traces }, (_, index) => ({
    ...trace,
    name: `Trc${index + 1}`,
  }))
  return frame
}

for (const points of [201, 4000, 8000, 10001]) {
  test(`完整 ${points} 点四迹线帧保留所有后端采样和投影`, () => {
    const frame = decodeFrame(completeFrame(points))
    assert.equal(frame.available, true)
    if (!frame.available) return
    assert.equal(frame.originalPointCount, points)
    assert.equal(frame.displayPointCount, points)
    assert.equal(frame.decimated, false)
    assert.equal(frame.frequencyHz.length, points)
    assert.equal(frame.sampleIndexes.length, points)
    for (const trace of frame.traces) {
      assert.equal(trace.real.length, points)
      assert.equal(trace.projection.values.length, points)
    }
  })
}

for (const format of ['smith', 'invSmith', 'polar', 'realimag'] as const) {
  test(`${format} 完整最大点数投影使用同一传输容量`, () => {
    const frame = decodeFrame(completeFrame(10001, 4, format))
    assert.equal(frame.available, true)
    if (!frame.available) return
    assert.equal(frame.traces[0]!.projection.values.length, 10001)
  })
}

test('帧解码仍拒绝单条或整体采样容量超限', () => {
  assert.throws(
    () => decodeFrame(completeFrame(10002)),
    /frame.displayPointCount/,
  )
  assert.throws(
    () => decodeFrame(completeFrame(10001, 5)),
    /frame sample budget/,
  )
})
