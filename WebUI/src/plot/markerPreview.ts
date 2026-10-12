import type { AcquiredFrame } from '../api/types'
import type { DisplayFormat, DisplayScale } from '../domain/presentation'
import { isCircularFormat } from '../domain/presentation'
import type { PlotRect } from './canvasRenderer'
import type { MarkerPreviewPoint } from './markerDrag'
import { smithPoint, type SmithViewport } from './smithMath'
import {
  normalizedX,
  normalizedY,
  projectedValue,
  validAt,
  type FrameTrace,
  type MarkerResult,
} from './traceMath'

interface MarkerPreviewGeometry {
  area: PlotRect
  range: { min: number; max: number }
  scale: DisplayScale
  smithArea: SmithViewport
  smithRadius: number
}

/** 只选取后端投影中的已显示频点；像素和读数冻结于同一次手势。 */
export function createMarkerPreviewPoints(
  frame: AcquiredFrame | null,
  trace: FrameTrace | undefined,
  format: DisplayFormat,
  geometry: MarkerPreviewGeometry,
): MarkerPreviewPoint[] {
  if (!frame || !trace?.available || trace.projection.format !== format)
    return []
  const projection = trace.projection
  return projection.values.flatMap((primary, index) => {
    const frequencyHz = frame.frequencyHz[index]
    const sampleIndex = frame.sampleIndexes[index]
    const value = projectedValue(primary, projection.valueStates[index]!)
    const secondaryValue = projectedValue(
      projection.secondaryValues[index] ?? null,
      projection.secondaryStates[index]!,
    )
    if (
      !validAt(trace, index) ||
      !Number.isFinite(frequencyHz) ||
      !Number.isInteger(sampleIndex) ||
      sampleIndex === undefined ||
      sampleIndex < 0 ||
      frequencyHz === undefined ||
      !Number.isFinite(value) ||
      (isCircularFormat(format) && !Number.isFinite(secondaryValue))
    )
      return []
    const result: MarkerResult = {
      index: sampleIndex,
      frequencyHz,
      valid: true,
      value:
        format === 'polar'
          ? projectedValue(
              projection.markerValues?.[index] ?? null,
              projection.markerValueStates?.[index] ?? 'undefined',
            )
          : value,
      secondaryValue:
        format === 'polar'
          ? projectedValue(
              projection.markerSecondaryValues?.[index] ?? null,
              projection.markerSecondaryStates?.[index] ?? 'undefined',
            )
          : secondaryValue,
      real: isCircularFormat(format) ? value : trace.real[index]!,
      imaginary: isCircularFormat(format) ? secondaryValue : trace.imag[index]!,
      // 旧服务没有逐点阻抗时显示缺省值，不把旧标记的阻抗拼到新的 Γ。
      impedanceReal: projection.impedanceRealValues?.[index] ?? null,
      impedanceImag: projection.impedanceImagValues?.[index] ?? null,
      impedanceState: projection.impedanceStates?.[index] ?? 'invalid',
      admittanceReal: projection.admittanceRealValues?.[index] ?? null,
      admittanceImag: projection.admittanceImagValues?.[index] ?? null,
      admittanceState: projection.admittanceStates?.[index] ?? 'invalid',
      interpolated: false,
      reason: '',
    }
    const point = isCircularFormat(format)
      ? smithPoint(
          { real: value, imaginary: secondaryValue },
          geometry.smithArea,
          geometry.smithRadius,
        )
      : {
          x:
            geometry.area.left +
            normalizedX(frequencyHz, geometry.range.min, geometry.range.max) *
              geometry.area.width,
          y:
            geometry.area.top +
            normalizedY(value, geometry.scale, format) * geometry.area.height,
        }
    return Number.isFinite(point.x) && Number.isFinite(point.y)
      ? [{ ...point, sampleIndex, result }]
      : []
  })
}
