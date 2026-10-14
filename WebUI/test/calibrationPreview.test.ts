import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive, type Ref } from 'vue'
import { decodeCalibrationPreview } from '../src/api/calibrationDecoders'
import { InstrumentApi } from '../src/api/instrumentApi'
import type {
  CalibrationCommand,
  CalibrationPreview,
  CalibrationPreviewResponse,
  CalibrationSnapshot,
} from '../src/api/calibrationTypes'
import { instrumentStatus } from './displayFixtures'
import { findComponent, findElements, sfcHarness } from './sfcHarness'
import type { InstrumentStatus } from '../src/api/types'

function preview(stepId = 'open-2'): CalibrationPreview {
  return {
    available: true,
    sessionId: '6',
    stepId,
    calibrationRevision: '5',
    parameter: 'S22',
    runId: '3',
    frameSeq: '1',
    originalPointCount: 3,
    displayPointCount: 3,
    sampleIndexes: [0, 1, 2],
    decimated: false,
    ifBandwidthHz: 1000,
    frequencyHz: [10e6, 20e6, 30e6],
    valid: [true, true, true],
    real: [0.1, 0.2, 0.3],
    imag: [0, 0, 0],
    dbValues: [-20, -13.9794, -10.4575],
    dbStates: ['finite', 'finite', 'finite'],
    phaseValues: [0, 0, 0],
    phaseStates: ['finite', 'finite', 'finite'],
  }
}
function calibration(): CalibrationSnapshot {
  return {
    revision: '5',
    collecting: true,
    capturePending: false,
    sessionId: '6',
    method: 'onePort',
    port1: 2,
    port2: 0,
    steps: [
      {
        id: 'open-2',
        kind: 'open',
        port1: 2,
        port2: 0,
        captured: true,
        runId: '3',
        frameSeq: '1',
      },
      {
        id: 'short-2',
        kind: 'short',
        port1: 2,
        port2: 0,
        captured: true,
        runId: '4',
        frameSeq: '1',
      },
      {
        id: 'load-2',
        kind: 'load',
        port1: 2,
        port2: 0,
        captured: false,
        runId: '0',
        frameSeq: '0',
      },
    ],
    selectedSetId: '',
    enabled: false,
    compatible: false,
    reason: '',
    sets: [],
  }
}

/** 保存标准的服务合同：完整频轴、独立复数与投影，以及明确的无效/零值状态。 */
function fullPreview(points: number, peakIndex: number): CalibrationPreview {
  const value = preview()
  value.calibrationRevision = '9007199254740993'
  value.runId = '9223372036854775808'
  value.frameSeq = '18446744073709551614'
  value.originalPointCount = points
  value.displayPointCount = points
  value.sampleIndexes = Array.from({ length: points }, (_, index) => index)
  value.frequencyHz = value.sampleIndexes.map((index) => 10e6 + index * 200e3)
  value.valid = value.sampleIndexes.map(() => true)
  value.real = value.sampleIndexes.map(() => 0.01)
  value.imag = value.sampleIndexes.map(() => 0)
  value.dbValues = value.sampleIndexes.map(() => -40)
  value.dbStates = value.sampleIndexes.map(() => 'finite')
  value.phaseValues = value.sampleIndexes.map(() => 0)
  value.phaseStates = value.sampleIndexes.map(() => 'finite')
  value.real[peakIndex] = 0.9
  value.dbValues[peakIndex] = -0.9151498112135024
  value.valid[17] = false
  value.real[17] = null
  value.imag[17] = null
  value.dbValues[17] = null
  value.dbStates[17] = 'undefined'
  value.phaseValues[17] = null
  value.phaseStates[17] = 'undefined'
  value.real[29] = 0
  value.dbValues[29] = null
  value.dbStates[29] = 'negativeInfinity'
  value.phaseValues[29] = null
  value.phaseStates[29] = 'undefined'
  return value
}

