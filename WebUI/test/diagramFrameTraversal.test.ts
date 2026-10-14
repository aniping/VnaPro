import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, shallowReactive, type Ref } from 'vue'
import type { AcquiredFrame, InstrumentStatus } from '../src/api/types'
import {
  acquiredFrame,
  instrumentStatus,
  presentation,
  receiverTrace,
  reflection,
} from './displayFixtures'
import { findComponent, sfcHarness } from './sfcHarness'

test('retained DUT metadata never combines an old frame sequence with the new calibration run', async (context) => {
  const harness = await sfcHarness('src/App.vue', {}, {
    fetch: async () => new Response('{}'),
    setTimeout,
    clearTimeout,
  })
  context.after(harness.dispose)
  const status = harness.bindings.status as Ref<InstrumentStatus | null>
  const saved = harness.bindings.frame as Ref<AcquiredFrame | null>
  status.value = {
    ...instrumentStatus(),
    runId: '6',
    acquisitionRevision: '7',
    revision: '88',
  }
  saved.value = { ...acquiredFrame(), frameSeq: '44', runId: '2', revision: '3' }
  let metadata = findComponent(harness.render(), 'InstrumentStatusBar')!.props!
  assert.equal(metadata['frame-seq'], '44')
  assert.equal(metadata['run-id'], '2', 'retained frame tooltip must use its actual run')
  assert.equal(metadata.revision, '3', 'retained frame tooltip must use its acquisition revision')
  saved.value = null
  metadata = findComponent(harness.render(), 'InstrumentStatusBar')!.props!
  assert.equal(metadata['frame-seq'], null)
  assert.equal(metadata['run-id'], '6')
  assert.equal(metadata.revision, '7')
})

test('4x4 panes watch only their assigned trace without traversing other frame sample arrays', async () => {
  const measurements = Array.from({ length: 16 }, (_, index) => ({
    ...reflection,
    name: `Trc${index + 1}`,
    parameter: `S${Math.floor(index / 4) + 1}${index % 4 + 1}`,
    receiverPort: Math.floor(index / 4) + 1,
    sourcePort: index % 4 + 1,
  }))
  let unrelatedReads = 0
  function frame(sequence: string) {
    const traces = measurements.map((measurement, index) => {
      const trace = receiverTrace(
        measurement,
        Array.from({ length: 4000 }, () => 0.2),
      )
      if (index !== 0) {
        Object.defineProperty(trace.real, '0', {
          enumerable: true,
          configurable: true,
          get() {
            unrelatedReads++
            return 0.2
          },
        })
      }
      return trace
    })
    const result = acquiredFrame(traces[0])
    result.traces = traces
    result.frameSeq = sequence
    result.originalPointCount = 4000
    result.decimated = false
    return result
  }
  const props = shallowReactive({
    number: 1,
    layout: 'grid',
    frame: frame('1'),
    measurements: [measurements[0]!],
    allMeasurements: measurements,
    presentations: Object.fromEntries(
      measurements.map((measurement) => [measurement.name, presentation()]),
    ),
    activeTraceName: 'Trc1',
    primaryTraceName: 'Trc1',
    selectedDiagram: 1,
    config: { ...instrumentStatus().config, points: 4000 },
    frameCondition: { kind: 'current', label: '', detail: '' },
    canEditDisplay: true,
    commitMarker: async () => true,
  })
  let paints = 0
  const harness = await sfcHarness(
    'src/components/diagrams/DiagramPane.vue',
    props,
    {
      window: { devicePixelRatio: 1 },
      requestAnimationFrame: () => ++paints,
      cancelAnimationFrame() {},
    },
  )
  try {
    assert.equal(
      unrelatedReads,
      0,
      'pane initialization traversed samples belonging to the other 15 panes',
    )
    props.frame = frame('2')
    await nextTick()
    assert.equal(
      unrelatedReads,
      0,
      'new frame traversed samples belonging to the other 15 panes',
    )
    assert.ok(paints > 0, 'new immutable frame must still schedule a repaint')
    const before = paints
    props.presentations = {
      ...props.presentations,
      Trc1: {
        ...props.presentations.Trc1!,
        scale: { min: -60, max: 20 },
      },
    }
    await nextTick()
    assert.ok(paints > before, 'changed display scale must still schedule a repaint')
  } finally {
    harness.dispose()
  }
})
