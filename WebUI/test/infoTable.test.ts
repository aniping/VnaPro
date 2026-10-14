import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive, type Ref, type VNode } from 'vue'
import { decodeDisplay } from '../src/api/displayDecoders'
import { useWorkspace } from '../src/composables/useWorkspace'
import { MARKER_INFO_AREAS } from '../src/domain/markerInfoArea'
import {
  MARKER_INFO_DRAG_TYPE,
  type MarkerReadoutPreview,
} from '../src/domain/markerReadouts'
import { traceTitleLayout } from '../src/plot/titleLayout'
import {
  acquiredFrame,
  displaySnapshot,
  instrumentStatus,
  markerProjection,
  presentation,
  receiverTrace,
  reflection,
  transmission,
} from './displayFixtures'
import { findComponent, findElements, sfcHarness } from './sfcHarness'

function textContent(value: unknown): string {
  if (typeof value === 'string') return value
  if (Array.isArray(value)) return value.map(textContent).join('')
  if (value && typeof value === 'object')
    return textContent(Reflect.get(value, 'children'))
  return ''
}
function component(node: VNode, name: string) {
  return (
    typeof node.type === 'object' && Reflect.get(node.type, 'name') === name
  )
}
function measured(name: string, frequencyHz: number, value: number) {
  const trace = receiverTrace({ ...transmission, name })
  trace.projection.marker = markerProjection({
    available: true,
    valid: true,
    reason: '',
    sampleIndex: 0,
    frequencyHz,
    real: 0.2,
    imag: 0.1,
    value,
    valueState: 'finite',
  })
  const view = presentation()
  view.marker = { enabled: true, frequencyHz, sampleIndex: 0, discrete: true }
  view.markerInfoLocation = 'table'
  return { trace, view }
}

test('display decoders preserve backend annotation settings and reject partial or invalid applied state', () => {
  const display = displaySnapshot([reflection])
  display.infoTable = { visible: true, position: 'left' }
  display.annotations = {
    activeTraceOnly: true,
    stimulusInfoVisible: false,
    fontScale: 120.5,
  }
  display.diagrams[0]!.markerInfoVisible = false
  display.presentations.Reflect!.markerInfoLocation = 'table'
  const decoded = decodeDisplay(display, [reflection])
  assert.deepEqual(decoded.infoTable, display.infoTable)
  assert.deepEqual(decoded.annotations, display.annotations)
  assert.equal(decoded.diagrams[0]!.markerInfoVisible, false)
  assert.equal(decoded.presentations.Reflect!.markerInfoLocation, 'table')
  for (const value of [79.9, 170.1, Infinity]) {
    assert.throws(
      () =>
        decodeDisplay(
          {
            ...display,
            annotations: { ...display.annotations, fontScale: value },
          },
          [reflection],
        ),
      /fontScale/,
    )
  }
  assert.throws(
    () =>
      decodeDisplay({ ...display, infoTable: { visible: true } }, [reflection]),
    /position/,
  )
  assert.throws(
    () =>
      decodeDisplay({ ...display, annotations: { activeTraceOnly: false } }, [
        reflection,
      ]),
    /stimulusInfoVisible/,
  )
  assert.throws(
    () =>
      decodeDisplay(
        { ...display, infoTable: { visible: true, position: 'center' } },
        [reflection],
      ),
    /position/,
  )
})

test('annotation command adapters dispatch one atomic backend patch without changing the applied snapshot', () => {
  const status = reactive(instrumentStatus([reflection]))
  const sent: unknown[] = []
  const workspace = useWorkspace(
    () => status,
    () => null,
    (command) => sent.push(command),
  )
  const before = JSON.stringify(status.display)
  workspace.setInfoTable({ enabled: true, position: 'right' })
  workspace.setAnnotations({ activeTraceOnly: true, fontScale: 120.5 })
  workspace.setMarkerInfo(false, 1)
  workspace.setMarkerInfoLocation('Reflect', 'table')
  assert.deepEqual(sent, [
    { action: 'infoTable', enabled: true, position: 'right' },
    { action: 'displayAnnotations', activeTraceOnly: true, fontScale: 120.5 },
    { action: 'markerInfo', enabled: false, diagram: 1 },
    { action: 'markerInfoLocation', traceName: 'Reflect', location: 'table' },
  ])
  assert.equal(JSON.stringify(status.display), before)
})

