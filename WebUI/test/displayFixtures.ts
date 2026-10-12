import type {
  AcquiredFrame,
  DisplaySnapshot,
  InstrumentStatus,
  MarkerProjection,
  Measurement,
  ReceiverTrace,
  TraceProjection,
} from '../src/api/types'
import type {
  DisplayFormat,
  TracePresentation,
} from '../src/domain/presentation'

export const reflection: Measurement = {
  name: 'Reflect',
  kind: 'sParameter',
  receiverKind: 'b',
  receiverPort: 1,
  sourcePort: 1,
  parameter: 'S11',
  unit: '1',
  calibration: 'uncalibrated',
}
export const transmission: Measurement = {
  ...reflection,
  name: 'Transmit',
  receiverPort: 2,
  parameter: 'S21',
}

/** 测试固定后端 DTO，不在夹具中实现仪器格式算法。 */
export function presentation(format: DisplayFormat = 'db'): TracePresentation {
  return {
    format,
    cartesianFormat: format === 'smith' ? 'db' : format,
    allowedFormats: [
      'db',
      'magnitude',
      'phase',
      'real',
      'imag',
      'realimag',
      'smith',
    ],
    scale: { min: -90, max: 10, referencePosition: 9 },
    smithRadius: 1,
    visible: true,
    marker: {
      enabled: false,
      frequencyHz: null,
      sampleIndex: null,
      discrete: false,
    },
  }
}
export function displaySnapshot(
  catalog: readonly Pick<Measurement, 'name'>[],
  revision = '1',
): DisplaySnapshot {
  return {
    revision,
    selectedMeasurement: catalog[0]?.name ?? '',
    displayedNames: catalog.slice(0, 4).map((trace) => trace.name),
    layout: 'single',
    diagrams: [
      {
        number: 1,
        measurementNames: catalog.slice(0, 4).map((trace) => trace.name),
        primaryMeasurement: catalog[0]?.name ?? '',
      },
    ],
    presentations: Object.fromEntries(
      catalog.map((trace) => [trace.name, presentation()]),
    ),
  }
}
export function markerProjection(
  overrides: Partial<MarkerProjection> = {},
): MarkerProjection {
  return {
    available: false,
    valid: false,
    reason: 'Marker off',
    sampleIndex: null,
    frequencyHz: null,
    real: null,
    imag: null,
    value: null,
    secondaryValue: null,
    valueState: 'undefined',
    secondaryState: 'undefined',
    impedanceReal: null,
    impedanceImag: null,
    impedanceState: 'invalid',
    interpolated: false,
    ...overrides,
  }
}
export function projection(
  values: (number | null)[],
  format: DisplayFormat = 'db',
  secondaryValues = values.map(() => null as number | null),
): TraceProjection {
  return {
    format,
    values,
    secondaryValues,
    valueStates: values.map((value) =>
      value === null ? 'undefined' : 'finite',
    ),
    secondaryStates: secondaryValues.map((value) =>
      value === null ? 'undefined' : 'finite',
    ),
    impedanceRealValues: format === 'smith' ? values.map(() => null) : [],
    impedanceImagValues: format === 'smith' ? values.map(() => null) : [],
    impedanceStates: format === 'smith' ? values.map(() => 'invalid') : [],
    marker: markerProjection(),
  }
}
export function receiverTrace(
  measurement: Measurement = reflection,
  values = [0.2, 0.3],
): ReceiverTrace {
  return {
    ...measurement,
    available: true,
    real: values,
    imag: values.map(() => 0),
    valid: values.map(() => true),
    projection: projection(values.map(() => -13.5)),
  }
}
export function acquiredFrame(
  trace: ReceiverTrace = receiverTrace(),
): AcquiredFrame {
  return {
    available: true,
    current: true,
    channelId: 1,
    runId: '1',
    revision: '1',
    measurementRevision: '1',
    displayRevision: '1',
    frameSeq: '1',
    originalPointCount: trace.real.length,
    displayPointCount: trace.real.length,
    decimated: false,
    frequencyHz: trace.real.map((_, index) => (index + 1) * 10e6),
    sampleIndexes: trace.real.map((_, index) => index),
    traces: [trace],
  }
}
export function instrumentStatus(
  catalog = [reflection, transmission],
): InstrumentStatus {
  return {
    identity: 'VnaPro,NativeSimulator,4PORT,1.0',
    backend: 'NativeSimulator',
    state: 'Ready',
    error: '',
    pending: false,
    revision: '1',
    acquisitionRevision: '1',
    runId: '1',
    activeConfigValid: true,
    capabilities: {
      portCount: 4,
      minFrequencyHz: 10e6,
      maxFrequencyHz: 8e9,
      minSweepPoints: 2,
      maxSweepPoints: 10001,
    },
    config: {
      startHz: 10e6,
      stopHz: 8e9,
      points: 201,
      ifBandwidthHz: 1000,
      triggerSource: 'IMM',
      sweepMode: 'HOLD',
    },
    measurements: catalog,
    display: displaySnapshot(catalog),
    control: {
      owner: 'web',
      hasControl: true,
      releasing: false,
      leaseRemainingMs: 30000,
      readOnlyReason: '',
    },
  }
}
