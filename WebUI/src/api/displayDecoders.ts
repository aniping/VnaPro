import type { DisplayFormat, TracePresentation } from '../domain/presentation'
import { MAX_TRACES } from '../domain/presentation'
import {
  decodeMarkerCollection,
  decodeMarkerProjections,
  decodeMarkerState,
} from './markerDecoders'
import type {
  DisplaySnapshot,
  DiagramDefinition,
  MarkerProjection,
  Measurement,
  ImpedanceState,
  ProjectionState,
  TraceProjection,
} from './types'
import {
  array,
  choice,
  flag,
  identity,
  integer,
  invalid,
  number,
  object,
  text,
} from './decodeValues'

const FORMATS: readonly DisplayFormat[] = [
  'db',
  'magnitude',
  'phase',
  'real',
  'imag',
  'realimag',
  'smith',
]
const STATES: readonly ProjectionState[] = [
  'finite',
  'negativeInfinity',
  'undefined',
]
const IMPEDANCE_STATES: readonly ImpedanceState[] = [
  'finite',
  'open',
  'overflow',
  'invalid',
]

function nullableNumber(value: unknown, field: string): number | null {
  return value === null ? null : number(value, field)
}

function presentation(value: unknown): TracePresentation {
  const item = object(value, 'display.presentation')
  const scale = object(item.scale, 'display.scale')
  const marker = decodeMarkerState(item.marker)
  const format = choice(item.format, 'display.format', FORMATS)
  const allowedFormats = array(
    item.allowedFormats,
    'display.allowedFormats',
    7,
  ).map((value) => choice(value, 'display.allowedFormat', FORMATS))
  if (
    !allowedFormats.includes(format) ||
    new Set(allowedFormats).size !== allowedFormats.length
  )
    invalid('display.allowedFormats')
  const min = number(scale.min, 'display.scale.min')
  const max = number(scale.max, 'display.scale.max')
  if (!(max > min) || !Number.isFinite(max - min))
    invalid('display.scale.bounds')
  return {
    format,
    allowedFormats,
    cartesianFormat: choice(item.cartesianFormat, 'display.cartesianFormat', [
      'db',
      'magnitude',
      'phase',
      'real',
      'imag',
      'realimag',
    ]),
    scale: {
      min,
      max,
      referencePosition: number(
        scale.referencePosition,
        'display.scale.referencePosition',
        0,
        10,
      ),
    },
    smithRadius: number(item.smithRadius, 'display.smithRadius', 0.01),
    visible: flag(item.visible, 'display.visible'),
    marker,
    ...decodeMarkerCollection(item, marker),
  }
}

function diagrams(
  value: unknown,
  displayedNames: readonly string[],
): DiagramDefinition[] {
  const result = array(value, 'display.diagrams', 4).map((value) => {
    const item = object(value, 'display.diagram')
    const measurementNames = array(
      item.measurementNames,
      'display.diagram.measurementNames',
      MAX_TRACES,
    ).map((value) => text(value, 'display.diagram.measurementName'))
    if (
      new Set(measurementNames).size !== measurementNames.length ||
      measurementNames.some((name) => !displayedNames.includes(name))
    )
      invalid('display.diagram.measurementNames')
    const primaryMeasurement = text(
      item.primaryMeasurement,
      'display.diagram.primaryMeasurement',
    )
    if (
      measurementNames.length
        ? !measurementNames.includes(primaryMeasurement)
        : primaryMeasurement !== ''
    )
      invalid('display.diagram.primaryMeasurement')
    return {
      number: integer(item.number, 'display.diagram.number', 1, 4),
      measurementNames,
      primaryMeasurement,
    }
  })
  if (
    !result.length ||
    new Set(result.map((diagram) => diagram.number)).size !== result.length
  )
    invalid('display.diagrams')
  return result
}

export function decodeDisplay(
  value: unknown,
  measurements: Measurement[],
): DisplaySnapshot {
  const item = object(value, 'display')
  const catalog = new Set(measurements.map((measurement) => measurement.name))
  const displayedNames = array(
    item.displayedNames,
    'display.displayedNames',
    MAX_TRACES,
  ).map((value) => text(value, 'display.traceName'))
  if (
    new Set(displayedNames).size !== displayedNames.length ||
    displayedNames.some((name) => !catalog.has(name))
  )
    invalid('display.displayedNames')
  const selectedMeasurement = text(
    item.selectedMeasurement,
    'display.selectedMeasurement',
  )
  if (selectedMeasurement !== '' && !catalog.has(selectedMeasurement))
    invalid('display.selectedMeasurement')
  const values = object(item.presentations, 'display.presentations')
  if (
    Object.keys(values).length !== measurements.length ||
    Object.keys(values).some((name) => !catalog.has(name))
  )
    invalid('display.presentations')
  const presentations = Object.fromEntries(
    Object.entries(values).map(([name, value]) => [name, presentation(value)]),
  )
  return {
    revision: identity(item.revision, 'display.revision'),
    selectedMeasurement,
    displayedNames,
    layout: choice(item.layout, 'display.layout', [
      'single',
      'grid',
      'maximized',
    ]),
    presentations,
    diagrams: diagrams(item.diagrams, displayedNames),
  }
}