test('marker info decoder preserves all nine backend areas and defaults only a missing optional field', () => {
  const display = displaySnapshot([reflection])
  delete display.presentations.Reflect!.markerInfoArea
  assert.equal(
    decodeDisplay(display, [reflection]).presentations.Reflect!.markerInfoArea,
    'topRight',
  )
  for (const area of MARKER_INFO_AREAS) {
    display.presentations.Reflect!.markerInfoArea = area
    assert.equal(
      decodeDisplay(display, [reflection]).presentations.Reflect!.markerInfoArea,
      area,
    )
  }
  const before = JSON.stringify(display)
  for (const invalid of [null, 'TOP_RIGHT', 'middleTop', 9, {}]) {
    assert.throws(() => decodeDisplay({
      ...display,
      presentations: {
        ...display.presentations,
        Reflect: { ...display.presentations.Reflect, markerInfoArea: invalid },
      },
    }, [reflection]), /markerInfoArea/)
  }
  assert.equal(JSON.stringify(display), before)
})

test('marker info relocation sends one atomic location and area intent without writing the applied snapshot', async () => {
  const status = reactive(instrumentStatus([reflection]))
  status.display.presentations.Reflect!.markerInfoArea = 'bottomLeft'
  const sent: unknown[] = []
  const workspace = useWorkspace(
    () => status,
    () => null,
    (command) => {
      sent.push(command)
      return Promise.resolve(false)
    },
  )
  const before = JSON.stringify(status.display)
  for (const area of MARKER_INFO_AREAS) {
    const result = workspace.setMarkerInfoLocation('Reflect', 'diagram', area)
    assert.equal(JSON.stringify(status.display), before)
    assert.equal(await result, false)
    assert.equal(JSON.stringify(status.display), before)
  }
  await workspace.setMarkerInfoLocation('Reflect', 'table')
  await workspace.setMarkerInfoLocation('Reflect', 'diagram')
  assert.deepEqual(sent, [
    ...MARKER_INFO_AREAS.map((area) => ({
      action: 'markerInfoLocation', traceName: 'Reflect', location: 'diagram', area,
    })),
    { action: 'markerInfoLocation', traceName: 'Reflect', location: 'table' },
    { action: 'markerInfoLocation', traceName: 'Reflect', location: 'diagram' },
  ])
  assert.equal(JSON.stringify(status.display), before)
})