for (const [points, peakIndex] of [
  [4000, 2002],
  [10001, 5003],
] as const) {
  test(`保存校准标准的 ${points} 点预览保留窄峰、全部原始索引与 uint64 身份`, () => {
    const input = fullPreview(points, peakIndex)
    const result = decodeCalibrationPreview(input)
    assert.equal(result.available, true)
    if (!result.available) return
    assert.deepEqual(result, input)
    assert.equal(result.displayPointCount, points)
    assert.equal(result.decimated, false)
    assert.equal(result.sampleIndexes[peakIndex], peakIndex)
    assert.equal(result.real[peakIndex], 0.9)
    assert.equal(result.dbValues[peakIndex], -0.9151498112135024)
    assert.equal(result.real[peakIndex - 1], 0.01)
    assert.equal(result.real[peakIndex + 1], 0.01)
    assert.equal(result.sampleIndexes[points - 1], points - 1)
    assert.equal(result.valid[17], false)
    assert.equal(result.dbStates[29], 'negativeInfinity')
    assert.equal(result.phaseStates[29], 'undefined')
  })
}

test('完整校准预览仍拒绝超限容量、身份溢出和无效投影状态', () => {
  assert.throws(
    () => decodeCalibrationPreview(fullPreview(10002, 5003)),
    /preview.displayPointCount/,
  )
  assert.throws(
    () =>
      decodeCalibrationPreview({ ...preview(), runId: '18446744073709551616' }),
    /preview.runId/,
  )
  const corrupt = fullPreview(4000, 2002)
  corrupt.dbValues[29] = 0
  assert.throws(
    () => decodeCalibrationPreview(corrupt),
    /preview.projection states/,
  )
})

test('standard preview decodes real backend projection provenance and rejects malformed or mismatched arrays', () => {
  assert.equal(decodeCalibrationPreview(preview()).available, true)
  assert.deepEqual(
    decodeCalibrationPreview({
      available: false,
      sessionId: '6',
      stepId: 'open-2',
      calibrationRevision: '5',
      reason: 'Not captured',
    }),
    {
      available: false,
      sessionId: '6',
      stepId: 'open-2',
      calibrationRevision: '5',
      reason: 'Not captured',
    },
  )
  assert.throws(
    () => decodeCalibrationPreview({ ...preview(), dbValues: [-20] }),
    /count/,
  )
  assert.throws(
    () =>
      decodeCalibrationPreview({ ...preview(), dbValues: [null, -20, -10] }),
    /states/,
  )
  assert.throws(
    () => decodeCalibrationPreview({ ...preview(), real: [null, 0.2, 0.3] }),
    /complex data/,
  )
  assert.throws(
    () => decodeCalibrationPreview({ ...preview(), sampleIndexes: [0, 1, 1] }),
    /ordering/,
  )
  assert.throws(
    () =>
      decodeCalibrationPreview({
        ...preview(),
        frequencyHz: [10e6, 10e6, 30e6],
      }),
    /ordering/,
  )
  assert.throws(
    () => decodeCalibrationPreview({ ...preview(), decimated: true }),
    /decimated/,
  )
})

test('preview transport addresses a saved standard without sending a measurement or display mutation', async () => {
  const requests: { path: string; method: string }[] = []
  const api = new InstrumentApi({
    storage: null,
    fetch: async (input, init) => {
      requests.push({ path: String(input), method: init?.method ?? 'GET' })
      return new Response(JSON.stringify(preview()))
    },
  })
  await api.calibrationPreview('6', 'open-2')
  assert.deepEqual(requests, [
    {
      path: '/api/v1/calibration/preview?sessionId=6&stepId=open-2',
      method: 'GET',
    },
  ])
})

test('setup uses actual topology selection and supported type slots; unsupported modes cannot submit', async (context) => {
  const calls: CalibrationCommand[] = []
  const status = instrumentStatus()
  status.calibration = {
    ...calibration(),
    collecting: false,
    sessionId: '',
    steps: [],
  }
  const props = reactive({
    status,
    disabled: false,
    command: async (command: CalibrationCommand) => {
      calls.push(command)
      return null
    },
  })
  const harness = await sfcHarness(
    'src/components/softtools/CalibrationWorkflow.vue',
    props,
    {},
  )
  context.after(harness.dispose)
  const button = (attribute: string, value: unknown) =>
    findElements(
      harness.render(),
      (node) => node.type === 'button' && node.props?.[attribute] === value,
    )[0]!
  assert.equal(button('data-cal-type', 'trl').props!.disabled, true)
  ;(button('data-cal-port', 1).props!.onClick as () => void)()
  ;(button('data-cal-port', 4).props!.onClick as () => void)()
  await nextTick()
  ;(harness.bindings.next as () => void)()
  await (harness.bindings.begin as () => Promise<void>)()
  assert.deepEqual(JSON.parse(JSON.stringify(calls)), [
    {
      action: 'begin',
      method: 'twoPort',
      port1: 2,
      port2: 4,
      includeIsolation: false,
    },
  ])
})

