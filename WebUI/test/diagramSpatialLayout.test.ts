import assert from 'node:assert/strict'
import test from 'node:test'
import { reactive } from 'vue'
import { decodeDisplay } from '../src/api/displayDecoders'
import {
  acquiredFrame,
  displaySnapshot,
  reflection,
  transmission,
} from './displayFixtures'
import { findElements, sfcHarness } from './sfcHarness'

function spatialSnapshot() {
  const display = displaySnapshot([reflection, transmission])
  display.layout = 'grid'
  display.gridColumns = 2
  display.diagrams = [
    {
      number: 1,
      measurementNames: [reflection.name],
      primaryMeasurement: reflection.name,
      region: { left: 0, top: 0, width: 0.25, height: 1 },
    },
    {
      number: 2,
      measurementNames: [transmission.name],
      primaryMeasurement: transmission.name,
      region: { left: 0.25, top: 0, width: 0.75, height: 1 },
    },
  ]
  return display
}

test('decoder retains backend spatial regions and rejects incomplete or out-of-bounds rectangles', () => {
  const display = spatialSnapshot()
  const before = JSON.stringify(display)
  assert.deepEqual(
    decodeDisplay(display, [reflection, transmission]).diagrams.map(
      (diagram) => diagram.region,
    ),
    display.diagrams.map((diagram) => diagram.region),
  )
  for (const region of [
    { left: 0, top: 0, width: 0.5 },
    { left: 0, top: 0, width: 0, height: 1 },
    { left: -0.01, top: 0, width: 0.5, height: 1 },
    { left: 0.75, top: 0, width: 0.5, height: 1 },
    { left: 0, top: 0.75, width: 1, height: 0.5 },
    { left: 0, top: 0, width: NaN, height: 1 },
    { left: 0, top: 0, width: 1, height: Infinity },
    null,
  ]) {
    assert.throws(
      () =>
        decodeDisplay(
          {
            ...display,
            diagrams: [{ ...display.diagrams[0], region }, display.diagrams[1]],
          },
          [reflection, transmission],
        ),
      /region/,
    )
  }
  assert.throws(
    () =>
      decodeDisplay(
        {
          ...display,
          diagrams: [
            display.diagrams[0],
            { ...display.diagrams[1], region: undefined },
          ],
        },
        [reflection, transmission],
      ),
    /region.incomplete/,
  )
  const legacy = decodeDisplay(
    {
      ...display,
      diagrams: display.diagrams.map((diagram) => ({
        ...diagram,
        region: undefined,
      })),
    },
    [reflection, transmission],
  )
  assert.ok(legacy.diagrams.every((diagram) => diagram.region === undefined))
  assert.equal(JSON.stringify(display), before)
})

test('actual workspace consumes committed spatial rectangles and maximizes without overwriting saved regions', async () => {
  const display = spatialSnapshot()
  const props = reactive({
    ...display,
    frame: acquiredFrame(),
    measurements: [reflection, transmission],
    activeTraceName: reflection.name,
    displayRevision: display.revision,
    config: { startHz: 10e6, stopHz: 8e9, points: 201, ifBandwidthHz: 1000 },
    frameCondition: { kind: 'current', label: '', detail: '' },
    canEditDisplay: true,
    commitMarker: async () => true,
  })
  const workspace = await sfcHarness(
    'src/components/diagrams/DiagramWorkspace.vue',
    props,
    {},
  )
  const panes = () =>
    findElements(
      workspace.render(),
      (node) =>
        typeof node.type === 'object' &&
        Reflect.get(node.type, 'name') === 'DiagramPane',
    )
  const before = JSON.stringify(props.diagrams)
  try {
    assert.equal(panes().length, 2)
    assert.deepEqual(
      JSON.parse(JSON.stringify(panes().map((pane) => pane.props!.style))),
      [
        {
          position: 'absolute',
          left: 'calc(0% + 1px)',
          top: 'calc(0% + 1px)',
          width: 'calc(25% - 2px)',
          height: 'calc(100% - 2px)',
        },
        {
          position: 'absolute',
          left: 'calc(25% + 1px)',
          top: 'calc(0% + 1px)',
          width: 'calc(75% - 2px)',
          height: 'calc(100% - 2px)',
        },
      ],
    )
    const event = {
      name: reflection.name,
      location: 'diagram',
      area: 'bottomLeft',
    }
    panes()[0]!.props!.onRelocateMarkerInfo(event)
    assert.deepEqual(JSON.parse(JSON.stringify(workspace.emitted)), [
      ['relocateMarkerInfo', event],
    ])
    assert.equal(JSON.stringify(props.diagrams), before)
    props.layout = 'maximized'
    props.selectedDiagram = 2
    assert.equal(panes().length, 1)
    assert.equal(panes()[0]!.props!.number, 2)
    assert.equal(panes()[0]!.props!.style, undefined)
    assert.equal(JSON.stringify(props.diagrams), before)
    props.layout = 'grid'
    assert.equal(panes().length, 2)
    assert.equal(panes()[0]!.props!.style.width, 'calc(25% - 2px)')
    assert.equal(panes()[1]!.props!.style.left, 'calc(25% + 1px)')
  } finally {
    workspace.dispose()
  }
})