test('one global Info Table keeps fields from maximized-away diagrams and updates only a transient dragged readout', async () => {
  const a = measured('Trc1', 10e6, -12.345)
  const b = measured('Trc2', 20e6, -77.891)
  const frame = acquiredFrame(a.trace)
  frame.traces.push(b.trace)
  const props = reactive({
    diagrams: [
      {
        number: 1,
        measurementNames: ['Trc1'],
        primaryMeasurement: 'Trc1',
        markerInfoVisible: true,
      },
      {
        number: 2,
        measurementNames: ['Trc2'],
        primaryMeasurement: 'Trc2',
        markerInfoVisible: true,
      },
    ],
    frame,
    measurements: [a.trace, b.trace],
    presentations: { Trc1: a.view, Trc2: b.view },
    activeTraceName: 'Trc1',
    selectedDiagram: 1,
    layout: 'maximized',
    gridColumns: 2,
    gridRows: 1,
    config: { startHz: 10e6, stopHz: 8e9, points: 201, ifBandwidthHz: 1000 },
    frameCondition: { kind: 'current', label: '', detail: '' },
    canEditDisplay: true,
    commitMarker: async () => true,
    infoTable: { visible: true, position: 'bottom' },
    annotations: {
      activeTraceOnly: false,
      stimulusInfoVisible: false,
      fontScale: 100,
    },
  })
  const display = await sfcHarness(
    'src/components/diagrams/DiagramWorkspace.vue',
    props,
    {},
  )
  try {
    const root = display.render()
    assert.equal(
      findElements(root, (node) => component(node, 'DiagramPane')).length,
      1,
    )
    const table = findComponent(root, 'InfoTable')!
    assert.deepEqual(
      table.props!.fields.map((field: { name: string }) => field.name),
      ['Trc1', 'Trc2'],
    )
    assert.equal(table.props!.fields[1].markers[0].result.value, -77.891)
    const preview = display.bindings
      .markerPreview as Ref<MarkerReadoutPreview | null>
    preview.value = {
      name: 'Trc2',
      markerId: 1,
      result: {
        ...table.props!.fields[1].markers[0].result,
        frequencyHz: 30e6,
        value: -99,
      },
    }
    assert.equal(
      findComponent(display.render(), 'InfoTable')!.props!.fields[1].markers[0]
        .result.value,
      -99,
    )
    assert.equal(props.frame.traces[1]!.projection.marker.value, -77.891)
    preview.value = null
    assert.equal(
      findComponent(display.render(), 'InfoTable')!.props!.fields[1].markers[0]
        .result.value,
      -77.891,
    )
    props.diagrams[1]!.markerInfoVisible = false
    assert.deepEqual(
      findComponent(display.render(), 'InfoTable')!.props!.fields.map(
        (field: { name: string }) => field.name,
      ),
      ['Trc1'],
    )
    assert.equal(props.presentations.Trc2.markerInfoLocation, 'table')
    props.diagrams[1]!.markerInfoVisible = true
    props.annotations.activeTraceOnly = true
    assert.equal(
      findComponent(display.render(), 'InfoTable')!.props!.fields.length,
      1,
    )
    props.infoTable.visible = false
    assert.equal(findComponent(display.render(), 'InfoTable'), undefined)
    assert.equal(props.presentations.Trc2.markerInfoLocation, 'table')
  } finally {
    display.dispose()
  }
})

test('Info Table renders original-style fields without invented headers and accepts one trace field drag', async () => {
  const a = measured('Trc12', 123e6, -21)
  const workspace = await sfcHarness(
    'src/components/diagrams/DiagramWorkspace.vue',
    reactive({
      diagrams: [
        { number: 1, measurementNames: ['Trc12'], primaryMeasurement: 'Trc12' },
      ],
      frame: acquiredFrame(a.trace),
      measurements: [a.trace],
      presentations: { Trc12: a.view },
      activeTraceName: 'Trc12',
      layout: 'single',
      gridColumns: 1,
      gridRows: 1,
      config: { startHz: 10e6, stopHz: 8e9, points: 201, ifBandwidthHz: 1000 },
      frameCondition: { kind: 'current', label: '', detail: '' },
      canEditDisplay: true,
      commitMarker: async () => true,
      infoTable: { visible: true, position: 'right' },
    }),
    {},
  )
  const fields = (workspace.bindings.infoFields as Ref<unknown[]>).value
  const props = reactive({
    fields,
    position: 'right',
    annotations: {
      activeTraceOnly: false,
      stimulusInfoVisible: true,
      fontScale: 100,
    },
    canEditDisplay: true,
  })
  const table = await sfcHarness(
    'src/components/diagrams/InfoTable.vue',
    props,
    {},
  )
  try {
    const root = table.render()
    assert.equal(findElements(root, (node) => node.type === 'th').length, 0)
    const field = findComponent(root, 'MarkerReadout')!
    assert.equal(field.props!.inline, true)
    assert.equal(field.props!['show-trace-names'], true)
    assert.equal(field.props!.markers[0].name, 'Trc12')
    const data = {
      types: [MARKER_INFO_DRAG_TYPE],
      getData: () => 'Trc12',
      dropEffect: '',
    }
    root.props!.onDrop({ dataTransfer: data, preventDefault() {} })
    assert.deepEqual(JSON.parse(JSON.stringify(table.emitted)), [
      ['relocate', { name: 'Trc12', location: 'table' }],
    ])
    assert.equal(a.view.markerInfoLocation, 'table')
    props.canEditDisplay = false
    table.render().props!.onDrop({ dataTransfer: data, preventDefault() {} })
    assert.equal(table.emitted.length, 1)
  } finally {
    table.dispose()
    workspace.dispose()
  }
})

