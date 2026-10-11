import type { Measurement } from '../api/types'
import { numberText } from './formatting'
import { validAt, type FrameTrace } from './traceMath'

export const MIN_SMITH_RADIUS = 0.01

export function validSmithRadius(radius: number): boolean {
  return Number.isFinite(radius) && radius >= MIN_SMITH_RADIUS
}

export interface ComplexPoint { real: number; imaginary: number }
export interface SmithViewport { centerX: number; centerY: number; radiusX: number; radiusY: number }
export interface NormalizedImpedance {
  state: 'finite' | 'open' | 'overflow' | 'invalid'
  real: number | null
  imaginary: number | null
}

export function supportsSmith(measurement: Measurement | undefined): boolean {
  return measurement?.kind === 'sParameter'
    && measurement.receiverPort === measurement.sourcePort
    && (measurement.parameter === 'S11' || measurement.parameter === 'S22')
}

/** 以屏幕尺寸建立圆形，独立补偿横纵 CSS 缩放。 */
export function smithViewport(width: number, height: number, scaleX = 1, scaleY = 1): SmithViewport {
  const radius = Math.max(1, Math.min(width * scaleX, height * scaleY) / 2 - 18)
  return { centerX: width / 2, centerY: height / 2, radiusX: radius / scaleX, radiusY: radius / scaleY }
}

export function smithPoint(point: ComplexPoint, viewport: SmithViewport, outerMagnitude: number): { x: number; y: number } {
  return { x: viewport.centerX + point.real / outerMagnitude * viewport.radiusX,
    y: viewport.centerY - point.imaginary / outerMagnitude * viewport.radiusY }
}

/** 保留超出单位圆的真实值；无效参考点在曲线中形成间断。 */
export function smithSeries(trace: FrameTrace | undefined): (ComplexPoint | null)[] {
  if (!supportsSmith(trace)) return []
  return trace!.real.map((real, index) => validAt(trace, index)
    ? { real: real!, imaginary: trace!.imag[index]! } : null)
}

export function smithAutoscale(trace: FrameTrace | undefined): number | null {
  const points = smithSeries(trace).filter((point): point is ComplexPoint => point !== null)
  if (!points.length) return null
  const maximum = Math.max(...points.map(point => Math.hypot(point.real, point.imaginary)))
  const radius = Math.max(MIN_SMITH_RADIUS, maximum * 1.2)
  return Number.isFinite(radius) ? radius : null
}

/** 二维点击仅吸附实际显示采样点，不沿曲线插值补造数据。 */
export function nearestSmithIndex(trace: FrameTrace | undefined, viewport: SmithViewport,
  radius: number, x: number, y: number, scaleX = 1, scaleY = 1): number {
  let selected = -1
  let distance = Infinity
  for (const [index, point] of smithSeries(trace).entries()) {
    if (!point) continue
    const projected = smithPoint(point, viewport, radius)
    const candidate = Math.hypot((projected.x - x) * scaleX, (projected.y - y) * scaleY)
    if (candidate < distance) { distance = candidate; selected = index }
  }
  return selected
}

/** z = (1 + Γ)/(1 - Γ)，仅为归一化阻抗，不假定物理参考阻抗。 */
export function normalizedImpedance(real: number, imaginary: number): NormalizedImpedance {
  if (!Number.isFinite(real) || !Number.isFinite(imaginary)) return { state: 'invalid', real: null, imaginary: null }
  if (real === 1 && imaginary === 0) return { state: 'open', real: null, imaginary: null }
  const scale = Math.max(Math.abs(1 - real), Math.abs(imaginary))
  const numeratorReal = real / scale + 1 / scale
  const numeratorImaginary = imaginary / scale
  const denominatorReal = (1 - real) / scale
  const denominatorImaginary = -imaginary / scale
  const denominator = denominatorReal ** 2 + denominatorImaginary ** 2
  const resistance = (numeratorReal * denominatorReal + numeratorImaginary * denominatorImaginary) / denominator
  const reactance = (numeratorImaginary * denominatorReal - numeratorReal * denominatorImaginary) / denominator
  return Number.isFinite(resistance) && Number.isFinite(reactance)
    ? { state: 'finite', real: resistance, imaginary: reactance }
    : { state: 'overflow', real: null, imaginary: null }
}

export function smithReadout(real: number, imaginary: number): string {
  const impedance = normalizedImpedance(real, imaginary)
  const value = impedance.state === 'open' ? '∞ (open)' : impedance.state === 'finite'
    ? `${numberText(impedance.real!, 6)} ${impedance.imaginary! < 0 ? '−' : '+'} j${numberText(Math.abs(impedance.imaginary!), 6)}`
    : '— (overflow)'
  return `Γ ${numberText(real, 6)} ${imaginary < 0 ? '−' : '+'} j${numberText(Math.abs(imaginary), 6)} · z ${value}`
}