test('acquisition shows only the selected saved standard and drops a late preview for another standard', async (context) => {
  const loaders: {
    id: string
    resolve: (value: CalibrationPreviewResponse) => void
  }[] = []
  const props = reactive({
    status: { ...instrumentStatus(), calibration: calibration() },
    disabled: false,
    command: async () => null,
    loadPreview: async (_session: string, id: string) =>
      new Promise<CalibrationPreviewResponse>((resolve) => {
        loaders.push({ id, resolve })
      }),
  })
  const harness = await sfcHarness(
    'src/components/softtools/CalibrationAcquisition.vue',
    props,
    {},
  )
  context.after(harness.dispose)
  assert.equal(loaders[0]!.id, 'open-2')
  ;(harness.bindings.selectedStepId as Ref<string>).value = 'short-2'
  await nextTick()
  assert.equal(loaders[1]!.id, 'short-2')
  loaders[0]!.resolve(preview())
  await nextTick()
  assert.equal(
    (harness.bindings.preview as Ref<CalibrationPreview | null>).value,
    null,
  )
  loaders[1]!.resolve({ ...preview('short-2'), runId: '4' })
  for (let index = 0; index < 4; index++) await Promise.resolve()
  await nextTick()
  assert.equal(
    (harness.bindings.preview as Ref<CalibrationPreview | null>).value?.stepId,
    'short-2',
  )
  ;(harness.bindings.selectedStepId as Ref<string>).value = 'load-2'
  await nextTick()
  assert.equal(
    (harness.bindings.preview as Ref<CalibrationPreview | null>).value,
    null,
  )
  assert.equal(loaders.length, 2)
})

test('same-value fresh status objects every 200 ms keep the saved preview and do not repeat GET', async (context) => {
  let reads = 0
  const snapshot = { ...instrumentStatus(), calibration: calibration() }
  const props = reactive({
    status: structuredClone(snapshot),
    disabled: false,
    command: async () => null,
    loadPreview: async () => {
      reads++
      return reads === 1
        ? preview()
        : new Promise<CalibrationPreviewResponse>(() => {})
    },
  })
  const harness = await sfcHarness(
    'src/components/softtools/CalibrationAcquisition.vue',
    props,
    {},
  )
  context.after(harness.dispose)
  for (let index = 0; index < 4; index++) await Promise.resolve()
  await nextTick()
  const saved = harness.bindings.preview as Ref<CalibrationPreview | null>
  assert.equal(saved.value?.runId, '3')
  assert.equal(reads, 1)
  context.mock.timers.enable({ apis: ['setTimeout'] })
  for (let poll = 1; poll <= 5; poll++) {
    setTimeout(() => {
      props.status = structuredClone(snapshot)
    }, 200)
    context.mock.timers.tick(200)
    await nextTick()
    assert.ok(
      saved.value,
      `unchanged poll ${poll} must not clear the saved standard`,
    )
    assert.equal(
      reads,
      1,
      `unchanged poll ${poll} must not request the saved standard again`,
    )
    assert.equal((harness.bindings.previewLoading as Ref<boolean>).value, false)
  }
})

test('unchanged polling does not repeat immediate preview reads or supersede an in-flight saved standard read', async (context) => {
  const loaders: { resolve: (value: CalibrationPreviewResponse) => void }[] = []
  const snapshot = { ...instrumentStatus(), calibration: calibration() }
  const props = reactive({
    status: structuredClone(snapshot),
    disabled: false,
    command: async () => null,
    loadPreview: async () =>
      new Promise<CalibrationPreviewResponse>((resolve) => {
        loaders.push({ resolve })
      }),
  })
  const harness = await sfcHarness(
    'src/components/softtools/CalibrationAcquisition.vue',
    props,
    {},
  )
  context.after(harness.dispose)
  for (let poll = 0; poll < 3; poll++) {
    props.status = structuredClone(snapshot)
    await nextTick()
  }
  assert.equal(
    loaders.length,
    1,
    'same capture identity must keep the original pending GET',
  )
  loaders[0]!.resolve(preview())
  for (let index = 0; index < 4; index++) await Promise.resolve()
  await nextTick()
  assert.equal(
    (harness.bindings.preview as Ref<CalibrationPreview | null>).value?.runId,
    '3',
  )
  let immediateReads = 0
  props.loadPreview = async () => {
    immediateReads++
    return preview()
  }
  await (harness.bindings.refreshPreview as () => Promise<void>)()
  for (let poll = 0; poll < 3; poll++) {
    props.status = structuredClone(snapshot)
    for (let index = 0; index < 4; index++) await Promise.resolve()
    await nextTick()
  }
  assert.equal(
    immediateReads,
    1,
    'a fast server must not hide repeated unnecessary GETs',
  )
})

