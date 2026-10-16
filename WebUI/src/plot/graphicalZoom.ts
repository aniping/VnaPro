import { isCircularFormat, type DisplayFormat, type DisplayScale } from '../domain/presentation'
import { scaleCoordinate } from './traceMath'

/** 相对原始图窗的客户端视口，不保存到采集配置或共享显示状态。 */
export interface GraphicalZoomViewport {
  left: number
  top: number
  width: number
  height: number
}

export const FULL_GRAPHICAL_ZOOM: Readonly<GraphicalZoomViewport> = Object.freeze({ left: 0, top: 0, width: 1, height: 1 })

function validViewport(value: GraphicalZoomViewport): boolean {
  return [value.left, value.top, value.width, value.height].every(Number.isFinite) &&
    value.left >= 0 && value.width > 0 && value.height > 0 &&
    value.left + value.width <= 1 + 1e-12 && Number.isFinite(value.top + value.height)
}

export function isGraphicalZoomed(value: GraphicalZoomViewport | null): boolean {
  return value !== null && validViewport(value) &&
    (Math.abs(value.left) > 1e-12 || Math.abs(value.top) > 1e-12 ||
      Math.abs(value.width - 1) > 1e-12 || Math.abs(value.height - 1) > 1e-12)
}

/** selection 是当前可视绘图区内的归一化矩形，支持在已有视口上继续框选。 */
export function composeGraphicalZoom(
  viewport: GraphicalZoomViewport,
  selection: GraphicalZoomViewport,
): GraphicalZoomViewport | null {
  if (!validViewport(viewport) || !validViewport(selection) || selection.top < 0 ||
    selection.top + selection.height > 1 + 1e-12) return null
  const next = {
    left: viewport.left + selection.left * viewport.width,
    top: viewport.top + selection.top * viewport.height,
    width: viewport.width * selection.width,
    height: viewport.height * selection.height,
  }
  return validViewport(next) ? Object.freeze(next) : null
}

export function graphicalFrequencyRange(
  range: { min: number; max: number },
  viewport: GraphicalZoomViewport,
): { min: number; max: number } | null {
  if (!validViewport(viewport) || ![range.min, range.max].every(Number.isFinite) || range.max <= range.min) return null
  const span = range.max - range.min
  const min = range.min + viewport.left * span
  const max = range.min + (viewport.left + viewport.width) * span
  return [min, max].every(Number.isFinite) && min < max ? Object.freeze({ min, max }) : null
}

/** 各条 Cartesian 迹线以同一屏幕分数缩放，仍使用各自的真实响应单位。 */
export function graphicalTraceScale(
  scale: DisplayScale,
  format: DisplayFormat,
  viewport: GraphicalZoomViewport,
): DisplayScale | null {
  if (isCircularFormat(format) || !validViewport(viewport)) return null
  const minimum = scaleCoordinate(scale.min, format)
  const maximum = scaleCoordinate(scale.max, format)
  const span = maximum - minimum
  if (![minimum, maximum, span].every(Number.isFinite) || span <= 0) return null
  const top = maximum - viewport.top * span
  const bottom = top - viewport.height * span
  const min = format === 'logMagnitude' ? 10 ** bottom : bottom
  const max = format === 'logMagnitude' ? 10 ** top : top
  return [min, max].every(Number.isFinite) && min < max && (format !== 'logMagnitude' || min > 0)
    ? Object.freeze({ ...scale, min, max })
    : null
}

/** 纵拖仅移动临时视口；delta 是当前可视高度的分数，可移出原先的 Y 范围。 */
export function panGraphicalZoom(
  viewport: GraphicalZoomViewport,
  delta: number,
): GraphicalZoomViewport | null {
  if (!validViewport(viewport) || !Number.isFinite(delta)) return null
  const next = { ...viewport, top: viewport.top + delta * viewport.height }
  return validViewport(next) ? Object.freeze(next) : null
}