test('Display controls respect backend flags, move a field with one action and keep font edits as a draft', async () => {
  const status = instrumentStatus([reflection])
  status.display.infoTable = { visible: false, position: 'bottom' }
  status.display.annotations = {
    activeTraceOnly: false,
    stimulusInfoVisible: true,
    fontScale: 100,
  }
  const props = reactive({ status, canEditDisplay: true })
  const tool = await sfcHarness(
    'src/components/softtools/DisplaySofttool.vue',
    props,
    {},
  )
  try {
    const button = (label: string) =>
      findElements(
        tool.render(),
        (node) => node.type === 'button' && textContent(node).trim() === label,
      )[0]!
    assert.equal(button('In Info Table').props!.disabled, true)
    button('Show Table Off').props!.onClick()
    assert.equal(props.status.display.infoTable!.visible, false)
    props.status.display.infoTable!.visible = true
    button('In Info Table').props!.onClick()
    assert.equal(
      props.status.display.presentations.Reflect!.markerInfoLocation,
      'diagram',
    )
    ;(tool.bindings.fontScale as Ref<number>).value = 120.5
    assert.equal(props.status.display.annotations!.fontScale, 100)
    ;(tool.bindings.applyFont as () => void)()
    assert.deepEqual(JSON.parse(JSON.stringify(tool.emitted)), [
      ['infoTable', { enabled: true }],
      ['markerInfoLocation', { name: 'Reflect', location: 'table' }],
      ['annotations', { fontScale: 120.5 }],
    ])
    button('Overlay All').props!.onClick()
    assert.deepEqual(tool.emitted.at(-1), ['overlay'])
    props.canEditDisplay = false
    assert.ok(
      findElements(tool.render(), (node) => node.type === 'button').every(
        (node) => node.props!.disabled,
      ),
    )
  } finally {
    tool.dispose()
  }
})

test('trace title layout follows intrinsic width rather than a fixed three or four column rule', () => {
  assert.deepEqual(traceTitleLayout(848, 205), { columns: 4, compact: false })
  assert.deepEqual(traceTitleLayout(848, 260), { columns: 3, compact: false })
  assert.deepEqual(traceTitleLayout(420, 205), { columns: 2, compact: false })
  assert.deepEqual(traceTitleLayout(160, 205), { columns: 1, compact: true })
  assert.deepEqual(traceTitleLayout(848, 205 * 1.7), {
    columns: 2,
    compact: false,
  })
})

