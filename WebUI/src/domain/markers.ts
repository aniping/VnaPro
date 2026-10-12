import type { TracePresentation } from './presentation'

export const MARKER_COUNT = 10
export const MARKER_IDS = Array.from(
  { length: MARKER_COUNT },
  (_, index) => index + 1,
)

export interface MarkerState {
  enabled: boolean
  frequencyHz: number | null
  /** 后端完整原始帧索引，不是前端抽取数组位置。 */
  sampleIndex?: number | null
  discrete: boolean
}

export interface MarkerDefinition extends MarkerState {
  id: number
}

/** 只读兼容旧服务；存在集合时始终使用后端集合，不回写活动标记别名。 */
export function traceMarkers(
  presentation: TracePresentation | undefined,
): readonly MarkerDefinition[] {
  if (!presentation) return []
  return presentation.markers ?? [{ id: 1, ...presentation.marker }]
}

export function activeMarkerId(
  presentation: TracePresentation | undefined,
): number {
  return presentation?.activeMarkerId ?? 1
}

export function activeMarker(
  presentation: TracePresentation | undefined,
): MarkerDefinition | undefined {
  return traceMarkers(presentation).find(
    (marker) => marker.id === activeMarkerId(presentation),
  )
}
