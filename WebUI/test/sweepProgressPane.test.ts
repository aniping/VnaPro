import assert from 'node:assert/strict'
import test from 'node:test'
import { reactive, type Ref } from 'vue'
import type { AcquiredFrame, ChannelStatus, FrameResponse, SweepProgress } from '../src/api/types'
import { acquiredFrame, instrumentStatus, markerProjection, presentation, projection, receiverTrace, reflection, transmission } from './displayFixtures'
import { findElements, sfcHarness } from './sfcHarness'
import type { SelectableCurve } from '../src/plot/traceHitTest'

function fixture() {
  const frame = acquiredFrame(receiverTrace(reflection, [0.1, 0.2, 0.3, 0.4]))
  frame.traces[0]!.projection = projection([-60, -50, -40, -30])
  frame.traces[0]!.projection.marker = markerProjection({ available: true, valid: true,
    frequencyHz: 20e6, sampleIndex: 1, real: 0.2, imag: 0, value: -50, valueState: 'finite' })
  const next = { ...frame, current: false, frameSeq: '2', traces: [receiverTrace(reflection, [0.6, 0.7, 0.8, 0.9])] }
  next.traces[0]!.real = [0.6, 0.7, null, null]
  next.traces[0]!.imag = [0, 0, null, null]
  next.traces[0]!.valid = [true, true, false, false]
  next.traces[0]!.projection = projection([-20, -10, null, null])
  const progress: SweepProgress = { completedPoints: 2, frame: next }
  const channel: ChannelStatus = { ...instrumentStatus(), channelId: 1, name: 'Ch1', state: 'Running',
    config: { ...instrumentStatus().config, points: 4, startHz: 10e6, stopHz: 40e6 } }
  const view = presentation()
  view.marker = { enabled: true, frequencyHz: 20e6, sampleIndex: 1, discrete: true }
  return reactive({ number: 1, layout: 'single', frame: frame as AcquiredFrame | null,
    channelFrames: { 1: { ...frame, sweepProgress: progress } } as Record<number, FrameResponse>,
    channels: [channel], activeChannelId: 1, connected: true,
    measurements: [reflection], allMeasurements: [reflection], presentations: { Reflect: view },
    activeTraceName: 'Reflect', primaryTraceName: 'Reflect', selectedDiagram: 1, displayRevision: '1', revision: '1',
    config: channel.config, frameCondition: { kind: 'current', label: '', detail: '' }, canEditDisplay: true,
    commitMarker: async () => true, commitReferencePosition: async () => true })
}

async function pane() {
  const props = fixture()
  const view = await sfcHarness('src/components/diagrams/DiagramPane.vue', props, {
    window: { devicePixelRatio: 1 }, requestAnimationFrame: () => 1, cancelAnimationFrame() {},
  })
  const bindings = view.bindings as unknown as {
    curves: Ref<SelectableCurve[]>
    hasDrawable: Ref<boolean>
    markerModels: Ref<{ result: { value: number } }[]>
    sweepCursors: Ref<{ x: number; y: number; index: number }[]>
  }
  return { props, view, bindings,
    arrows: () => findElements(view.render(), (node) => node.props?.['data-sweep-cursor'] !== undefined) }
}

test('慢扫曲线局部推进，向下箭头落在真实前缀末点，正式Marker保留完整帧', async () => {
  const { props, view, bindings, arrows } = await pane()
  try {
    assert.equal(bindings.hasDrawable.value, true)
    assert.equal(bindings.curves.value.length, 2)
    const prefix = bindings.curves.value[1]!
    assert.equal(prefix.kind, 'cartesian')
    if (prefix.kind === 'cartesian') {
      assert.equal(prefix.values[1], -10)
      assert.ok(Number.isNaN(prefix.values[2]))
    }
    assert.equal(bindings.markerModels.value[0]!.result.value, -50)
    assert.equal(arrows().length, 1)
    assert.equal(arrows()[0]!.props!['data-completed-points'], 2)
    assert.equal(bindings.sweepCursors.value[0]!.index, 1)
    assert.equal(bindings.sweepCursors.value[0]!.x, 1 + 698 / 3)
    props.connected = false
    assert.equal(arrows().length, 0)
    assert.equal(bindings.curves.value.length, 1)
  } finally { view.dispose() }
})

test('首扫尚未完成就能画出真实前缀，完成后只保留正常完整曲线', async () => {
  const { props, view, bindings, arrows } = await pane()
  try {
    const progress = props.channelFrames[1]!.sweepProgress!
    props.frame = null
    props.channelFrames[1] = { available: false, current: false, reason: 'No completed frame',
      channelId: 1, sweepProgress: progress }
    assert.equal(bindings.hasDrawable.value, true)
    assert.equal(bindings.curves.value.length, 1)
    assert.equal(bindings.markerModels.value.length, 0)
    assert.equal(arrows().length, 1)
    const complete = { ...progress.frame, current: true }
    props.frame = complete
    props.channelFrames[1] = complete
    assert.equal(bindings.curves.value.length, 1)
    assert.equal(arrows().length, 0)
  } finally { view.dispose() }
})

