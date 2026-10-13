import type { ScreenBounds } from './canvasRenderer'
import type { MixedCurve, MixedDiagram } from './mixedRenderer'
import { smithPoint, smithViewport, validSmithRadius } from './smithMath'
import { normalizedX, normalizedY } from './traceMath'

export type SelectableCurve = MixedCurve & { name: string }

export interface TraceHitTestInput extends Pick<
  MixedDiagram,
  'width' | 'height' | 'rect' | 'xMin' | 'xMax'
> {
  curves: readonly SelectableCurve[]
  bounds: ScreenBounds
}

interface Point {
  x: number
  y: number
}

interface ClipBounds {
  left: number
  top: number
  right: number
  bottom: number
}

function finitePoint(point: Point): boolean {
  return Number.isFinite(point.x) && Number.isFinite(point.y)
}

function inside(point: Point, clip: ClipBounds): boolean {
  return (
    point.x >= clip.left &&
    point.x <= clip.right &&
    point.y >= clip.top &&
    point.y <= clip.bottom
  )
}

/** 先裁剪线段，再求像素距离，避免选中视口之外的延长线。 */
function segmentDistanceSquared(
  point: Point,
  start: Point,
  end: Point,
  clip: ClipBounds,
): number {
  const dx = end.x - start.x
  const dy = end.y - start.y
  let first = 0
  let last = 1
  for (let edge = 0; edge < 4; edge++) {
    const direction = edge < 2 ? dx : dy
    const p = edge % 2 === 0 ? -direction : direction
    const q =
      edge === 0
        ? start.x - clip.left
        : edge === 1
          ? clip.right - start.x
          : edge === 2
            ? start.y - clip.top
            : clip.bottom - start.y
    if (p === 0) {
      if (q < 0) return Infinity
      continue
    }
    const ratio = q / p
    if (p < 0) first = Math.max(first, ratio)
    else last = Math.min(last, ratio)
    if (first > last) return Infinity
  }
  const x = start.x + first * dx
  const y = start.y + first * dy
  const segmentX = (last - first) * dx
  const segmentY = (last - first) * dy
  const lengthSquared = segmentX ** 2 + segmentY ** 2
  const fraction =
    lengthSquared === 0
      ? 0
      : Math.max(
          0,
          Math.min(
            1,
            ((point.x - x) * segmentX + (point.y - y) * segmentY) /
              lengthSquared,
          ),
        )
  return (
    (point.x - x - fraction * segmentX) ** 2 +
    (point.y - y - fraction * segmentY) ** 2
  )
}

/** 命中只读取已投影的曲线；返回名称供调用者提交后端选中请求。 */
export function hitTestTrace(
  input: TraceHitTestInput,
  point: Point,
  tolerancePx = 6,
): string | null {
  const { bounds, rect } = input
  if (
    !finitePoint(point) ||
    !Number.isFinite(tolerancePx) ||
    tolerancePx < 0 ||
    ![input.width, input.height, bounds.width, bounds.height].every(
      (value) => Number.isFinite(value) && value > 0,
    ) ||
    ![
      bounds.left,
      bounds.top,
      rect.left,
      rect.top,
      rect.width,
      rect.height,
    ].every(Number.isFinite) ||
    rect.width <= 0 ||
    rect.height <= 0
  )
    return null

  const scaleX = bounds.width / input.width
  const scaleY = bounds.height / input.height
  const cartesianClip: ClipBounds = {
    left: bounds.left + rect.left * scaleX,
    top: bounds.top + rect.top * scaleY,
    right: bounds.left + (rect.left + rect.width) * scaleX,
    bottom: bounds.top + (rect.top + rect.height) * scaleY,
  }
  // 圆图的曲线沿用渲染器的矩形裁剪，单位圆只表示坐标网格。
  const circularClip: ClipBounds = {
    left: bounds.left + 1,
    top: bounds.top + 1,
    right: bounds.left + bounds.width - 1,
    bottom: bounds.top + bounds.height - 1,
  }
  const viewport = smithViewport(input.width, input.height, scaleX, scaleY)
  let selected: SelectableCurve | null = null
  let distance = tolerancePx ** 2
  for (const curve of input.curves) {
    const clip = curve.kind === 'cartesian' ? cartesianClip : circularClip
    if (!inside(point, clip)) continue
    if (
      curve.kind === 'circular'
        ? !validSmithRadius(curve.radius)
        : !Number.isFinite(input.xMin) || !Number.isFinite(input.xMax)
    )
      continue
    let previous: Point | null = null
    let candidate = Infinity
    const count =
      curve.kind === 'cartesian'
        ? curve.frequencyHz.length
        : curve.points.length
    for (let index = 0; index < count; index++) {
      let current: Point | null = null
      if (curve.kind === 'cartesian') {
        const frequency = curve.frequencyHz[index]
        const value = curve.values[index]
        if (
          typeof frequency === 'number' &&
          typeof value === 'number' &&
          Number.isFinite(frequency) &&
          Number.isFinite(value)
        ) {
          current = {
            x:
              bounds.left +
              (rect.left +
                normalizedX(frequency, input.xMin, input.xMax) * rect.width) *
                scaleX,
            y:
              bounds.top +
              (rect.top +
                normalizedY(value, curve.scale, curve.format) * rect.height) *
                scaleY,
          }
        }
      } else {
        const sample = curve.points[index]
        if (sample) {
          const projected = smithPoint(sample, viewport, curve.radius)
          current = {
            x: bounds.left + projected.x * scaleX,
            y: bounds.top + projected.y * scaleY,
          }
        }
      }
      if (!current || !finitePoint(current)) {
        previous = null
        continue
      }
      if (previous && (current.x !== previous.x || current.y !== previous.y)) {
        candidate = Math.min(
          candidate,
          segmentDistanceSquared(point, previous, current, clip),
        )
      }
      // 圆图渲染器为孤立采样点补画圆点；矩形图只有线段可命中。
      if (curve.kind === 'circular' && inside(current, clip)) {
        candidate = Math.min(
          candidate,
          (point.x - current.x) ** 2 + (point.y - current.y) ** 2,
        )
      }
      previous = current
    }
    if (candidate > tolerancePx ** 2) continue
    const tied = Math.abs(candidate - distance) <= 1e-9
    if (
      candidate < distance - 1e-9 ||
      (tied && (!selected || curve.active || !selected.active))
    ) {
      selected = curve
      distance = candidate
    }
  }
  return selected?.name ?? null
}
