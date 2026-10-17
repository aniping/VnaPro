import assert from 'node:assert/strict'
import test from 'node:test'
import { decodeFrame } from '../src/api/decoders'
import { acquiredFrame, markerProjection, projection, receiverTrace, reflection } from './displayFixtures'

function progressFixture() {
  const trace = receiverTrace(reflection, [0.6, 0.7, 0.8, 0.9])
  trace.real = [0.6, 0.7, null, null]
  trace.imag = [0, 0, null, null]
  trace.valid = [true, true, false, false]
  trace.projection = projection([-4, -3, null, null])
  return { completedPoints: 2, frame: { ...acquiredFrame(trace), current: false, frameSeq: '2' } }
}

test('未完成首扫的真实前缀随完整响应旁带返回，不冒充完整帧', () => {
  const progress = progressFixture()
  const result = decodeFrame({ available: false, current: false, reason: 'No completed frame',
    channelId: 1, sweepProgress: progress })
  assert.equal(result.available, false)
  assert.ok('sweepProgress' in result)
  assert.deepEqual(result.sweepProgress, { ...progress, frame: decodeFrame(progress.frame) })
})

test('下一扫预览与上一完整帧并存，原帧数值和序号不变', () => {
  const previous = acquiredFrame(receiverTrace(reflection, [0.1, 0.2, 0.3, 0.4]))
  const result = decodeFrame({ ...previous, sweepProgress: progressFixture() })
  assert.ok(result.available)
  assert.equal(result.frameSeq, '1')
  assert.deepEqual(result.traces[0]!.real, previous.traces[0]!.real)
  assert.ok('sweepProgress' in result)
})

for (const count of [0, -1, 1.5, 4, 5]) {
  test(`拒绝异常或已完成的预览边界 ${count}`, () => {
    const progress = { ...progressFixture(), completedPoints: count }
    assert.throws(() => decodeFrame({ available: false, current: false, reason: '',
      channelId: 1, sweepProgress: progress }), /sweepProgress/)
  })
}

test('未采后缀不能通过零值、valid或marker伪装成已取得测量', () => {
  const changed = progressFixture()
  changed.frame.traces[0]!.real[2] = 0
  assert.throws(() => decodeFrame({ available: false, current: false, reason: '',
    channelId: 1, sweepProgress: changed }), /sweepProgress/)
  const marker = progressFixture()
  marker.frame.traces[0]!.projection.marker = markerProjection({ available: true, valid: true,
    sampleIndex: 0, frequencyHz: 10e6, real: 0.6, imag: 0, value: -4, valueState: 'finite' })
  assert.throws(() => decodeFrame({ available: false, current: false, reason: '',
    channelId: 1, sweepProgress: marker }), /sweepProgress/)
})

test('不接受其他通道、完整帧标记或递归进度帧', () => {
  for (const mutation of [
    (frame: ReturnType<typeof progressFixture>['frame']) => { frame.channelId = 2 },
    (frame: ReturnType<typeof progressFixture>['frame']) => { frame.current = true },
    (frame: ReturnType<typeof progressFixture>['frame']) => { Object.assign(frame, { sweepProgress: progressFixture() }) },
  ]) {
    const changed = progressFixture()
    mutation(changed.frame)
    assert.throws(() => decodeFrame({ available: false, current: false, reason: '',
      channelId: 1, sweepProgress: changed }), /sweepProgress/)
  }
})

test('多通道预览不能绕过总迹线预算，也不能携带其他通道的迹线', () => {
  const channel = (id: number) => {
    const progress = progressFixture()
    progress.frame.channelId = id
    progress.frame.traces = Array.from({ length: 65 }, (_, index) => ({
      ...progress.frame.traces[0]!, name: `Ch${id}T${index}`, channelId: id,
    }))
    return { available: false, current: false, reason: '', channelId: id, sweepProgress: progress }
  }
  assert.throws(() => decodeFrame({ ...channel(1), channelFrames: [channel(1), channel(2)] }), /budget/)
  const foreign = progressFixture()
  foreign.frame.traces[0]!.channelId = 2
  assert.throws(() => decodeFrame({ available: false, current: false, reason: '', channelId: 1,
    sweepProgress: foreign }), /sweepProgress/)
})
