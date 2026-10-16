import {
  isCircularFormat,
  type DisplayFormat,
  type DisplayScale,
} from '../domain/presentation'
import type { StimulusZoomSelection } from '../api/types'
export type { StimulusZoomSelection } from '../api/types'

export interface PlotGesturePoint { x: number; y: number }
export interface PlotGestureRect { left: number; top: number; width: number; height: number }

export interface TracePanMove {
  name: string
  reference: number
  displayRevision: string
}

export interface TracePanGeometryInput {
  name: string
  format: DisplayFormat
  scale: DisplayScale
  displayRevision: string
  startClientY: number
  plotHeightCSS: number
}

export interface TracePanGeometry {
  readonly scaleAt: (clientY: number) => DisplayScale | null
  readonly commandAt: (clientY: number) => TracePanMove | null
}

export interface StimulusZoomGeometryInput {
  traceName: string
  format: DisplayFormat
  scale: DisplayScale
  revision: string
  displayRevision: string
  range: { min: number; max: number }
  /** 逻辑画布内的绘图矩形。 */
  rect: PlotGestureRect
  /** 整个画布的屏幕 CSS 边界。 */
  bounds: PlotGestureRect
  size: { width: number; height: number }
}

export interface StimulusZoomGeometry {
  readonly pointAt: (client: PlotGesturePoint) => PlotGesturePoint | null
  /** 两端都接收屏幕 client 坐标；内部映射后的坐标仅供画框预览。 */
  readonly selectionAt: (startClient: PlotGesturePoint, endClient: PlotGesturePoint) => StimulusZoomSelection | null
}

function revisionValid(value: string): boolean {
  return /^[0-9]+$/.test(value)
}

function coordinates(scale: DisplayScale, format: DisplayFormat) {
  const position = scale.referencePosition ?? 8
  if (isCircularFormat(format) ||
    ![scale.min, scale.max, position].every(Number.isFinite) ||
    scale.min >= scale.max || position < 0 || position > 10 ||
    (format === 'logMagnitude' && scale.min <= 0)) return null
  const min = format === 'logMagnitude' ? Math.log10(scale.min) : scale.min
  const max = format === 'logMagnitude' ? Math.log10(scale.max) : scale.max
  return Number.isFinite(max - min) && max > min ? { min, max, position } : null
}

/** 去除坐标运算的二进制浮点长尾，不对采集数据或格式投影做舍入。 */
function stable(value: number): number {
  return Number(value.toPrecision(15))
}

/** 冻结纵向拖动几何，只改变 Ref Value，原始样本、每格跨度与参考三角位置保持。 */
export function createTracePanGeometry(input: TracePanGeometryInput): TracePanGeometry | null {
  const { name, format, displayRevision, startClientY, plotHeightCSS } = input
  const initialScale = { ...input.scale }
  const axis = coordinates(initialScale, format)
  if (!name || !revisionValid(displayRevision) || !axis ||
    ![startClientY, plotHeightCSS].every(Number.isFinite) || plotHeightCSS <= 0) return null
  const span = axis.max - axis.min
  const initialReference = axis.min + span * axis.position / 10
  const fromCoordinate = (value: number) => format === 'logMagnitude' ? 10 ** value : value
  const shifted = (clientY: number) => {
    if (!Number.isFinite(clientY)) return null
    if (clientY === startClientY)
      return { reference: fromCoordinate(initialReference), min: initialScale.min, max: initialScale.max }
    // 每格的 1/100 是本项目的手势精度，不是已核验的原机量化算法。
    // 未移动保留起点；实际移动时量化绝对参考坐标，清除 Autoscale 等来源的长小数。
    const step = span / 1000
    const coordinate = stable(Math.round((initialReference + (clientY - startClientY) / plotHeightCSS * span) / step) * step)
    const reference = fromCoordinate(coordinate)
    const minimum = coordinate - span * axis.position / 10
    const min = fromCoordinate(stable(minimum))
    const max = fromCoordinate(stable(minimum + span))
    return [reference, min, max].every(Number.isFinite) && min < max &&
      (format !== 'logMagnitude' || min > 0)
      ? { reference, min, max }
      : null
  }
  return Object.freeze({
    scaleAt(clientY: number) {
      const result = shifted(clientY)
      return result ? clientY === startClientY
        ? { ...initialScale }
        : { min: result.min, max: result.max, referencePosition: axis.position }
        : null
    },
    commandAt(clientY: number) {
      const result = shifted(clientY)
      return result ? { name, reference: result.reference, displayRevision } : null
    },
  })
}

/** 框选冻结真实频率及纵轴范围；鼠标反向、画布 CSS 缩放不改变所选物理区域。 */
export function createStimulusZoomGeometry(input: StimulusZoomGeometryInput): StimulusZoomGeometry | null {
  const { traceName, format, revision, displayRevision } = input
  const axis = coordinates(input.scale, format)
  const range = { ...input.range }
  const rect = { ...input.rect }
  const bounds = { ...input.bounds }
  const size = { ...input.size }
  if (!traceName || !revisionValid(revision) || !revisionValid(displayRevision) || !axis ||
    ![range.min, range.max, rect.left, rect.top, rect.width, rect.height,
      bounds.left, bounds.top, bounds.width, bounds.height, size.width, size.height].every(Number.isFinite) ||
    range.min < 0 || range.max <= range.min ||
    rect.left < 0 || rect.top < 0 || rect.width <= 0 || rect.height <= 0 ||
    size.width <= 0 || size.height <= 0 || bounds.width <= 0 || bounds.height <= 0 ||
    rect.left + rect.width > size.width || rect.top + rect.height > size.height) return null
  const pointAt = (client: PlotGesturePoint): PlotGesturePoint | null => {
    if (![client.x, client.y].every(Number.isFinite)) return null
    return {
      x: Math.max(rect.left, Math.min(rect.left + rect.width, (client.x - bounds.left) * size.width / bounds.width)),
      y: Math.max(rect.top, Math.min(rect.top + rect.height, (client.y - bounds.top) * size.height / bounds.height)),
    }
  }
  const frequencyAt = (x: number) => range.min + (x - rect.left) / rect.width * (range.max - range.min)
  const valueAt = (y: number) => {
    const coordinate = axis.max - (y - rect.top) / rect.height * (axis.max - axis.min)
    return format === 'logMagnitude' ? 10 ** coordinate : coordinate
  }
  return Object.freeze({
    pointAt,
    selectionAt(startClient: PlotGesturePoint, endClient: PlotGesturePoint) {
      const start = pointAt(startClient)
      const end = pointAt(endClient)
      if (!start || !end ||
        Math.abs(end.x - start.x) * bounds.width / size.width < 6 ||
        Math.abs(end.y - start.y) * bounds.height / size.height < 6) return null
      const startHz = frequencyAt(Math.min(start.x, end.x))
      const stopHz = frequencyAt(Math.max(start.x, end.x))
      const min = valueAt(Math.max(start.y, end.y))
      const max = valueAt(Math.min(start.y, end.y))
      return [startHz, stopHz, min, max].every(Number.isFinite) && startHz < stopHz && min < max &&
        (format !== 'logMagnitude' || min > 0)
        ? { traceName, startHz, stopHz, min, max, revision, displayRevision }
        : null
    },
  })
}
