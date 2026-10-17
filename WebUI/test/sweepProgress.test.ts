import assert from 'node:assert/strict'
import test from 'node:test'
import { currentSweepProgress, sweepTraceSegments } from '../src/domain/sweepProgress'
import { acquiredFrame, instrumentStatus, projection, receiverTrace, reflection } from './displayFixtures'
import type { ChannelStatus, SweepProgress } from '../src/api/types'

function fixture() {
  const previous = acquiredFrame(receiverTrace(reflection, [0.1, 0.2, 0.3, 0.4]))
  previous.traces[0]!.projection = projection([-60, -50, -40, -30])
  const next = acquiredFrame(receiverTrace(reflection, [0.6, 0.7, 0.8, 0.9]))
  next.current = false
  next.frameSeq = '2'
  next.traces[0]!.real = [0.6, 0.7, null, null]
  next.traces[0]!.imag = [0, 0, null, null]
  next.traces[0]!.valid = [true, true, false, false]
  next.traces[0]!.projection = projection([-20, -10, null, null])
  const progress: SweepProgress = { completedPoints: 2, frame: next }
  const channel: ChannelStatus = { ...instrumentStatus(), channelId: 1, name: 'Ch1', state: 'Running' }
  return { previous, progress, channel }
}

test('真实新前缀覆盖旧扫前缀，未扫旧尾保留且两扫不连线', () => {
  const { previous, progress } = fixture()
  const frozen = JSON.stringify(previous)
  const segments = sweepTraceSegments(previous, progress, reflection.name)
  assert.equal(segments.length, 2)
  assert.deepEqual(segments[0]!.trace.projection.values, [null, null, -40, -30])
  assert.deepEqual(segments[1]!.trace.projection.values, [-20, -10, null, null])
  assert.equal(JSON.stringify(previous), frozen)
  assert.deepEqual(sweepTraceSegments(null, progress, reflection.name).map((item) => item.trace.projection.values),
    [[-20, -10, null, null]])
})

test('同配置Restart保留旧扫尾，配置/频轴/校准/投影改变不混旧尾', () => {
  const { previous, progress } = fixture()
  previous.runId = '0'
  assert.equal(sweepTraceSegments(previous, progress, reflection.name).length, 2)
  for (const key of ['revision', 'measurementRevision', 'displayRevision', 'calibrationRevision'] as const) {
    assert.equal(sweepTraceSegments({ ...previous, [key]: '99' }, progress, reflection.name).length, 1)
  }
  assert.equal(sweepTraceSegments({ ...previous, frequencyHz: [11e6, 22e6, 33e6, 44e6] }, progress, reflection.name).length, 1)
  assert.equal(sweepTraceSegments(previous, null, reflection.name)[0]!.trace, previous.traces[0])
})

test('掉线/故障/新运行/版本变化立即移除进度，完整帧不会被旧进度覆盖', () => {
  const { previous, progress, channel } = fixture()
  const response = { ...previous, sweepProgress: progress }
  assert.equal(currentSweepProgress(response, channel, true, '1', '1'), progress)
  assert.equal(currentSweepProgress(response, channel, false, '1', '1'), null)
  for (const changed of [{ state: 'Faulted' as const }, { state: 'Ready' as const },
    { runId: '2' }, { acquisitionRevision: '2' }, { activeConfigValid: false }, { channelId: 2 }])
    assert.equal(currentSweepProgress(response, { ...channel, ...changed }, true, '1', '1'), null)
  assert.equal(currentSweepProgress(response, channel, true, '2', '1'), null)
  assert.equal(currentSweepProgress(response, channel, true, '1', '2'), null)
  assert.equal(currentSweepProgress({ ...response, frameSeq: '2' }, channel, true, '1', '1'), null)
})
