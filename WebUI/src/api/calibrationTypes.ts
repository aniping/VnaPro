import type { ProjectionState } from './types'

export type CalibrationMethod = 'onePort' | 'twoPort' | 'fourPort'
export type CalibrationFileFormat = 'binary' | 'csv'
export type CalibrationFile = {
  format: CalibrationFileFormat
  encoding: 'base64' | 'utf8'
  filename: string
  byteLength: number
  content: string
}
export type CalibrationStandard =
  | 'open'
  | 'short'
  | 'load'
  | 'thru'
  | 'isolation'

export interface CalibrationPreviewIdentity {
  sessionId: string
  stepId: string
  calibrationRevision: string
}

export interface CalibrationPreview extends CalibrationPreviewIdentity {
  available: true
  parameter: string
  runId: string
  frameSeq: string
  originalPointCount: number
  displayPointCount: number
  sampleIndexes: number[]
  decimated: boolean
  ifBandwidthHz: number
  frequencyHz: number[]
  valid: boolean[]
  real: (number | null)[]
  imag: (number | null)[]
  dbValues: (number | null)[]
  dbStates: ProjectionState[]
  phaseValues: (number | null)[]
  phaseStates: ProjectionState[]
}

export type CalibrationPreviewResponse =
  | CalibrationPreview
  | (CalibrationPreviewIdentity & {
      available: false
      reason: string
    })

export interface CalibrationStep {
  id: string
  kind: CalibrationStandard
  port1: number
  port2: number
  captured: boolean
  runId: string
  frameSeq: string
}

/** 服务端针对完整激励轴计算；预检通过不等于系数已经准备完成。 */
export interface CalibrationApplicability {
  applicable: boolean
  prepared: boolean
  frequencyMode: 'none' | 'exact' | 'subset' | 'interpolated'
  reasonCode: 'none' | 'noSelection' | 'configurationNotReady' | 'deviceMismatch' | 'invalidAxis' | 'outOfRange' | 'powerMismatch' | 'invalidCoefficients'
  reason: string
  sourcePoints: number
  targetPoints: number
  exactPoints: number
  interpolatedPoints: number
  targetStartHz: number
  targetStopHz: number
  targetIfBandwidthHz: number
  targetMinPowerDbm: number
  targetMaxPowerDbm: number
  warnings: Array<'ifbwChanged' | 'interpolation'>
}

export interface CalibrationSetSummary {
  id: string
  name: string
  method: CalibrationMethod
  port1: number
  port2: number
  compatible: boolean
  includeIsolation?: boolean
  startHz?: number
  stopHz?: number
  points?: number
  ifBandwidthHz?: number
  powerMinDbm?: number
  powerMaxDbm?: number
  deviceIdentity?: string
  applicability?: CalibrationApplicability
}

export interface CalibrationSnapshot {
  revision: string
  collecting: boolean
  capturePending: boolean
  sessionId: string
  method: CalibrationMethod
  port1: number
  port2: number
  steps: CalibrationStep[]
  selectedSetId: string
  enabled: boolean
  compatible: boolean
  effective?: boolean
  applicability?: CalibrationApplicability
  reason: string
  includeIsolation?: boolean
  error?: string
  sets: CalibrationSetSummary[]
}

export type CalibrationCommand =
  | {
      action: 'begin'
      method: CalibrationMethod
      port1: number
      port2?: number
      includeIsolation?: boolean
    }
  | { action: 'capture'; sessionId: string; stepId: string }
  | { action: 'finish'; sessionId: string; name: string }
  | { action: 'cancel'; sessionId: string }
  | { action: 'enable'; enabled: boolean }
  | { action: 'activate' | 'select' | 'delete'; setId: string }
  | { action: 'importFile'; format: CalibrationFileFormat; encoding: 'base64' | 'utf8'; content: string }
