import type { AcquiredFrame, Measurement, ReceiverTrace } from '../api/types'
import type { DisplayFormat, TracePresentation } from './presentation'
import { activeMarkerId, traceMarkers } from './markers'
import { markerResult, type MarkerResult } from '../plot/traceMath'

export interface MarkerReadoutItem {
  name: string
  markerId: number
  color: string
  format: DisplayFormat
  measurement: Measurement
  result: MarkerResult
  active: boolean
  preview?: boolean
}

export interface MarkerReadoutPreview {
  name: string
  markerId: number
  result: MarkerResult
}

export interface MarkerInfoField {
  name: string
  color: string
  markers: MarkerReadoutItem[]
}

/** 适配 C++ 的独立标记投影，表格与图内信息框共享此读数入口。 */
export function markerReadouts(
  frame: AcquiredFrame | null,
  trace: ReceiverTrace | undefined,
  measurement: Measurement,
  presentation: TracePresentation,
  color: string,
  activeTraceName: string,
): MarkerReadoutItem[] {
  if (!presentation.visible) return []
  return traceMarkers(presentation).flatMap((marker) => {
    if (!marker.enabled) return []
    const result = markerResult(
      frame,
      trace,
      marker.frequencyHz,
      presentation.format,
      marker.sampleIndex,
      marker.id,
    )
    return result
      ? [
          {
            name: measurement.name,
            markerId: marker.id,
            color,
            format: presentation.format,
            measurement,
            result,
            active:
              measurement.name === activeTraceName &&
              marker.id === activeMarkerId(presentation),
          },
        ]
      : []
  })
}

/** 手势预览仅替换已有标记的显示读数；取消、提交或失败后回到后端快照。 */
export function previewMarkerReadouts(
  markers: MarkerReadoutItem[],
  preview: MarkerReadoutPreview | null,
): MarkerReadoutItem[] {
  return markers.map((marker) =>
    preview?.name === marker.name && preview.markerId === marker.markerId
      ? { ...marker, result: preview.result, preview: true }
      : marker,
  )
}

export const MARKER_INFO_DRAG_TYPE = 'application/x-vnapro-marker-info'

/** 拖放只携带归属名，字段有效性、控制权与版本仍由后端执行时检查。 */
export function markerInfoDragName(event: DragEvent): string | null {
  const name = event.dataTransfer?.getData(MARKER_INFO_DRAG_TYPE)
  return name && name.length <= 128 ? name : null
}
