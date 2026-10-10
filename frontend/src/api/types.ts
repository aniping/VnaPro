/** Instrument identities stay decimal strings: the server uses uint64 counters. */
export type CoreState = 'Uninitialized' | 'Ready' | 'Running' | 'Faulted' | 'Closed'
export type RunAction = 'single' | 'continuous' | 'hold' | 'trigger'

export interface SweepConfiguration {
  startHz: number
  stopHz: number
  points: number
  ifBandwidthHz: number
}

export interface Measurement {
  name: string
  receiverKind: 'a' | 'b'
  receiverPort: number
  sourcePort: number
  parameter: string
}

export type MeasurementCandidate = Omit<Measurement, 'parameter'>

export interface InstrumentStatus {
  identity: string
  backend: string
  state: CoreState
  error: string
  pending: boolean
  revision: string
  runId: string
  activeConfigValid: boolean
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
  revision: string
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
