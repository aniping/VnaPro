import type { MarkerDefinition, MarkerState } from './markers'

/** 本项目的共享迹线资源上限，与物理端口数、图窗数相互独立。 */
export const MAX_TRACES = 128
export const MAX_DIAGRAMS = 128

export type CartesianFormat =
  | 'db'
  | 'magnitude'
  | 'phase'
  | 'real'
  | 'imag'
  | 'realimag'
  | 'swr'
  | 'unwrappedPhase'
  | 'logMagnitude'
  | 'delay'
  | 'delayDerivation'
export type DisplayFormat = CartesianFormat | 'smith' | 'polar' | 'invSmith'

export function isCircularFormat(format: DisplayFormat): boolean {
  return format === 'smith' || format === 'polar' || format === 'invSmith'
}
export type LayoutMode = 'single' | 'grid' | 'maximized'

export interface DisplayScale {
  min: number
  max: number
  /** 从绘图区底边向上计数的格数，对应仪器 0..10 的参考位置。 */
  referencePosition?: number
}

export interface ScaleValues {
  perDivision: number
  referenceLevel: number
  referencePosition: number
}

export interface TracePresentation {
  allowedFormats: DisplayFormat[]
  format: DisplayFormat
  scale: DisplayScale
  cartesianFormat: CartesianFormat
  smithRadius: number
  visible: boolean
  /** 后端派生的活动标记只读别名，仅用于兼容旧协议。 */
  marker: MarkerState
  markers?: MarkerDefinition[]
  activeMarkerId?: number
}

/** 八色循环参考 ZNB 的颜色数量配置；具体 RGB 是本项目的显示色板。 */
export const TRACE_COLORS = [
  '#60e600',
  '#ffff26',
  '#36c5d8',
  '#d879e8',
  '#ff9b45',
  '#82aaff',
  '#ff6e78',
  '#e5e9ef',
] as const

export function traceColor(index: number): string {
  return TRACE_COLORS[
    ((index % TRACE_COLORS.length) + TRACE_COLORS.length) % TRACE_COLORS.length
  ]!
}

/** 每个图窗独立排列显示颜色，仅属于渲染样式。 */
export function diagramTraceColor(index: number, _layout: LayoutMode): string {
  return traceColor(index)
}
