import type { ProjectionState } from './types'

export type CalibrationMethod = 'onePort' | 'twoPort' | 'fourPort'
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
  | { action: 'select' | 'delete'; setId: string }
  | { action: 'import'; data: Record<string, unknown> }
