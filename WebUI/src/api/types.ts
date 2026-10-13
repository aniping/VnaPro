import type {
  DisplayFormat,
  LayoutMode,
  TracePresentation,
  InfoTableSettings,
  DisplayAnnotations,
  InfoTablePosition,
  MarkerInfoLocation,
} from '../domain/presentation'

/** 仪器标识保持为十进制字符串，以完整保留服务端 uint64 计数器的值。 */
export type CoreState =
  | 'Uninitialized'
  | 'Ready'
  | 'Running'
  | 'Faulted'
  | 'Closed'
export type RunAction = 'single' | 'continuous' | 'hold' | 'trigger'

export interface SweepConfiguration {
  startHz: number
  stopHz: number
  points: number
  ifBandwidthHz: number
}

export interface SweepPatch {
  startHz?: number
  stopHz?: number
  centerHz?: number
  spanHz?: number
  points?: number
  ifBandwidthHz?: number
}

export interface Measurement {
  name: string
  kind: 'receiver' | 'sParameter'
  receiverKind: 'a' | 'b'
  receiverPort: number
  sourcePort: number
  parameter: string
  unit: 'a.u.' | '1'
  calibration: 'raw' | 'uncalibrated'
}

export type ReceiverCandidate = Pick<
  Measurement,
  'name' | 'kind' | 'receiverKind' | 'receiverPort' | 'sourcePort'
>

export type MeasurementCandidate =
  | ReceiverCandidate
  | { name: string; parameter: string }

export interface TraceDestination {
  newDiagram?: boolean
  diagram?: number
}

export interface DisplaySnapshot {
  revision: string
  selectedMeasurement: string
  selectedDiagram?: number
  displayedNames: string[]
  layout: LayoutMode
  presentations: Record<string, TracePresentation>
  diagrams: DiagramDefinition[]
  infoTable?: InfoTableSettings
  annotations?: DisplayAnnotations
}

export interface DiagramDefinition {
  number: number
  measurementNames: string[]
  primaryMeasurement: string
  markerInfoVisible?: boolean
}

export type DisplayCommand =
  | { action: 'infoTable'; enabled?: boolean; position?: InfoTablePosition }
  | {
      action: 'displayAnnotations'
      activeTraceOnly?: boolean
      stimulusInfoVisible?: boolean
      fontScale?: number
    }
  | { action: 'markerInfo'; enabled: boolean; diagram?: number }
  | {
      action: 'markerInfoLocation'
      traceName: string
      location: MarkerInfoLocation
    }
  | { action: 'select'; traceName: string; diagram?: number }
  | { action: 'diagramSelect'; diagram: number }
  | { action: 'overlay' | 'split'; diagram?: number }
  | { action: 'displayed' | 'visible'; traceName: string; enabled: boolean }
  | { action: 'layout'; layout: LayoutMode; diagram?: number }
  | { action: 'format'; traceName: string; format: DisplayFormat }
  | {
      action: 'scale'
      traceName: string
      perDivision: number
      referenceLevel: number
      referencePosition: number
    }
  | { action: 'autoscale'; traceName: string; acquisitionRevision: string }
  | { action: 'smithRadius'; traceName: string; radius: number }
  | {
      action: 'marker'
      traceName: string
      markerId?: number
      markerAction?: 'update' | 'add' | 'select' | 'allOff'
      enabled?: boolean
      frequencyHz?: number
      sampleIndex?: number
      step?: number
      discrete?: boolean
      acquisitionRevision?: string
    }

export type ProjectionState = 'finite' | 'negativeInfinity' | 'undefined'
export type ImpedanceState = 'finite' | 'open' | 'overflow' | 'invalid'
export type AdmittanceState = 'finite' | 'short' | 'overflow' | 'invalid'

