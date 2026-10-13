import assert from 'node:assert/strict'
import test from 'node:test'
import { decodeDisplay, decodeProjection } from '../src/api/displayDecoders'
import type { DisplayFormat } from '../src/domain/presentation'
import { measurementFormatInfo } from '../src/plot/formatting'
import { circularReadout } from '../src/plot/smithMath'
import { markerResult } from '../src/plot/traceMath'
import {
  acquiredFrame,
  displaySnapshot,
  markerProjection,
  presentation,
  projection,
  receiverTrace,
  reflection,
  legacyProtocolFields,
} from './displayFixtures'

const formats: DisplayFormat[] = [
  'db',
  'phase',
  'smith',
  'polar',
  'swr',
  'unwrappedPhase',
  'magnitude',
  'logMagnitude',
  'real',
  'imag',
  'invSmith',
  'delay',
  'delayDerivation',
]

test('all thirteen native formats decode backend projection values without recomputing measurement math', () => {
  for (const format of formats) {
    const value = projection([0.2, 0.3], format, [0.1, 0.2])
    assert.deepEqual(legacyProtocolFields(decodeProjection(value, 2)), value)
    const display = displaySnapshot([reflection])
    display.presentations.Reflect = presentation(format)
    assert.equal(
      decodeDisplay(display, [reflection]).presentations.Reflect!.format,
      format,
    )
  }
  assert.equal(measurementFormatInfo('delay', reflection).unit, 's')
  assert.equal(measurementFormatInfo('delayDerivation', reflection).unit, 's²')
  assert.equal(measurementFormatInfo('unwrappedPhase', reflection).unit, 'deg')
})

test('Inv Smith uses independent backend admittance and special states rather than transforming complex data locally', () => {
  const trace = receiverTrace()
  trace.projection = projection([0.2, 0.3], 'invSmith', [0.1, 0.2])
  trace.projection.admittanceRealValues = [17, null]
  trace.projection.admittanceImagValues = [-4, null]
  trace.projection.admittanceStates = ['finite', 'short']
  trace.projection.marker = markerProjection({
    available: true,
    valid: true,
    frequencyHz: 10e6,
    sampleIndex: 0,
    real: 0.2,
    imag: 0.1,
    value: 0.2,
    secondaryValue: 0.1,
    valueState: 'finite',
    secondaryState: 'finite',
    admittanceReal: 17,
    admittanceImag: -4,
    admittanceState: 'finite',
    reason: '',
  })
  const decoded = decodeProjection(trace.projection, 2)
  assert.deepEqual(decoded.admittanceRealValues, [17, null])
  const result = markerResult(acquiredFrame(trace), trace, 10e6, 'invSmith')!
  assert.match(circularReadout('invSmith', result), /y = 17 − j4/)
  assert.match(
    circularReadout('invSmith', {
      ...result,
      admittanceReal: null,
      admittanceImag: null,
      admittanceState: 'short',
    }),
    /∞/,
  )
  assert.match(
    circularReadout('invSmith', {
      ...result,
      admittanceReal: null,
      admittanceImag: null,
      admittanceState: 'invalid',
    }),
    /Re 0.2 \/ Im 0.1/,
  )
  assert.throws(() =>
    decodeProjection(
      { ...trace.projection, admittanceStates: ['finite', 'finite'] },
      2,
    ),
  )
  assert.throws(() =>
    decodeProjection(
      { ...trace.projection, admittanceImagValues: undefined },
      2,
    ),
  )
})

test('Polar marker arrays preserve independent magnitude/phase states and reject partial or mismatched data', () => {
  const value = projection([0.2, 0], 'polar', [0.3, 0])
  value.markerValues = [17, 0]
  value.markerSecondaryValues = [-65.37, null]
  value.markerValueStates = ['finite', 'finite']
  value.markerSecondaryStates = ['finite', 'undefined']
  const result = decodeProjection(value, 2)
  assert.deepEqual(result.markerValues, [17, 0])
  assert.deepEqual(result.markerSecondaryValues, [-65.37, null])
  assert.deepEqual(result.markerSecondaryStates, ['finite', 'undefined'])
  assert.deepEqual(
    result.values,
    [0.2, 0],
    'The curve must remain a complex projection',
  )
  assert.throws(() =>
    decodeProjection({ ...value, markerSecondaryValues: undefined }, 2),
  )
  assert.throws(() => decodeProjection({ ...value, markerValues: [17] }, 2))
  assert.throws(() =>
    decodeProjection(
      { ...value, markerSecondaryStates: ['finite', 'finite'] },
      2,
    ),
  )
  assert.throws(() =>
    decodeProjection({ ...value, markerValueStates: ['finite', 'invalid'] }, 2),
  )
  assert.throws(() => decodeProjection({ ...value, format: 'db' }, 2))
  const {
    markerValues: _values,
    markerSecondaryValues: _secondary,
    markerValueStates: _states,
    markerSecondaryStates: _secondaryStates,
    ...legacy
  } = value
  const older = decodeProjection(legacy, 2)
  assert.deepEqual(older.markerValues, [])
  assert.deepEqual(older.markerSecondaryValues, [])
})

test('explicit diagram membership rejects duplicate assignments and missing ownership while retaining all windows in maximized snapshots', () => {
  const catalog = [reflection, { ...reflection, name: 'Other' }]
  const display = displaySnapshot(catalog)
  display.layout = 'maximized'
  display.selectedDiagram = 2
  display.gridColumns = 2
  display.gridRows = 1
  display.diagrams = catalog.map((trace, index) => ({
    number: index + 1,
    primaryMeasurement: trace.name,
    measurementNames: [trace.name],
  }))
  assert.equal(decodeDisplay(display, catalog).diagrams.length, 2)
  assert.equal(decodeDisplay(display, catalog).selectedDiagram, 2)
  assert.throws(() =>
    decodeDisplay({ ...display, selectedDiagram: 3 }, catalog),
  )
  assert.throws(() =>
    decodeDisplay({ ...display, diagrams: [display.diagrams[0]] }, catalog),
  )
  assert.throws(() =>
    decodeDisplay(
      {
        ...display,
        diagrams: [
          display.diagrams[0],
          {
            ...display.diagrams[1],
            measurementNames: [reflection.name],
            primaryMeasurement: reflection.name,
          },
        ],
      },
      catalog,
    ),
  )
})
