import type { MarkerDefinition, MarkerState } from './markers'

/** 本项目的共享迹线资源上限，与物理端口数、图窗数相互独立。 */
export const MAX_TRACES = 128

export type CartesianFormat =
  | 'db'
  | 'magnitude'
  | 'phase'
  | 'real'
  | 'imag'
  | 'realimag'
export type DisplayFormat = CartesianFormat | 'smith'
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

export const TRACE_COLORS = [
  '#60e600',
  '#ffff26',
  '#36c5d8',
  '#d879e8',
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