export interface MarkerProjection {
  available: boolean
  valid: boolean
  reason: string
  sampleIndex: number | null
  frequencyHz: number | null
  real: number | null
  imag: number | null
  value: number | null
  secondaryValue: number | null
  valueState: ProjectionState
  secondaryState: ProjectionState
  impedanceReal: number | null
  impedanceImag: number | null
  impedanceState: ImpedanceState
  admittanceReal?: number | null
  admittanceImag?: number | null
  admittanceState?: AdmittanceState
  interpolated: boolean
}

export interface TraceProjection {
  format: DisplayFormat
  values: (number | null)[]
  secondaryValues: (number | null)[]
  valueStates: ProjectionState[]
  secondaryStates: ProjectionState[]
  /** Polar 曲线保留 Re/Im，标记读数另由后端提供幅值/相位。 */
  markerValues?: (number | null)[]
  markerSecondaryValues?: (number | null)[]
  markerValueStates?: ProjectionState[]
  markerSecondaryStates?: ProjectionState[]
  /** Smith 每个显示点的 C++ 阻抗投影；旧服务或其他格式为空。 */
  impedanceRealValues: (number | null)[]
  impedanceImagValues: (number | null)[]
  impedanceStates: ImpedanceState[]
  admittanceRealValues?: (number | null)[]
  admittanceImagValues?: (number | null)[]
  admittanceStates?: AdmittanceState[]
  marker: MarkerProjection
  /** 完整缺省表示旧服务；新版响应始终包含全部十个标记投影。 */
  markers?: (MarkerProjection & { id: number })[]
  activeMarkerId?: number
}

export interface InstrumentCapabilities {
  portCount: number
  minFrequencyHz: number
  maxFrequencyHz: number
  minSweepPoints: number
  maxSweepPoints: number
}

export interface InstrumentStatus {
  identity: string
  backend: string
  state: CoreState
  error: string
  pending: boolean
  /** 逻辑配置与测量目录的 CAS 版本。 */
  revision: string
  /** 已成功提交给采集核心的硬件计划版本。 */
  acquisitionRevision: string
  runId: string
  activeConfigValid: boolean
  capabilities: InstrumentCapabilities
  config: SweepConfiguration & {
    triggerSource: 'IMM' | 'MAN'
    sweepMode: 'HOLD' | 'SING' | 'CONT'
  }
  measurements: Measurement[]
  display: DisplaySnapshot
  control: {
    owner: 'none' | 'scpi' | 'web'
    hasControl: boolean
    releasing: boolean
    leaseRemainingMs: number
    readOnlyReason: string
  }
}

export interface ReceiverTrace extends Measurement {
  available: boolean
  real: (number | null)[]
  imag: (number | null)[]
  valid: boolean[]
  projection: TraceProjection
}

export interface AcquiredFrame {
  available: true
  current: boolean
  channelId: number
  runId: string
  /** 此不可变原始帧实际使用的采集计划版本。 */
  revision: string
  /** 本次响应计算迹线时使用的测量目录版本。 */
  measurementRevision: string
  displayRevision: string
  frameSeq: string
  originalPointCount: number
  displayPointCount: number
  decimated: boolean
  frequencyHz: number[]
  sampleIndexes: number[]
  traces: ReceiverTrace[]
}

export interface UnavailableFrame {
  available: false
  current: false
  reason: string
}

export type FrameResponse = AcquiredFrame | UnavailableFrame

export interface FrameCondition {
  kind: 'empty' | 'current' | 'acquiring' | 'stale' | 'disconnected' | 'fault'
  label: string
  detail: string
}

export interface ClaimResponse {
  sessionId: string
  hasControl: true
  leaseSeconds: number
}

export interface AcceptedOperation {
  operationId: string
  state: 'pending' | 'completed' | 'failed'
}

export interface OperationResult {
  ok: boolean
  errorCode: number
  message: string
}

export interface OperationSnapshot extends AcceptedOperation {
  result: OperationResult | null
}

export interface PendingOperation {
  id: string
  label: string
  kind: 'config' | 'measurement' | 'sweep' | 'display'
  ownerSessionId: string
  readingBack: boolean
}

export interface InstrumentNotice {
  text: string
  tone: 'info' | 'success' | 'warning' | 'error'
}
