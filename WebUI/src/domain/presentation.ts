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
  marker: {
    enabled: boolean
    frequencyHz: number | null
    /** 同频采样时保留实际展示数组索引；频率改变后重新吸附。 */
    sampleIndex?: number | null
    discrete: boolean
  }
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
export function diagramTraceColor(index: number, layout: LayoutMode): string {
  return traceColor(layout === 'single' ? index : 0)
}
