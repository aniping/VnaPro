import { isNewTraceDrag, NEW_TRACE_DRAG_TYPE } from '../instrument/newTraceDrag'
import type { TraceDropPosition } from '../../domain/diagramLayout'
export type { TraceDropPosition } from '../../domain/diagramLayout'

export const TRACE_DRAG_TYPE = 'application/x-vnapro-trace'

export interface TraceDragSource {
  name: string
  displayRevision: string
}

export type TraceDragIntent =
  | { kind: 'new' }
  | ({ kind: 'move' } & TraceDragSource)

/** 迹线标题拖放只移动显示归属，不创建测量或复制标记。 */
export function beginTraceDrag(
  event: DragEvent,
  source: TraceDragSource,
  enabled: boolean,
): void {
  if (
    !enabled ||
    !event.dataTransfer ||
    !source.name ||
    !source.displayRevision
  ) {
    event.preventDefault()
    return
  }
  event.dataTransfer.setData(TRACE_DRAG_TYPE, JSON.stringify(source))
  event.dataTransfer.effectAllowed = 'move'
}

/** dragover 期间浏览器只开放 MIME types，不能依赖 getData 读取源。 */
export function isTraceDrag(event: DragEvent): boolean {
  return event.dataTransfer?.types?.includes(TRACE_DRAG_TYPE) ?? false
}

export function traceDragIntent(event: DragEvent): TraceDragIntent | null {
  if (isNewTraceDrag(event)) {
    return event.dataTransfer?.getData(NEW_TRACE_DRAG_TYPE) === 'new-trace'
      ? { kind: 'new' }
      : null
  }
  if (!isTraceDrag(event)) return null
  try {
    const source: unknown = JSON.parse(
      event.dataTransfer!.getData(TRACE_DRAG_TYPE),
    )
    if (
      typeof source !== 'object' ||
      !source ||
      !('name' in source) ||
      typeof source.name !== 'string' ||
      !source.name ||
      !('displayRevision' in source) ||
      typeof source.displayRevision !== 'string' ||
      !/^[0-9]+$/.test(source.displayRevision)
    )
      return null
    return {
      kind: 'move',
      name: source.name,
      displayRevision: source.displayRevision,
    }
  } catch {
    return null
  }
}

/** 五区边界和商用仪器的黄色对角线一致，缩放后仍覆盖整个图窗。 */
export function traceDropPosition(
  x: number,
  y: number,
  width: number,
  height: number,
  allowSplit = true,
): TraceDropPosition | null {
  if (
    ![x, y, width, height].every(Number.isFinite) ||
    width <= 0 ||
    height <= 0 ||
    x < 0 ||
    y < 0 ||
    x > width ||
    y > height
  )
    return null
  const horizontal = x / width
  const vertical = y / height
  if (
    horizontal >= 1 / 3 &&
    horizontal <= 2 / 3 &&
    vertical >= 1 / 3 &&
    vertical <= 2 / 3
  )
    return 'center'
  if (!allowSplit) return null
  const dx = horizontal - 0.5
  const dy = vertical - 0.5
  if (Math.abs(dy) >= Math.abs(dx)) return dy < 0 ? 'above' : 'below'
  return dx < 0 ? 'left' : 'right'
}
