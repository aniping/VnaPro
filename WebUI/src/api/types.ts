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

export type MeasurementCandidate = Pick<
  Measurement,
  'name' | 'kind' | 'receiverKind' | 'receiverPort' | 'sourcePort'
>

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
  frameSeq: string
  originalPointCount: number
  displayPointCount: number
  decimated: boolean
  frequencyHz: number[]
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
  kind: 'config' | 'measurement' | 'sweep'
  ownerSessionId: string
  readingBack: boolean
}

export interface InstrumentNotice {
  text: string
  tone: 'info' | 'success' | 'warning' | 'error'
}