function markerProjection(value: unknown): MarkerProjection {
  const item = object(value, 'projection.marker')
  const result: MarkerProjection = {
    available: flag(item.available, 'projection.marker.available'),
    valid: flag(item.valid, 'projection.marker.valid'),
    reason: text(item.reason, 'projection.marker.reason'),
    sampleIndex:
      item.sampleIndex === null
        ? null
        : integer(item.sampleIndex, 'projection.marker.sampleIndex', 0, 10000),
    frequencyHz: nullableNumber(
      item.frequencyHz,
      'projection.marker.frequencyHz',
    ),
    real: nullableNumber(item.real, 'projection.marker.real'),
    imag: nullableNumber(item.imag, 'projection.marker.imag'),
    value: nullableNumber(item.value, 'projection.marker.value'),
    secondaryValue: nullableNumber(
      item.secondaryValue,
      'projection.marker.secondaryValue',
    ),
    valueState: choice(item.valueState, 'projection.marker.valueState', STATES),
    secondaryState: choice(
      item.secondaryState,
      'projection.marker.secondaryState',
      STATES,
    ),
    impedanceReal: nullableNumber(
      item.impedanceReal,
      'projection.marker.impedanceReal',
    ),
    impedanceImag: nullableNumber(
      item.impedanceImag,
      'projection.marker.impedanceImag',
    ),
    impedanceState: choice(
      item.impedanceState,
      'projection.marker.impedanceState',
      ['finite', 'open', 'overflow', 'invalid'],
    ),
    interpolated: flag(item.interpolated, 'projection.marker.interpolated'),
  }
  if (
    (result.available && result.frequencyHz === null) ||
    (result.valid && (result.real === null || result.imag === null))
  )
    invalid('projection.marker.available')
  return result
}

export function decodeProjection(
  value: unknown,
  count: number,
): TraceProjection {
  const item = object(value, 'projection')
  const format = choice(item.format, 'projection.format', FORMATS)
  const marker = markerProjection(item.marker)
  const result: TraceProjection = {
    format,
    values: array(item.values, 'projection.values', 2001).map((value) =>
      nullableNumber(value, 'projection.value'),
    ),
    secondaryValues: array(
      item.secondaryValues,
      'projection.secondaryValues',
      2001,
    ).map((value) => nullableNumber(value, 'projection.secondaryValue')),
    valueStates: array(item.valueStates, 'projection.valueStates', 2001).map(
      (value) => choice(value, 'projection.valueState', STATES),
    ),
    secondaryStates: array(
      item.secondaryStates,
      'projection.secondaryStates',
      2001,
    ).map((value) => choice(value, 'projection.secondaryState', STATES)),
    ...pointImpedance(item, format, count),
    marker,
    ...decodeMarkerProjections(item, marker, markerProjection),
  }
  if (
    [
      result.values,
      result.secondaryValues,
      result.valueStates,
      result.secondaryStates,
    ].some((values) => values.length !== count)
  )
    invalid('projection.pointCount')
  for (let index = 0; index < count; index++) {
    if (
      (result.valueStates[index] === 'finite') !==
        (result.values[index] !== null) ||
      (result.secondaryStates[index] === 'finite') !==
        (result.secondaryValues[index] !== null)
    )
      invalid('projection.valueState')
  }
  return result
}

function pointImpedance(
  item: Record<string, unknown>,
  format: DisplayFormat,
  count: number,
): Pick<
  TraceProjection,
  'impedanceRealValues' | 'impedanceImagValues' | 'impedanceStates'
> {
  const fields = [
    item.impedanceRealValues,
    item.impedanceImagValues,
    item.impedanceStates,
  ]
  // 新字段可整体缺省以兼容旧服务；部分缺省属于损坏响应，不能拼接旧标记的阻抗。
  if (fields.every((value) => value === undefined)) {
    return {
      impedanceRealValues: [],
      impedanceImagValues: [],
      impedanceStates: [],
    }
  }
  const impedanceRealValues = array(
    item.impedanceRealValues,
    'projection.impedanceRealValues',
    2001,
  ).map((value) => nullableNumber(value, 'projection.impedanceRealValue'))
  const impedanceImagValues = array(
    item.impedanceImagValues,
    'projection.impedanceImagValues',
    2001,
  ).map((value) => nullableNumber(value, 'projection.impedanceImagValue'))
  const impedanceStates = array(
    item.impedanceStates,
    'projection.impedanceStates',
    2001,
  ).map((value) => choice(value, 'projection.impedanceState', IMPEDANCE_STATES))
  const expected = format === 'smith' ? count : 0
  if (
    [impedanceRealValues, impedanceImagValues, impedanceStates].some(
      (values) => values.length !== expected,
    )
  ) {
    invalid('projection.impedancePointCount')
  }
  for (let index = 0; index < expected; index++) {
    const finite = impedanceStates[index] === 'finite'
    if (
      (impedanceRealValues[index] !== null) !== finite ||
      (impedanceImagValues[index] !== null) !== finite
    ) {
      invalid('projection.impedanceState')
    }
  }
  return { impedanceRealValues, impedanceImagValues, impedanceStates }
}
