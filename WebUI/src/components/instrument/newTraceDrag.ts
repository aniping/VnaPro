/** 拖放只表达打开新迹线选择框，正式测量创建等到选定参数后才提交。 */
export const NEW_TRACE_DRAG_TYPE = 'application/x-vnapro-new-trace'

export function beginNewTraceDrag(event: DragEvent, enabled: boolean): void {
  if (!enabled || !event.dataTransfer) {
    event.preventDefault()
    return
  }
  event.dataTransfer.setData(NEW_TRACE_DRAG_TYPE, 'new-trace')
  event.dataTransfer.effectAllowed = 'copy'
}

export function isNewTraceDrag(event: DragEvent): boolean {
  return event.dataTransfer?.types?.includes(NEW_TRACE_DRAG_TYPE) ?? false
}