test('同图跨通道一快一慢，快通道完整帧更新不覆盖慢通道前缀与扫描符号', async () => {
  const { props, view, bindings, arrows } = await pane()
  try {
    const fast = { ...transmission, name: 'Ch2S21', channelId: 2 }
    props.measurements.push(fast)
    props.allMeasurements.push(fast)
    Object.assign(props.presentations, { [fast.name]: presentation() })
    props.channels.push({ ...props.channels[0]!, channelId: 2, name: 'Ch2', runId: '7',
      config: { ...props.config, points: 3, startHz: 20e6, stopHz: 60e6 } })
    const fastFrame = { ...acquiredFrame(receiverTrace(fast, [0.2, 0.3, 0.4])),
      channelId: 2, runId: '7', frameSeq: '90', frequencyHz: [20e6, 40e6, 60e6] }
    fastFrame.traces[0]!.projection = projection([-30, -20, -10])
    props.channelFrames[2] = fastFrame
    const slowProgress = props.channelFrames[1]!.sweepProgress
    assert.equal(bindings.curves.value.length, 3)
    assert.equal(arrows().length, 1)
    assert.equal(arrows()[0]!.props!['data-sweep-channel'], 1)
    const fastCurve = bindings.curves.value.find((curve) => curve.name === fast.name)!
    assert.equal(fastCurve.kind, 'cartesian')
    if (fastCurve.kind === 'cartesian') {
      assert.deepEqual(fastCurve.frequencyHz, [20e6, 40e6, 60e6])
      assert.deepEqual(fastCurve.values, [-30, -20, -10])
    }
    const updated = { ...fastFrame, frameSeq: '91', traces: [receiverTrace(fast, [0.4, 0.5, 0.6])] }
    updated.traces[0]!.projection = projection([-15, -10, -5])
    props.channelFrames[2] = updated
    props.activeChannelId = 2
    props.primaryTraceName = fast.name
    assert.equal(props.channelFrames[1]!.sweepProgress, slowProgress)
    assert.equal(bindings.curves.value.length, 3)
    assert.equal(arrows().length, 1)
    assert.equal(arrows()[0]!.props!['data-sweep-channel'], 1)
    assert.equal(arrows()[0]!.props!['data-completed-points'], 2)
    assert.equal(bindings.markerModels.value[0]!.result.value, -50)
  } finally { view.dispose() }
})

test('Real/Imag双分量各自保留旧尾和推进箭头，Marker仍读取完整扫频', async () => {
  const { props, view, bindings, arrows } = await pane()
  try {
    props.presentations.Reflect.format = 'realimag'
    props.presentations.Reflect.cartesianFormat = 'realimag'
    props.presentations.Reflect.scale = { min: -1, max: 1, referencePosition: 5 }
    const response = props.channelFrames[1]!
    assert.ok(response.available)
    const previous = response.traces[0]!
    previous.imag = [0.4, 0.3, 0.2, 0.1]
    previous.projection = projection([0.1, 0.2, 0.3, 0.4], 'realimag', previous.imag)
    previous.projection.marker = markerProjection({ available: true, valid: true,
      frequencyHz: 20e6, sampleIndex: 1, real: 0.2, imag: 0.3, value: 0.2,
      secondaryValue: 0.3, valueState: 'finite', secondaryState: 'finite' })
    const partial = response.sweepProgress!.frame.traces[0]!
    partial.imag = [-0.6, -0.7, null, null]
    partial.projection = projection([0.6, 0.7, null, null], 'realimag', partial.imag)
    const curves = bindings.curves.value
    assert.equal(curves.length, 4)
    assert.ok(curves.every((curve) => curve.kind === 'cartesian'))
    const cartesian = curves.filter((curve) => curve.kind === 'cartesian')
    assert.deepEqual(cartesian.map((curve) => curve.dashed), [false, true, false, true])
    assert.deepEqual(cartesian[0]!.values, [NaN, NaN, 0.3, 0.4])
    assert.deepEqual(cartesian[1]!.values, [NaN, NaN, 0.2, 0.1])
    assert.deepEqual(cartesian[2]!.values, [0.6, 0.7, NaN, NaN])
    assert.deepEqual(cartesian[3]!.values, [-0.6, -0.7, NaN, NaN])
    assert.equal(arrows().length, 2)
    assert.ok(arrows().every((arrow) => arrow.props!['data-completed-points'] === 2))
    assert.deepEqual(bindings.sweepCursors.value.map((cursor) => cursor.index), [1, 1])
    assert.equal(bindings.sweepCursors.value[0]!.x, bindings.sweepCursors.value[1]!.x)
    assert.ok(bindings.sweepCursors.value[0]!.y < bindings.sweepCursors.value[1]!.y)
    assert.equal(bindings.markerModels.value[0]!.result.value, 0.2)
  } finally { view.dispose() }
})