test('Marker info font and stimulus settings affect field rows while keeping local numbers and backend values', async () => {
  const trace = measured('Trc128', 4.005e9, -28.1234)
  const field = {
    name: 'Trc128',
    markerId: 10,
    color: '#ffff26',
    format: 'db',
    measurement: trace.trace,
    result: {
      index: 0,
      frequencyHz: 4.005e9,
      value: -28.1234,
      secondaryValue: NaN,
      real: 0.2,
      imaginary: 0.1,
      valid: true,
      impedanceReal: null,
      impedanceImag: null,
      impedanceState: 'invalid',
      interpolated: false,
      reason: '',
    },
    active: true,
  }
  const props = reactive({
    markers: [field],
    inline: true,
    showTraceNames: true,
    canEditDisplay: true,
    location: 'table',
    infoTableVisible: true,
    stimulusInfoVisible: false,
    fontScale: 170,
  })
  const readout = await sfcHarness(
    'src/components/diagrams/MarkerReadout.vue',
    props,
    {},
  )
  try {
    const root = readout.render()
    assert.equal(
      findElements(root, (node) =>
        String(node.props?.class).includes('marker-frequency'),
      ).length,
      0,
    )
    assert.ok(textContent(root).includes('M10Trc128'))
    assert.equal(root.props!.style['--marker-font-size'], '18.7px')
    assert.equal(root.props!.style['--marker-frequency-width'], '144.5px')
    let dragged = ''
    root.props!.onDragstart({
      dataTransfer: {
        setData(type: string, value: string) {
          assert.equal(type, MARKER_INFO_DRAG_TYPE)
          dragged = value
        },
        effectAllowed: '',
      },
      preventDefault() {},
    })
    assert.equal(dragged, 'Trc128')
    assert.equal(field.result.value, -28.1234)
    props.stimulusInfoVisible = true
    assert.equal(
      findElements(readout.render(), (node) =>
        String(node.props?.class).includes('marker-frequency'),
      ).length,
      1,
    )
  } finally {
    readout.dispose()
  }
})

test('per-window marker info hides only field readouts and table placement leaves graph glyphs usable', async () => {
  const a = measured('Trc1', 10e6, -12.345)
  const props = reactive({
    number: 1,
    layout: 'single',
    frame: acquiredFrame(a.trace),
    measurements: [a.trace],
    allMeasurements: [a.trace],
    presentations: { Trc1: a.view },
    activeTraceName: 'Trc1',
    primaryTraceName: 'Trc1',
    selectedDiagram: 1,
    config: { startHz: 10e6, stopHz: 8e9, points: 201, ifBandwidthHz: 1000 },
    frameCondition: { kind: 'current', label: '', detail: '' },
    canEditDisplay: true,
    commitMarker: async () => true,
    markerInfoVisible: true,
    annotations: {
      activeTraceOnly: false,
      stimulusInfoVisible: false,
      fontScale: 100,
    },
    infoTableVisible: true,
  })
  const pane = await sfcHarness(
    'src/components/diagrams/DiagramPane.vue',
    props,
    {},
  )
  try {
    assert.equal(findComponent(pane.render(), 'MarkerReadout'), undefined)
    const handles = () =>
      findElements(
        pane.render(),
        (node) =>
          node.type === 'button' &&
          String(node.props?.class).includes('marker-handle'),
      )
    assert.equal(handles().length, 1)
    props.presentations.Trc1.markerInfoLocation = 'diagram'
    assert.ok(findComponent(pane.render(), 'MarkerReadout'))
    assert.ok(textContent(pane.render()).includes('Start'))
    props.markerInfoVisible = false
    assert.equal(findComponent(pane.render(), 'MarkerReadout'), undefined)
    assert.equal(handles().length, 1)
    assert.ok(textContent(pane.render()).includes('Stop'))
    const root = pane.render()
    root.props!.onDrop({
      dataTransfer: { getData: () => 'Trc1' },
      preventDefault() {},
    })
    assert.deepEqual(JSON.parse(JSON.stringify(pane.emitted)), [
      ['relocateMarkerInfo', { name: 'Trc1', location: 'diagram' }],
    ])
    assert.equal(props.markerInfoVisible, false)
  } finally {
    pane.dispose()
  }
})

