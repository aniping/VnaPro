import { isCircularFormat, type DisplayFormat } from '../domain/presentation'
import {
  paintDiagram,
  prepareCanvas,
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
  fontScale?: number
  backgrounds: readonly MixedBackground[]
  curves: readonly MixedCurve[]
}

export interface MixedBackground {
  format: DisplayFormat
  radius: number
}

/** 可见格式的背景先叠加，再绘制曲线；活动坐标只决定刻度与圆图半径。 */
export function paintMixedDiagram(
  canvas: HTMLCanvasElement,
  diagram: MixedDiagram,
  bounds: ScreenBounds,
): void {
  const curves = [...diagram.curves].sort(
    (first, second) => Number(first.active) - Number(second.active),
  )
  const circularAxis = isCircularFormat(diagram.axisFormat)
  if (
    !circularAxis &&
    diagram.backgrounds.every(
      (background) => !isCircularFormat(background.format),
    ) &&
    curves.every((curve) => curve.kind === 'cartesian')
  ) {
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
      { grid: diagram.backgrounds.length > 0 },
    )
    return
  }
  if (!prepareCanvas(canvas, diagram, bounds)) return
  if (
    diagram.backgrounds.some(
      (background) => !isCircularFormat(background.format),
    )
  ) {
    paintDiagram(canvas, { ...diagram, curves: [] }, bounds, {
      clear: false,
      labels: false,
    })
  }
  const circularBackgrounds = new Map<string, MixedBackground>()
  for (const background of diagram.backgrounds) {
    if (isCircularFormat(background.format)) {
      circularBackgrounds.set(
        `${background.format}:${background.radius}`,
        background,
      )
    }
  }
  for (const background of circularBackgrounds.values()) {
    paintSmithDiagram(
      canvas,
      {
        width: diagram.width,
        height: diagram.height,
        radius: background.radius,
        fontScale: diagram.fontScale,
        format: background.format as 'smith' | 'polar' | 'invSmith',
        curves: [],
      },
      bounds,
      { clear: false, labels: false },
    )
  }
  // 所有网格完成后再画刻度，测量曲线最后叠加，黑底不会擦掉迹线。
  paintDiagram(canvas, { ...diagram, curves: [], references: [] }, bounds, {
    clear: false,
    grid: false,
  })
  for (const background of circularBackgrounds.values()) {
    paintSmithDiagram(
      canvas,
      {
        width: diagram.width,
        height: diagram.height,
        radius: background.radius,
        fontScale: diagram.fontScale,
        format: background.format as 'smith' | 'polar' | 'invSmith',
        curves: [],
      },
      bounds,
      { clear: false, grid: false, labels: true },
    )
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
        { clear: false, grid: false, labels: false },
      )
    } else {
      paintDiagram(
        canvas,
        { ...diagram, curves: [curve], references: [] },
        bounds,
        { clear: false, grid: false, labels: false },
      )
    }
  }
}
