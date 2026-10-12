import { numberText } from './formatting'
import { isCircularFormat, type DisplayFormat } from '../domain/presentation'
import type { FrameTrace, MarkerResult } from './traceMath'

export const MIN_SMITH_RADIUS = 0.01

export function validSmithRadius(radius: number): boolean {
  return Number.isFinite(radius) && radius >= MIN_SMITH_RADIUS
}

export interface ComplexPoint {
  real: number
  imaginary: number
}
export interface SmithViewport {
  centerX: number
  centerY: number
  radiusX: number
  radiusY: number
}
/** 以屏幕尺寸建立圆形，独立补偿横纵 CSS 缩放。 */
export function smithViewport(
  width: number,
  height: number,
  scaleX = 1,
  scaleY = 1,
): SmithViewport {
  const radius = Math.max(1, Math.min(width * scaleX, height * scaleY) / 2 - 18)
  return {
    centerX: width / 2,
    centerY: height / 2,
    radiusX: radius / scaleX,
    radiusY: radius / scaleY,
  }
}

export function smithPoint(
  point: ComplexPoint,
  viewport: SmithViewport,
  outerMagnitude: number,
): { x: number; y: number } {
  return {
    x: viewport.centerX + (point.real / outerMagnitude) * viewport.radiusX,
    y: viewport.centerY - (point.imaginary / outerMagnitude) * viewport.radiusY,
  }
}

/** 保留超出单位圆的真实值；无效参考点在曲线中形成间断。 */
export function smithSeries(
  trace: FrameTrace | undefined,
): (ComplexPoint | null)[] {
  if (!trace?.available || !isCircularFormat(trace.projection.format)) return []
  return trace.projection.values.map((real, index) => {
    const imaginary = trace.projection.secondaryValues[index]
    return real !== null && typeof imaginary === 'number'
      ? { real, imaginary }
      : null
  })
}

/** 二维点击仅吸附实际显示采样点，不沿曲线插值补造数据。 */
export function nearestSmithIndex(
  trace: FrameTrace | undefined,
  viewport: SmithViewport,
  radius: number,
  x: number,
  y: number,
  scaleX = 1,
  scaleY = 1,
): number {
  let selected = -1
  let distance = Infinity
  for (const [index, point] of smithSeries(trace).entries()) {
    if (!point) continue
    const projected = smithPoint(point, viewport, radius)
    const candidate = Math.hypot(
      (projected.x - x) * scaleX,
      (projected.y - y) * scaleY,
    )
    if (candidate < distance) {
      distance = candidate
      selected = index
    }
  }
  return selected
}

/** 只格式化后端返回的 Γ 与阻抗读数，不在浏览器执行阻抗换算。 */
export function smithReadout(
  marker: Pick<
    MarkerResult,
    'real' | 'imaginary' | 'impedanceReal' | 'impedanceImag' | 'impedanceState'
  >,
): string {
  const real = marker.real
  const imaginary = marker.imaginary
  if (real === null || imaginary === null) return '—'
  const impedance =
    marker.impedanceState === 'open'
      ? '∞ (open)'
      : marker.impedanceState === 'overflow'
        ? '— (overflow)'
        : marker.impedanceReal === null || marker.impedanceImag === null
          ? '—'
          : `${numberText(marker.impedanceReal, 6)} ${marker.impedanceImag < 0 ? '−' : '+'} j${numberText(Math.abs(marker.impedanceImag), 6)}`
  return `Γ ${numberText(real, 6)} ${imaginary < 0 ? '−' : '+'} j${numberText(Math.abs(imaginary), 6)} · z ${impedance}`
}

/** 圆图直接显示服务器给出的复数或物理阻抗、导纳；不在浏览器换算测量量。 */
export function circularReadout(
  format: DisplayFormat,
  marker: MarkerResult,
  unit = '1',
): string {
  if (format === 'polar')
    return `|z| ${numberText(marker.value, 8)} ${unit} / ∠ ${numberText(marker.secondaryValue, 8)}°`
  const complex = `Re ${numberText(marker.real ?? NaN, 8)} / Im ${numberText(marker.imaginary ?? NaN, 8)}`
  if (format === 'smith' && marker.impedanceState !== 'invalid')
    return smithReadout(marker)
  if (
    format !== 'invSmith' ||
    marker.admittanceState === undefined ||
    marker.admittanceState === 'invalid'
  )
    return complex
  if (marker.admittanceState === 'short') return 'y = ∞ · short'
  if (marker.admittanceState === 'overflow') return 'y overflow'
  return `y = ${numberText(marker.admittanceReal ?? NaN, 8)} ${(marker.admittanceImag ?? 0) < 0 ? '−' : '+'} j${numberText(Math.abs(marker.admittanceImag ?? NaN), 8)}`
}
