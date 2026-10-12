import { isCircularFormat, type DisplayFormat } from '../domain/presentation'
import {
  paintDiagram,
  type CanvasCurve,
  type CanvasDiagram,
  type ScreenBounds,
} from './canvasRenderer'
import { paintSmithDiagram, type SmithCurve } from './smithRenderer'

export type MixedCurve =
  | (CanvasCurve & { kind: 'cartesian'; format: DisplayFormat })
  | (SmithCurve & {
      kind: 'circular'
      format: 'smith' | 'polar' | 'invSmith'
      radius: number
    })

export interface MixedDiagram extends Omit<CanvasDiagram, 'curves'> {
  axisFormat: DisplayFormat
  radius: number
  curves: readonly MixedCurve[]
}

/** 网格只由活动坐标决定；混合格式各自投影，绘图排序不改变迹线标题和后端归属。 */
export function paintMixedDiagram(
  canvas: HTMLCanvasElement,
  diagram: MixedDiagram,
  bounds: ScreenBounds,
): void {
  const curves = [...diagram.curves].sort(
    (first, second) => Number(first.active) - Number(second.active),
  )
  const circularAxis = isCircularFormat(diagram.axisFormat)
  if (!circularAxis && curves.every((curve) => curve.kind === 'cartesian')) {
    paintDiagram(
      canvas,
      {
        ...diagram,
        curves: curves.filter(
          (curve): curve is Extract<MixedCurve, { kind: 'cartesian' }> =>
            curve.kind === 'cartesian',
        ),
      },
      bounds,
    )
    return
  }
  if (circularAxis) {
    paintSmithDiagram(
      canvas,
      {
        width: diagram.width,
        height: diagram.height,
        radius: diagram.radius,
        format: diagram.axisFormat as 'smith' | 'polar' | 'invSmith',
        curves: [],
      },
      bounds,
    )
  } else {
    paintDiagram(canvas, { ...diagram, curves: [] }, bounds)
  }
  for (const curve of curves) {
    if (curve.kind === 'circular') {
      paintSmithDiagram(
        canvas,
        {
          width: diagram.width,
          height: diagram.height,
          radius: curve.radius,
          format: curve.format,
          curves: [curve],
        },
        bounds,
        { clear: false, grid: false },
      )
    } else {
      paintDiagram(
        canvas,
        { ...diagram, curves: [curve], references: [] },
        bounds,
        { clear: false, grid: false },
      )
    }
  }
}
