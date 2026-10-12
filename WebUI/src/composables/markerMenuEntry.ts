import type { TracePresentation } from '../domain/presentation'
import { traceMarkers } from '../domain/markers'

/** Marker 菜单入口只委托启用首个标记；已有标记及正式选择始终保留后端状态。 */
export function enableMarkerOnEntry(
  presentation: TracePresentation | undefined,
  editable: boolean,
  selectMarker: (id: number) => unknown,
): unknown {
  if (
    !editable ||
    !presentation ||
    traceMarkers(presentation).some((marker) => marker.enabled)
  )
    return
  return selectMarker(1)
}