test('a changed captured run or revision reloads once and rejects the superseded response', async (context) => {
  const loaders: { resolve: (value: CalibrationPreviewResponse) => void }[] = []
  const props = reactive({
    status: { ...instrumentStatus(), calibration: calibration() },
    disabled: false,
    command: async () => null,
    loadPreview: async () =>
      new Promise<CalibrationPreviewResponse>((resolve) => {
        loaders.push({ resolve })
      }),
  })
  const harness = await sfcHarness(
    'src/components/softtools/CalibrationAcquisition.vue',
    props,
    {},
  )
  context.after(harness.dispose)
  const changed = structuredClone({
    ...instrumentStatus(),
    calibration: calibration(),
  })
  changed.calibration.revision = '6'
  changed.calibration.steps[0]!.runId = '7'
  changed.calibration.steps[0]!.frameSeq = '2'
  props.status = changed
  await nextTick()
  assert.equal(loaders.length, 2)
  loaders[0]!.resolve(preview())
  for (let index = 0; index < 4; index++) await Promise.resolve()
  await nextTick()
  assert.equal(
    (harness.bindings.preview as Ref<CalibrationPreview | null>).value,
    null,
  )
  loaders[1]!.resolve({
    ...preview(),
    calibrationRevision: '6',
    runId: '7',
    frameSeq: '2',
  })
  for (let index = 0; index < 4; index++) await Promise.resolve()
  await nextTick()
  assert.equal(
    (harness.bindings.preview as Ref<CalibrationPreview | null>).value?.runId,
    '7',
  )
  props.status = structuredClone(changed)
  await nextTick()
  assert.equal(loaders.length, 2)
})

for (const end of ['collecting', 'session'] as const) {
  test(`a ${end} change clears the saved standard even when the capture revision is unchanged`, async (context) => {
    let reads = 0
    const props = reactive({
      status: { ...instrumentStatus(), calibration: calibration() },
      disabled: false,
      command: async () => null,
      loadPreview: async () => {
        reads++
        return preview()
      },
    })
    const harness = await sfcHarness(
      'src/components/softtools/CalibrationAcquisition.vue',
      props,
      {},
    )
    context.after(harness.dispose)
    for (let index = 0; index < 4; index++) await Promise.resolve()
    await nextTick()
    assert.ok(
      (harness.bindings.preview as Ref<CalibrationPreview | null>).value,
    )
    if (end === 'collecting') props.status.calibration.collecting = false
    else props.status.calibration.sessionId = '9'
    await nextTick()
    assert.equal(
      (harness.bindings.preview as Ref<CalibrationPreview | null>).value,
      null,
    )
    assert.equal(reads, 1)
  })
}

test('preview diagrams do not schedule canvas work for fresh equal configs and use saved-standard axes', async (context) => {
  let scheduled = 0
  const props = reactive({
    format: 'db' as 'db' | 'phase',
    preview: preview() as CalibrationPreview | null,
    parameter: 'S22',
    config: instrumentStatus().config,
    calibrationLabel: 'OSM P2',
    emptyText: '',
  })
  const harness = await sfcHarness(
    'src/components/softtools/CalibrationPreviewDiagram.vue',
    props,
    {
      cancelAnimationFrame() {},
      requestAnimationFrame() {
        return ++scheduled
      },
    },
  )
  context.after(harness.dispose)
  for (let poll = 0; poll < 5; poll++) {
    props.config = structuredClone(instrumentStatus().config)
    await nextTick()
    await nextTick()
  }
  assert.equal(
    scheduled,
    0,
    'equal status snapshots must not redraw the saved standard',
  )
  props.config.startHz = 20e6
  await nextTick()
  await nextTick()
  assert.equal(
    scheduled,
    0,
    'DUT config does not change the saved standard frequency axis',
  )
  props.format = 'phase'
  await nextTick()
  await nextTick()
  assert.equal(scheduled, 1)
  props.preview = null
  await nextTick()
  await nextTick()
  assert.equal(scheduled, 2)
  props.config.startHz = 30e6
  await nextTick()
  await nextTick()
  assert.equal(
    scheduled,
    3,
    'an uncaptured empty diagram follows its fallback frequency axis',
  )
})