test('changing the active Smith axis remeasures current title text without resizing or measuring ordinary CONT frames', async () => {
  const a = measured('Trc1', 10e6, -12)
  const b = measured('Trc2', 20e6, -24)
  a.view.format = 'smith'
  b.view.format = 'smith'
  a.view.smithRadius = 0.01
  b.view.smithRadius = 10000
  const props = reactive({
    number: 1,
    layout: 'single',
    frame: acquiredFrame(a.trace),
    measurements: [a.trace, b.trace],
    allMeasurements: [a.trace, b.trace],
    presentations: { Trc1: a.view, Trc2: b.view },
    activeTraceName: 'Trc1',
    primaryTraceName: 'Trc1',
    selectedDiagram: 1,
    config: { startHz: 10e6, stopHz: 8e9, points: 201, ifBandwidthHz: 1000 },
    frameCondition: { kind: 'current', label: '', detail: '' },
    canEditDisplay: true,
    commitMarker: async () => true,
  })
  const pane = await sfcHarness(
    'src/components/diagrams/DiagramPane.vue',
    props,
    {
      window: {
        getComputedStyle: () => ({
          font: '11px Tahoma',
          paddingLeft: '0px',
          paddingRight: '0px',
        }),
      },
      requestAnimationFrame: () => 1,
      cancelAnimationFrame() {},
    },
  )
  // 本夹具只验证真实 watch 的测量调度；字体宽度与四列效果由浏览器验收。
  const reference = { className: 'trace-reference', textContent: 'Ref short' }
  const tokens: string[] = []
  Reflect.set(pane.bindings, 'titleMeasure', {
    font: '',
    measureText(text: string) {
      tokens.push(text)
      return { width: text.length * 6 }
    },
  })
  ;(pane.bindings.traceList as Ref<unknown>).value = {
    clientWidth: 848,
    querySelectorAll: () => [
      {
        children: [
          { className: 'trace-number', textContent: 'Trc1' },
          { className: 'measurement-chip', textContent: 'S21' },
          reference,
        ],
      },
    ],
  }
  try {
    ;(pane.bindings.measureTitles as () => void)()
    const initialCount = tokens.length
    reference.textContent = 'Ref much longer'
    props.primaryTraceName = 'Trc2'
    props.activeTraceName = 'Trc2'
    await nextTick()
    await nextTick()
    assert.ok(
      tokens.length > initialCount,
      'Changing the primary Smith axis must remeasure unchanged-size title boxes',
    )
    assert.equal(tokens.at(-1), 'Ref much longer')
    const afterSelection = tokens.length
    props.frame.frameSeq = '2'
    await nextTick()
    await nextTick()
    assert.equal(
      tokens.length,
      afterSelection,
      'Ordinary CONT frames must not remeasure title geometry',
    )
  } finally {
    pane.dispose()
  }
})

test('diagram marker fields consume each backend anchor and stack fields sharing the same anchor', async () => {
  const items = MARKER_INFO_AREAS.map((area, index) => {
    const value = measured(`Trc${index + 1}`, 10e6, -12 - index)
    value.view.markerInfoLocation = 'diagram'
    value.view.markerInfoArea = area
    return value
  })
  const extra = measured('Trc10', 10e6, -45)
  extra.view.markerInfoLocation = 'diagram'
  extra.view.markerInfoArea = 'center'
  items.push(extra)
  const frame = acquiredFrame(items[0]!.trace)
  frame.traces.push(...items.slice(1).map((item) => item.trace))
  const props = reactive({
    number: 1,
    layout: 'single',
    frame,
    measurements: items.map((item) => item.trace),
    allMeasurements: items.map((item) => item.trace),
    presentations: Object.fromEntries(items.map((item) => [item.trace.name, item.view])),
    activeTraceName: 'Trc1',
    primaryTraceName: 'Trc1',
    config: { startHz: 10e6, stopHz: 8e9, points: 201, ifBandwidthHz: 1000 },
    frameCondition: { kind: 'current', label: '', detail: '' },
    canEditDisplay: true,
    commitMarker: async () => true,
    markerInfoVisible: true,
    annotations: { activeTraceOnly: false, stimulusInfoVisible: true, fontScale: 100 },
  })
  const pane = await sfcHarness('src/components/diagrams/DiagramPane.vue', props, {})
  const groups = () => findElements(pane.render(), (node) =>
    !!node.props?.['data-marker-info-area'],
  )
  try {
    assert.deepEqual(groups().map((node) => node.props!['data-marker-info-area']), MARKER_INFO_AREAS)
    const center = groups().find((node) => node.props!['data-marker-info-area'] === 'center')!
    const centerFields = findElements(center, (node) => component(node, 'MarkerReadout'))
    assert.deepEqual(centerFields.map((field) => field.props!.markers[0].name), ['Trc5', 'Trc10'])
    const { left, top, transform } = center.props!.style
    assert.deepEqual({ left, top, transform }, {
      left: '50%', top: '50%',
      transform: 'translate(-50%, -50%)',
    })
    const bottomRight = groups().find((node) => node.props!['data-marker-info-area'] === 'bottomRight')!
    assert.equal(bottomRight.props!.style.right, '2px')
    assert.equal(bottomRight.props!.style.bottom, '2px')
    props.presentations.Trc1!.markerInfoArea = 'bottomRight'
    assert.equal(groups().some((node) => node.props!['data-marker-info-area'] === 'topLeft'), false)
    assert.equal(findElements(groups().find((node) => node.props!['data-marker-info-area'] === 'bottomRight')!,
      (node) => component(node, 'MarkerReadout')).length, 2)
    props.annotations.activeTraceOnly = true
    assert.deepEqual(groups().map((node) => node.props!['data-marker-info-area']), ['bottomRight'])
    props.markerInfoVisible = false
    assert.equal(groups().length, 0)
    assert.equal(frame.traces[0]!.projection.marker.frequencyHz, 10e6)
    assert.deepEqual(pane.emitted, [])
  } finally { pane.dispose() }
})

