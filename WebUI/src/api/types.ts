import type {
  DisplayFormat,
  LayoutMode,
  TracePresentation,
  InfoTableSettings,
  DisplayAnnotations,
  InfoTablePosition,
  MarkerInfoLocation,
  MarkerInfoArea,
} from '../domain/presentation'
import type { CalibrationSnapshot } from './calibrationTypes'
import type { TraceDropPosition } from '../domain/diagramLayout'

/** 仪器标识保持为十进制字符串，以完整保留服务端 uint64 计数器的值。 */
export type CoreState =
  'Uninitialized' | 'Ready' | 'Running' | 'Faulted' | 'Closed'
export type RunAction = 'single' | 'continuous' | 'hold' | 'trigger'
/** 改选 CONT 即运行，SING 停止等待启动；source 保留运行意图，start 保留模式与触发源。 */
export type SweepControlCommand =
  | { action: 'mode'; mode: 'CONT' | 'SING' }
  | { action: 'source'; source: 'IMM' | 'MAN' }
  | { action: 'start' | 'stop' | 'trigger' | 'single' }

export type ChannelCommand =
  | { action: 'new' | 'copy'; channelId?: number; newDiagram?: boolean }
  | { action: 'select' | 'delete'; channelId: number }

export type StimulusZoomCommand =
  | { action: 'apply'; traceName: string; startHz: number; stopHz: number; min: number; max: number }
  | { action: 'out' }

export interface StimulusZoomState {
  available: boolean
  canZoomOut: boolean
  reason: string
  depth: number
}

export interface StimulusZoomSelection {
  traceName: string
  startHz: number
  stopHz: number
  min: number
  max: number
  revision: string
  displayRevision: string
}

export interface SweepConfiguration {
  startHz: number
  stopHz: number
  points: number
  ifBandwidthHz: number
  powerDbm?: number
  /** 最小主机回读节拍；不代表板卡内部逐点驻留时间。 */
  sweepTimeAuto?: boolean
  sweepTimeSeconds?: number
}

export interface SweepPatch {
  startHz?: number
  stopHz?: number
  centerHz?: number
  spanHz?: number
  points?: number
  ifBandwidthHz?: number
  powerDbm?: number
  sweepTimeAuto?: boolean
  sweepTimeSeconds?: number
}

export interface Measurement {
  name: string
  /** 旧服务未提供时按 Ch1 处理；新服务中所属通道不可由图窗推断。 */
  channelId?: number
  kind: 'receiver' | 'sParameter'
  receiverKind: 'a' | 'b'
  receiverPort: number
  sourcePort: number
  parameter: string
  unit: 'a.u.' | '1'
  calibration: 'raw' | 'uncalibrated' | 'calibrated'
}

export type ReceiverCandidate = Pick<
  Measurement,
  'name' | 'kind' | 'receiverKind' | 'receiverPort' | 'sourcePort'
>

export type MeasurementCandidate =
  ReceiverCandidate | { name: string; parameter: string }

export interface TraceDestination {
  newDiagram?: boolean
  diagram?: number
  position?: TraceDropPosition
}

export interface TraceCreationOptions extends TraceDestination {
  parameter?: string
}

export interface DisplaySnapshot {
  revision: string
  selectedMeasurement: string
  selectedDiagram?: number
  displayedNames: string[]
  layout: LayoutMode
  gridColumns: number
  gridRows: number
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
  region?: { left: number; top: number; width: number; height: number }
}

export type DisplayCommand =
  | { action: 'traceMove'; traceName: string; diagram: number; position: TraceDropPosition }
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
      area?: MarkerInfoArea
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
      perDivision?: number
      referenceLevel?: number
      referencePosition?: number
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
  minPowerDbm?: number
  maxPowerDbm?: number
  manualSweepTimeSupported?: boolean
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
  /** 最近完整扫描的主机耗时，0 表示当前配置尚无实测。 */
  lastSweepTimeSeconds?: number
  capabilities: InstrumentCapabilities
  activeChannelId?: number
  channels?: ChannelStatus[]
  stimulusZoom?: StimulusZoomState
  config: SweepConfiguration & {
    triggerSource: 'IMM' | 'MAN'
    sweepMode: 'HOLD' | 'SING' | 'CONT'
    /** 持久模式选择；旧服务缺省时不能从运行态猜测。 */
    selectedSweepMode?: 'SING' | 'CONT'
  }
  measurements: Measurement[]
  display: DisplaySnapshot
  /** 缺省表示旧服务不提供校准能力。 */
  calibration?: CalibrationSnapshot
  control: {
    owner: 'none' | 'scpi' | 'web'
    hasControl: boolean
    releasing: boolean
    leaseRemainingMs: number
    readOnlyReason: string
  }
}

/** 独立采集上下文；测量目录和图窗目录仍由整台仪器统一管理。 */
export interface ChannelStatus {
  channelId: number
  name: string
  state: CoreState
  error: string
  acquisitionRevision: string
  runId: string
  activeConfigValid: boolean
  lastSweepTimeSeconds?: number
  config: InstrumentStatus['config']
  calibration?: CalibrationSnapshot
  stimulusZoom?: StimulusZoomState
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
  /** 旧服务缺省；新版标明本次复数投影使用的校准版本。 */
  calibrationRevision?: string
  frameSeq: string
  originalPointCount: number
  displayPointCount: number
  decimated: boolean
  frequencyHz: number[]
  sampleIndexes: number[]
  traces: ReceiverTrace[]
  /** 顶层仍是活动通道的兼容别名；各通道使用自己的完整频率轴和版本。 */
  channelFrames?: FrameResponse[]
  /** 仅用于绘图的真实扫描前缀；正式帧、标记和导出仍使用外层完整帧。 */
  sweepProgress?: SweepProgress
}

export interface SweepProgress {
  completedPoints: number
  frame: AcquiredFrame
}

export interface UnavailableFrame {
  available: false
  current: false
  reason: string
  channelId?: number
  channelFrames?: FrameResponse[]
  sweepProgress?: SweepProgress
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
  kind: 'config' | 'measurement' | 'sweep' | 'display' | 'calibration'
  ownerSessionId: string
  readingBack: boolean
}

export interface InstrumentNotice {
  text: string
  tone: 'info' | 'success' | 'warning' | 'error'
}