test('a completed or cancelled server session closes acquisition without reopening setup after its component is removed', async (context) => {
  const harness = await sfcHarness(
    'src/App.vue',
    {},
    {
      fetch: async () => new Response('{}'),
      setTimeout,
      clearTimeout,
    },
  )
  context.after(harness.dispose)
  const status = harness.bindings.status as Ref<InstrumentStatus | null>
  const open = harness.bindings.calibrationWorkflowOpen as Ref<boolean>
  status.value = { ...instrumentStatus(), calibration: calibration() }
  open.value = true
  await nextTick()
  assert.ok(findComponent(harness.render(), 'CalibrationAcquisition'))
  assert.equal(
    findComponent(harness.render(), 'CalibrationWorkflow'),
    undefined,
  )
  status.value = {
    ...status.value,
    calibration: {
      ...calibration(),
      collecting: false,
      sessionId: '',
      steps: [],
    },
  }
  await nextTick()
  assert.equal(open.value, false)
  assert.equal(
    findComponent(harness.render(), 'CalibrationAcquisition'),
    undefined,
  )
  assert.equal(
    findComponent(harness.render(), 'CalibrationWorkflow'),
    undefined,
  )
})

test('saved zero or invalid standards explain an empty plot using backend projection states', async (context) => {
  const sample = preview()
  sample.valid = [true, true, true]
  sample.real = [0, 0, 0]
  sample.imag = [0, 0, 0]
  sample.dbValues = [null, null, null]
  sample.dbStates = ['negativeInfinity', 'negativeInfinity', 'negativeInfinity']
  sample.phaseValues = [null, null, null]
  sample.phaseStates = ['undefined', 'undefined', 'undefined']
  const props = reactive({
    format: 'db' as 'db' | 'phase',
    preview: sample,
    parameter: 'S22',
    config: instrumentStatus().config,
    calibrationLabel: 'OSM P2',
    emptyText: '',
  })
  const harness = await sfcHarness(
    'src/components/softtools/CalibrationPreviewDiagram.vue',
    props,
    {
      cancelAnimationFrame() {},
      requestAnimationFrame() {
        return 0
      },
    },
  )
  context.after(harness.dispose)
  const message = harness.bindings.noDrawableMessage as Ref<string>
  assert.match(message.value, /−∞/)
  props.format = 'phase'
  await nextTick()
  assert.match(message.value, /相位未定义/)
  props.preview.valid = [false, false, false]
  await nextTick()
  assert.match(message.value, /没有有效样本/)
})

test('a read-only observer can retry failed saved preview without recapturing a standard', async (context) => {
  let reads = 0
  let writes = 0
  const props = reactive({
    status: { ...instrumentStatus(), calibration: calibration() },
    disabled: true,
    canCancel: false,
    command: async () => {
      writes++
      return null
    },
    loadPreview: async () => {
      if (++reads === 1) throw new Error('Temporary read failure')
      return preview()
    },
  })
  const harness = await sfcHarness(
    'src/components/softtools/CalibrationAcquisition.vue',
    props,
    {},
  )
  context.after(harness.dispose)
  for (let index = 0; index < 4; index++) await Promise.resolve()
  await nextTick()
  assert.equal(
    (harness.bindings.preview as Ref<CalibrationPreview | null>).value,
    null,
  )
  const retry = findElements(
    harness.render(),
    (node) =>
      node.type === 'button' && node.props?.class === 'cal-preview-retry',
  )[0]!
  assert.ok(retry)
  await (retry.props!.onClick as () => Promise<void>)()
  assert.equal(
    (harness.bindings.preview as Ref<CalibrationPreview | null>).value?.stepId,
    'open-2',
  )
  assert.equal(reads, 2)
  assert.equal(writes, 0)
})