test('actual diagram drop forwards the nine marker info areas without optimistically changing backend placement', async () => {
  const a = measured('Trc1', 10e6, -12)
  const props = reactive({
    number: 1, layout: 'single', frame: acquiredFrame(a.trace),
    measurements: [a.trace], allMeasurements: [a.trace],
    presentations: { Trc1: a.view }, activeTraceName: 'Trc1', primaryTraceName: 'Trc1',
    config: { startHz: 10e6, stopHz: 8e9, points: 201, ifBandwidthHz: 1000 },
    frameCondition: { kind: 'current', label: '', detail: '' },
    canEditDisplay: true, commitMarker: async () => true,
  })
  const pane = await sfcHarness('src/components/diagrams/DiagramPane.vue', props, {})
  const bounds = { left: 50, top: 80, width: 900, height: 300 }
  ;(pane.bindings.plotArea as Ref<unknown>).value = { getBoundingClientRect: () => bounds }
  let prevented = 0
  function drop(name: string, index: number) {
    pane.render().props!.onDrop({
      clientX: bounds.left + ((index % 3) + 0.5) * bounds.width / 3,
      clientY: bounds.top + (Math.floor(index / 3) + 0.5) * bounds.height / 3,
      dataTransfer: {
        types: [MARKER_INFO_DRAG_TYPE],
        getData: (type: string) => type === MARKER_INFO_DRAG_TYPE ? name : '',
      },
      preventDefault() { prevented += 1 },
    })
  }
  const before = JSON.stringify(props.presentations)
  try {
    for (let index = 0; index < MARKER_INFO_AREAS.length; index++) drop('Trc1', index)
    assert.deepEqual(JSON.parse(JSON.stringify(pane.emitted)), MARKER_INFO_AREAS.map((area) => [
      'relocateMarkerInfo', { name: 'Trc1', location: 'diagram', area },
    ]))
    assert.equal(prevented, 9)
    assert.equal(JSON.stringify(props.presentations), before)
    bounds.width = 0
    drop('Trc1', 0)
    bounds.width = 900
    drop('Trc1', NaN)
    assert.equal(pane.emitted.length, 9, 'Invalid drop geometry cannot silently migrate a table field')
    drop('AnotherDiagramTrace', 0)
    props.canEditDisplay = false
    drop('Trc1', 0)
    assert.equal(pane.emitted.length, 9)
    assert.equal(prevented, 9)
    assert.equal(JSON.stringify(props.presentations), before)
  } finally { pane.dispose() }
})
