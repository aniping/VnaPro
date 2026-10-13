import {
  prepareCanvas,
  type PaintPass,
  type ScreenBounds,
} from './canvasRenderer'
import {
  smithPoint,
  smithViewport,
  validSmithRadius,
  type ComplexPoint,
} from './smithMath'

export interface SmithCurve {
  points: readonly (ComplexPoint | null)[]
  color: string
  active: boolean
}
export interface SmithDiagram {
  width: number
  height: number
  radius: number
  format?: 'smith' | 'polar' | 'invSmith'
  curves: readonly SmithCurve[]
}

/** 圆图在最终屏幕坐标绘制，非方形视口及 CSS 缩放不能拉伸单位圆。 */
export function paintSmithDiagram(
  canvas: HTMLCanvasElement,
  diagram: SmithDiagram,
  bounds: ScreenBounds,
  pass: PaintPass = {},
): void {
  if (!validSmithRadius(diagram.radius)) return
  const prepared = prepareCanvas(canvas, diagram, bounds, pass.clear !== false)
  if (!prepared) return
  const { context, raster } = prepared
  const viewport = smithViewport(
    diagram.width,
    diagram.height,
    raster.cssScaleX,
    raster.cssScaleY,
  )
  const point = (real: number, imaginary: number) => {
    const value = smithPoint({ real, imaginary }, viewport, diagram.radius)
    return { x: value.x * raster.cssScaleX, y: value.y * raster.cssScaleY }
  }
  if (pass.grid !== false && diagram.format !== 'polar') {
    const gridPoint = (real: number, imaginary: number) =>
      diagram.format === 'invSmith'
        ? point(-real, -imaginary)
        : point(real, imaginary)
    const center = gridPoint(0, 0)
    const unitRadius = (viewport.radiusX * raster.cssScaleX) / diagram.radius
    context.setTransform(
      raster.density,
      0,
      0,
      raster.density,
      raster.offsetX,
      raster.offsetY,
    )
    context.lineWidth = 1 / raster.density
    context.strokeStyle = '#4b5f6c'
    context.save()
    context.beginPath()
    context.arc(center.x, center.y, unitRadius, 0, Math.PI * 2)
    context.clip()
    const circle = (real: number, imaginary: number, radius: number) => {
      const location = gridPoint(real, imaginary)
      context.beginPath()
      context.arc(location.x, location.y, radius * unitRadius, 0, Math.PI * 2)
      context.stroke()
    }
    for (const resistance of [0.2, 0.5, 1, 2, 5]) {
      circle(resistance / (1 + resistance), 0, 1 / (1 + resistance))
    }
    for (const reactance of [0.2, 0.5, 1, 2, 5]) {
      circle(1, 1 / reactance, 1 / reactance)
      circle(1, -1 / reactance, 1 / reactance)
    }
    const left = gridPoint(-1, 0)
    const right = gridPoint(1, 0)
    context.beginPath()
    context.moveTo(left.x, left.y)
    context.lineTo(right.x, right.y)
    context.stroke()
    context.restore()
    context.beginPath()
    context.arc(center.x, center.y, unitRadius, 0, Math.PI * 2)
    context.stroke()
    if (diagram.radius !== 1) {
      context.strokeStyle = '#526973'
      context.setLineDash([3, 3])
      context.beginPath()
      context.arc(
        center.x,
        center.y,
        viewport.radiusX * raster.cssScaleX,
        0,
        Math.PI * 2,
      )
      context.stroke()
      context.setLineDash([])
    }

    context.fillStyle = '#8ca0a8'
    const fontSize = 11 * Math.min(raster.cssScaleX, raster.cssScaleY)
    // 小圆图仅减少坐标文字；这是项目像素可读性规则，不改变网格或后端参考半径。
    const compactLabels = unitRadius < fontSize * 12
    context.font = `${fontSize}px "Segoe UI", sans-serif`
    context.textAlign = 'center'
    context.textBaseline = 'bottom'
    for (const resistance of compactLabels ? [0, 1] : [0, 0.2, 0.5, 1, 2, 5]) {
      const location = gridPoint((resistance - 1) / (resistance + 1), 0)
      context.fillText(String(resistance), location.x, location.y - 2)
    }
    context.fillText('∞', right.x, right.y - 2)
    for (const reactance of compactLabels ? [1] : [0.5, 1, 2, 5]) {
      const denominator = 1 + reactance ** 2
      for (const sign of [-1, 1]) {
        const location = gridPoint(
          (reactance ** 2 - 1) / denominator,
          (sign * 2 * reactance) / denominator,
        )
        context.fillText(String(sign * reactance), location.x, location.y - 2)
      }
    }
  }

  if (pass.grid !== false && diagram.format === 'polar') {
    context.setTransform(
      raster.density,
      0,
      0,
      raster.density,
      raster.offsetX,
      raster.offsetY,
    )
    context.strokeStyle = '#4b5f6c'
    context.lineWidth = 1 / raster.density
    const center = point(0, 0)
    const outer = viewport.radiusX * raster.cssScaleX
    for (const fraction of [0.25, 0.5, 0.75, 1]) {
      context.beginPath()
      context.arc(center.x, center.y, outer * fraction, 0, 2 * Math.PI)
      context.stroke()
    }
    for (let division = 0; division < 8; division++) {
      const angle = (division * Math.PI) / 4
      context.beginPath()
      context.moveTo(center.x, center.y)
      context.lineTo(
        center.x + outer * Math.cos(angle),
        center.y + outer * Math.sin(angle),
      )
      context.stroke()
    }
  }

  context.setTransform(
    raster.density,
    0,
    0,
    raster.density,
    raster.offsetX,
    raster.offsetY,
  )
  context.save()
  context.beginPath()
  context.rect(
    1,
    1,
    diagram.width * raster.cssScaleX - 2,
    diagram.height * raster.cssScaleY - 2,
  )
  context.clip()
  for (const curve of [...diagram.curves].sort(
    (first, second) => Number(first.active) - Number(second.active),
  )) {
    context.strokeStyle = curve.color
    context.fillStyle = curve.color
    context.lineWidth = 1
    context.beginPath()
    let previous: { x: number; y: number } | null = null
    let segmentMoved = false
    const isolated: { x: number; y: number }[] = []
    const finishSegment = () => {
      if (!segmentMoved && previous) isolated.push(previous)
      previous = null
      segmentMoved = false
    }
    for (const sample of curve.points) {
      const location = sample ? point(sample.real, sample.imaginary) : null
      if (
        !location ||
        !Number.isFinite(location.x) ||
        !Number.isFinite(location.y)
      ) {
        finishSegment()
        continue
      }
      if (previous) {
        context.lineTo(location.x, location.y)
        segmentMoved ||= location.x !== previous.x || location.y !== previous.y
      } else context.moveTo(location.x, location.y)
      previous = location
    }
    finishSegment()
    context.stroke()
    for (const location of isolated) {
      context.beginPath()
      context.arc(location.x, location.y, 1, 0, Math.PI * 2)
      context.fill()
    }
  }
  context.restore()
}
