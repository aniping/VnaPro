export type DisplayFormat = 'db' | 'magnitude' | 'phase' | 'real' | 'imag' | 'realimag'
export type LayoutMode = 'single' | 'grid' | 'maximized'

export interface DisplayScale {
  min: number
  max: number
  /** Divisions above the bottom edge, matching the instrument's 0..10 reference position. */
  referencePosition?: number
}

export interface TracePresentation {
  format: DisplayFormat
  scale: DisplayScale | null
  visible: boolean
  marker: {
    enabled: boolean
    frequencyHz: number | null
  }
}

export const TRACE_COLORS = ['#60e600', '#ffff26', '#36c5d8', '#d879e8'] as const

export function traceColor(index: number): string {
  return TRACE_COLORS[((index % TRACE_COLORS.length) + TRACE_COLORS.length) % TRACE_COLORS.length]!
}

/** ZNB factory presentation restarts the color sequence in each diagram. */
export function diagramTraceColor(index: number, layout: LayoutMode): string {
  return traceColor(layout === 'single' ? index : 0)
}

export function defaultTracePresentation(_index = 0): TracePresentation {
  return { format: 'db', scale: { min: -90, max: 10, referencePosition: 9 }, visible: true, marker: { enabled: false, frequencyHz: null } }
}
