import type { PlotRect } from './canvasRenderer'
import type { SelectableCurve } from './traceHitTest'
import { normalizedX, normalizedY } from './traceMath'
import { smithPoint, smithViewport } from './smithMath'

/** 只定位已采前缀中的可见实测点，不使用计时器或像素插值制造扫描位置。 */
export function sweepCursorPoint(
  curve: SelectableCurve,
  completedPoints: number,
  geometry: { width: number; height: number; rect: PlotRect; scaleX: number; scaleY: number },
): { x: number; y: number; index: number } | null {
  for (let index = completedPoints - 1; index >= 0; index--) {
    let point: { x: number; y: number }
    if (curve.kind === 'circular') {
      const sample = curve.points[index]
      if (!sample) continue
      point = smithPoint(sample, smithViewport(geometry.width, geometry.height,
        geometry.scaleX, geometry.scaleY), curve.radius)
    } else {
      const value = curve.values[index]
      const range = curve.range
      const frequency = curve.frequencyHz[index]
      if (value === undefined || frequency === undefined || !range) continue
      point = {
        x: geometry.rect.left + normalizedX(frequency, range.min, range.max) * geometry.rect.width,
        y: geometry.rect.top + normalizedY(value, curve.scale, curve.format) * geometry.rect.height,
      }
    }
    if (!Number.isFinite(point.x) || !Number.isFinite(point.y)) continue
    const rect = geometry.rect
    if (point.x < rect.left || point.x > rect.left + rect.width ||
      point.y < rect.top || point.y > rect.top + rect.height) return null
    return { ...point, index }
  }
  return null
}
